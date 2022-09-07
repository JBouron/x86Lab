#include <x86lab/vm.hpp>
#include <functional>
#include <map>

namespace X86Lab {

Vm::State::Registers::Registers(kvm_regs const& regs, kvm_sregs const& sregs) :
    rax(regs.rax), rbx(regs.rbx), rcx(regs.rcx), rdx(regs.rdx),
    rdi(regs.rdi), rsi(regs.rsi), rsp(regs.rsp), rbp(regs.rbp),
    r8(regs.r8),   r9(regs.r9),   r10(regs.r10), r11(regs.r11),
    r12(regs.r12), r13(regs.r13), r14(regs.r14), r15(regs.r15),

    rflags(regs.rflags), rip(regs.rip),

    cs(sregs.cs.selector), ds(sregs.ds.selector), es(sregs.es.selector),
    fs(sregs.fs.selector), gs(sregs.gs.selector), ss(sregs.ss.selector),

    cr0(sregs.cr0), cr2(sregs.cr2), cr3(sregs.cr3), cr4(sregs.cr4),
    cr8(sregs.cr8), efer(sregs.efer),

    idt({.base = sregs.idt.base, .limit = sregs.idt.limit}),
    gdt({.base = sregs.gdt.base, .limit = sregs.gdt.limit}) {}

Vm::State::Registers const& Vm::State::registers() const {
    return regs;
}

Vm::State::Memory const& Vm::State::memory() const {
    return mem;
}

Vm::State::State(Registers const& regs, Memory && mem) :
    regs(regs),
    mem(std::move(mem)) {}

Vm::Vm(CpuMode const startMode, u64 const memorySize) :
    vmFd(Util::Kvm::createVm()),
    vcpuFd(Util::Kvm::createVcpu(vmFd)),
    kvmRun(Util::Kvm::getVcpuRunStruct(vcpuFd)),
    physicalMemorySize(memorySize * PAGE_SIZE),
    memory(std::get<0>(addPhysicalMemory(memorySize))),
    currState(OperatingState::NoCodeLoaded)
    {
    // Disable any MSR access filtering. KVM's doc indicate that if this is not
    // done then the default behaviour is used. However it's not really clear if
    // the default behaviour allows access to MSRs or not. Hence disable it here
    // completely.
    Util::Kvm::disableMsrFiltering(vmFd);

    // Setup access to CPUID information. We don't want to "hide" anything from
    // the guest, having CPUID instruction available can always be useful.
    Util::Kvm::setupCpuid(vcpuFd);

    // Setup the registers depending on the requested mode and honor the initial
    // value of registers documented in .hpp.
    setRegistersInitialValue(startMode);
}

void Vm::loadCode(u8 const * const shellCode, u64 const shellCodeSize) {
    // For now the code is always loaded at address 0x0.
    std::memcpy(memory, shellCode, shellCodeSize);

    // Set RIP to first instruction.
    State::Registers regs(getRegisters());
    regs.rip = 0x0;
    setRegisters(regs);

    // The KVM is now runnable.
    currState = OperatingState::Runnable;
}

std::unique_ptr<Vm::State> Vm::getState() const {
    State::Registers const regs(getRegisters());
    Vm::State::Memory mem({
        .data = std::unique_ptr<u8[]>(new u8[physicalMemorySize]),
        .size = physicalMemorySize,
    });
    std::memcpy(mem.data.get(), memory, physicalMemorySize);
    return std::unique_ptr<Vm::State>(new Vm::State(regs, std::move(mem)));
}

Vm::State::Registers Vm::getRegisters() const {
    kvm_regs const regs(Util::Kvm::getRegs(vcpuFd));
    kvm_sregs const sregs(Util::Kvm::getSRegs(vcpuFd));
    return State::Registers(regs, sregs);
}

void Vm::setRegisters(State::Registers const& registerValues) {
    kvm_regs const regs({
        .rax    = registerValues.rax, .rbx    = registerValues.rbx,
        .rcx    = registerValues.rcx, .rdx    = registerValues.rdx,
        .rsi    = registerValues.rsi, .rdi    = registerValues.rdi,
        .rsp    = registerValues.rsp, .rbp    = registerValues.rbp,
        .r8     = registerValues.r8,  .r9     = registerValues.r9,
        .r10    = registerValues.r10, .r11    = registerValues.r11,
        .r12    = registerValues.r12, .r13    = registerValues.r13,
        .r14    = registerValues.r14, .r15    = registerValues.r15,
        .rip    = registerValues.rip, .rflags = registerValues.rflags,
    });
    Util::Kvm::setRegs(vcpuFd, regs);

    // State::Registers doesn't quite contain all the values that kvm_sregs has.
    // Hence for those missing values, we read the current kvm_sregs to re-use
    // them in the call to KVM_SET_SREGS.
    kvm_sregs sregs(Util::Kvm::getSRegs(vcpuFd));

    // Set the control registers, efer and the IDT/GDT. Segment registers are
    // left untouched since setting those using setRegisters is not supported.
    sregs.cr0 = registerValues.cr0;
    sregs.cr2 = registerValues.cr2;
    sregs.cr3 = registerValues.cr3;
    sregs.cr4 = registerValues.cr4;
    sregs.cr8 = registerValues.cr8;
    sregs.efer = registerValues.efer;
    sregs.idt.base = registerValues.idt.base;
    sregs.idt.limit = registerValues.idt.limit;
    sregs.gdt.base = registerValues.gdt.base;
    sregs.gdt.limit = registerValues.gdt.limit;

    Util::Kvm::setSRegs(vcpuFd, sregs);
}

Vm::OperatingState Vm::operatingState() const {
    return currState;
}

Vm::OperatingState Vm::step() {
    // Enable debug on guest vcpu in order to be able to do single
    // stepping.
    // The documentation is sparse on this, but it seems that single
    // stepping gets disabled everytime registers are set using
    // KVM_SET_REGS. Hence do it right before the call to KVM_RUN.
    kvm_guest_debug dbg;
    dbg.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
    if (::ioctl(vcpuFd, KVM_SET_GUEST_DEBUG, &dbg) == -1) {
        currState = OperatingState::SingleStepError;
        throw KvmError("Cannot set guest debug", errno);
    }

    if (::ioctl(vcpuFd, KVM_RUN, NULL) != 0) {
        currState = OperatingState::SingleStepError;
        throw KvmError("Cannot run VM", errno);
    }

    if (kvmRun.exit_reason == KVM_EXIT_DEBUG) {
        // The execution stopped after one step, we are still runnable.
        currState = OperatingState::Runnable;
    } else if (kvmRun.exit_reason == KVM_EXIT_SHUTDOWN) {
        // Execution stopped the host. This is most likely a triple-fault hehe.
        currState = OperatingState::Shutdown;
    } else if (kvmRun.exit_reason == KVM_EXIT_HLT) {
        // The single step executed a halt instruction.
        currState = OperatingState::Halted;
    } else {
        // For now consider everything else as an error.
        currState = OperatingState::SingleStepError;
    }
    return currState;
}

// See computeSegmentRegister.
enum class SegmentType {
    Code,
    Data,
};

// Compute the value of the hidden parts of a segment register to represent a
// flat segment of the given type in the given cpu mode.
// For mode == RealMode, code and data segments are set to 0.
// For mode == ProtectedMode, code and data segments are set to have base 0 and
// limit 0xFFFFF with page granularity (eg. segments cover the entire 4GiB addr
// space). Code segment is read-only and data segment is read-write. DPL is set
// to 0.
// For mode == LongMode, code and data segments are set to 64-bit flat segments
// with DPL == 0. Since there is no segmentation in 64-bit, this is equivalent
// to have base == 0 and limit == ~0.
// @param mode: The cpu mode. This is required because segment registers are
// interpreted differently depending in which mode the cpu is running.
// @param type: Indicate if the segment should be a code segment or data
// segment.
// @param rflags: The current value of rflags on the guest.
// @return: A kvm_segment with the hidden parts set accordingly.
static kvm_segment computeSegmentRegister(Vm::CpuMode const mode,
                                          SegmentType const type,
                                          u64 const rflags) {
    // VMX is _very_ peculiar about the state of the segment registers (hidden
    // parts) upon a VMentry. AFAIK, the kvm implementation of the Linux kernel
    // does not help us here (except for the accessed flag which is set in
    // https://elixir.bootlin.com/linux/v5.19.6/source/arch/x86/kvm/vmx/vmx.c
    // line 3420. Hence we need to do our best to follows VMX expectations,
    // everything we need to know about segment registers is described in the
    // Intel's manual vol. 3, "26.3.1.2 Checks on Guest Segment Registers".
    // This leaves something rather obvious: we are here targetting Intel's VMX,
    // and therefore this might not work with AMD's SVM as its spec probably
    // differs in that regards.

    if (rflags & (1 << 17)) {
        // Bit 17 in RFLAGS indicate that the guest is running in virtual-8086
        // mode, which imposes different requirements compared to "normal" real
        // mode, protected mode or IA-32e mode.
        // For now we don't support setting the segment registers if the guest
        // is staring in virtual 8086 mode.
        throw Error("Guest startup in virtual8086 mode not supported", 0);
    }

    kvm_segment seg{};

    // Few restrictions on the selector. It's value is irrelevant for execution
    // since it is the hidden parts that are used for logical address
    // translation.
    // Nevertheless, in the case of SS, the RPL in the selector must be equal to
    // CS's RPL. Since we are running everything in ring 0 this is by default.
    // For TR and LDTR, the TI bit must be 0.
    seg.selector = 0;

    // The base address must be canonical and bits 63:32 must be 0. GS and FS
    // do not have this restriction.
    seg.base = 0x0;

    // No restriction on limit if we are not in virtual8086 mode. Hence set it
    // to the max.
    // Note that this value is expressed in byte, even if the corresponding
    // segment is using page granularity.
    seg.limit = 0xFFFFFFFF;

    // Type:
    //  - For CS this must be either 3, 9, 11, 13 or 15.
    //  - For SS this must be either 3 or 7.
    //  - DS, ES, FS and GS do not have restrictions on type.
    // Note that the accessed flag must be set, hence add 1 to the type above.
    // In our case we can use 9 for CS and 3 for all the other segment
    // registers.
    seg.type = (type == SegmentType::Code) ? 0xb : 0x3;

    // Must be 1.
    seg.present = 1;

    // Won't go into details here, the DPL of CS and SS are related and in
    // general related to the selector's RPL. Setting everything to 0 here is
    // valid.
    seg.dpl = 0;

    // Only set if the current mode is 32-bit (this indicates 32-bit default
    // operation size).
    seg.db = !!(mode == Vm::CpuMode::ProtectedMode);

    // Must be 1.
    seg.s = 1;

    // Only set if the current mode is 64-bit. If L is set then DB must be
    // unset.
    seg.l = !!(mode == Vm::CpuMode::LongMode);

    // Granularity bit:
    //  - If any bit in limit[11:0] is 0 then G must be 0.
    //  - If any bit in limit[31:20] is 1, then G must be 1.
    // This might seem odd at first, but I think this is explained by the
    // following:
    // When a segment register is loaded with a descriptor using page
    // granularity (e.g. G is set), Intel's documentation specifies that the
    // bottom 12 bits of the hidden limit are ignored when checking offsets.
    // However the documentation does not specify what those bits are. After
    // experimenting a bit (pun intended), it seems that those 12 lower bits are
    // set to 1. This is why having any 0 in limit[11:0] implies that G must be
    // 0.
    // If there are any set bit in limit[31:20] then we are obviously using page
    // granularity (e.g. G is set) since the limit field in a segment descriptor
    // is only 20 bits. Having limit[31:20] != 0 implies that a shift occured:
    // the shift to translate the page granularity to the bytes limit in the
    // hidden part.
    // This leaves one corner case: what if limit[11:0] == 111...111 and
    // limit[31:20] is 0. Such a limit would be valid for both byte and page
    // granularity and therefore imposes no restriction on the G bit.
    // In our case, we want a flat segment model spanning the entire address
    // space hence use page granularity.
    seg.g = 1;

    // Indicate if this segment is usuable or not. I imagine this is used by the
    // micro-arch to know if the segment register has been initialized.
    // In our case, we want all segment registers to be defined/initialized
    // hence unset the bit.
    seg.unusable = 0;
    return seg;
}

void Vm::setRegistersInitialValue(CpuMode const mode) {
    kvm_sregs sregs(Util::Kvm::getSRegs(vcpuFd));

    // Setup the control registers for the requested cpu mode.
    enableCpuMode(sregs, mode);

    // Setup the segment registers.
    u64 const initialRflags(0x2);
    sregs.cs = computeSegmentRegister(mode, SegmentType::Code, initialRflags);
    sregs.ds = computeSegmentRegister(mode, SegmentType::Data, initialRflags);
    sregs.es = sregs.ds;
    sregs.fs = sregs.ds;
    sregs.gs = sregs.ds;
    sregs.ss = sregs.ds;

    // VMX allows us to set the LDTR to unusable. Do that so we don't have to
    // bother carefully crafting a valid value.
    sregs.ldt.unusable = 1;

    // Not so lucky with TR, which cannot be unusable. Set it up to an empty
    // segment (e.g NULL entry in GDT). This is fine, as long as the code is not
    // trying to execute a task without setting up the proper data-structures
    // first.
    // Per Intel's docs, type must be 11 (so it works in all CpuModes), s must
    // be 0, present must be 1, base and limit are free so set them to 0x0 so we
    // get an exception in case we try to exec the task.
    sregs.tr.selector = 0;
    sregs.tr.type = 11;
    sregs.tr.s = 0;
    sregs.tr.present = 1;
    sregs.tr.base = 0x0;
    sregs.tr.limit = 0x0;
    sregs.tr.g = 0;

    Util::Kvm::setSRegs(vcpuFd, sregs);

    // Honor the documented initial values of the registers. Don't touch the
    // segment registers as those have been set when enabling the requested cpu
    // mode above.
    State::Registers regs(getRegisters());
    regs.rax = 0;  regs.rbx = 0;  regs.rcx = 0;  regs.rdx = 0;
    regs.rdi = 0;  regs.rsi = 0;  regs.rsp = 0;  regs.rbp = 0;
    regs.r8  = 0;  regs.r9  = 0;  regs.r10 = 0;  regs.r11 = 0;
    regs.r12 = 0;  regs.r13 = 0;  regs.r14 = 0;  regs.r15 = 0;
    regs.rflags = initialRflags;
    // rip will be set when loading code.
    regs.gdt.base = 0; regs.gdt.limit = 0;
    regs.idt.base = 0; regs.idt.limit = 0;
    setRegisters(regs);
}

void Vm::enableCpuMode(kvm_sregs& sregs, CpuMode const mode) {
    if (mode == CpuMode::RealMode) {
        // RealMode does not need to set anything. Assuming that by default KVM
        // starts in real mode.
        return;
    }

    // Both protected mode and long mode require having the PE bit (bit 0) set
    // in CR0.
    // For protected mode that's all we need, since we are not enabling paging.
    sregs.cr0 |= 1;

    if (mode == CpuMode::LongMode) {
        // 64-bit is a bit more involved. We need to setup multiple control
        // registers as well as the EFER MSR and setup paging as this is
        // required in 64-bit.

        // Setup paging.
        // Load PML4 into CR3.
        u64 const pml4Offset(createIdentityMapping());
        sregs.cr3 = pml4Offset & 0xFFFFFFFFFFFFF000ULL;

        // Page table is ready now setup the control registers as they would be
        // in long mode.
        // Enable Physical Address Extension (PAE) in Cr4 (bit 5) , mandatory
        // for 64-bits.
        sregs.cr4 |= (1 << 5);

        // Set LME (bit 8) and LMA (bit 10) bits in EFER.
        sregs.efer |= ((1 << 8) | (1 << 10));

        // CR0: Enable paging bit (PG, bit 31). PE has been enabled already
        // above.
        assert(sregs.cr0 & 1);
        sregs.cr0 |= (1UL << 31);
    }
}

u64 Vm::createIdentityMapping() {
    // Since we are mapping from the host's user space, we need to keep track of
    // two addresses per table in the page table structure:
    //  - The host address, that is where the table has been mmap'ed on the
    //  host. This is the address we use to write into the table from the host.
    //  - The guest physical address, this is the physical address at which the
    //  table reside on the guest and the address that we need to write into
    //  page table entries.
    // This maps a guest physical offset of a table to the host mmap address
    // used to write into that table.
    std::map<u64, u64*> guestToHost;

    // Compute the index of the table entry mapping the physical address at a
    // certain level.
    // This is an helper function for map() below.
    auto const getIndexAtLevel([&](u64 const pAddr, u8 const level) {
        // 64-bit linear addresses are mapped as follows:
        //  47        39 38         30 29         21 20         12 11         0
        // |PML4        |Directory Ptr|Directory    |Table        |Offset     |
        u64 const shift(12 + (level - 1) * 9);
        assert(shift <= 39);
        u64 const mask(0x1FF);
        return (pAddr >> shift) & mask;
    });

    // Check if a page table entry is marked as present.
    // This is an helper function for map() below.
    auto const isPresent([&](u64 const entry) {
        return entry & 0x1;
    });

    // Compute a table entry to point to the next level's offset.
    // This is an helper function for map() below.
    auto const computeEntry([&](u64 const nextLevelOffset) {
        // Set present bit, and write permission.
        return (nextLevelOffset & 0xFFFFFFFFFFFFF000ULL) | 0x3;
    });

    // Helper lambda to ID map a physical frame on the guest. This function will
    // recurse into lower level table to map the frame.
    // @param pAddr: The physical address to map, must be PAGE_SIZE aligned.
    // @param table: The host-address of the current table to be modified to
    // contain the mapping.
    // @param level: The level of `table`.
    std::function<void(u64, u64*, u8)> map(
        [&](u64 const pAddr, u64 * const table, u8 const level) {
        assert(!(pAddr % PAGE_SIZE));
        if (!level) {
            // We reached the last level, nothing to do.
            return;
        }

        u64 const index(getIndexAtLevel(pAddr, level));
        if (level == 1) {
            // Reached last level, map the frame.
            table[index] = computeEntry(pAddr);
        } else {
            u64 const currEntry(table[index]);
            if (!isPresent(currEntry)) {
                // The entry is marked non-present, e.g. there is no table in
                // the lower-level. Allocate the next table, map it to the
                // current table, then recurse into it.
                std::pair<void*, u64> const alloc(addPhysicalMemory(1));
                // The offset of the allocated table in the guest's physical
                // memory. This is the offset that we need to write in the
                // current table's entry.
                u64 const guestOffset(std::get<1>(alloc));
                assert(!(guestOffset % PAGE_SIZE));
                // The address at which the table is mapped to the host's user
                // memory. This is the address that we need to pass to the
                // recursive call to map() so we can read and write the table
                // from the host.
                u64 * const host(reinterpret_cast<u64*>(std::get<0>(alloc)));
                // Add the mapping guestOffset -> host so that future calls to
                // map() know how to read/write the allocated table.
                guestToHost[guestOffset] = host;
                // Update the current entry to point to the new table.
                table[index] = computeEntry(guestOffset);
                // Continue the mapping into the lower level.
                map(pAddr, host, level - 1);
            } else {
                // The entry is present, there is already a table at level - 1,
                // continue the mapping into the lower level.
                u64 const guestOffset(currEntry & 0xFFFFFFFFFFFFF000ULL);
                // The level-1 table must have been added to the map when
                // allocated. If the key is not present in the map then we have
                // no way to know at which address we should read/write from/to
                // on the host to manipulate it.
                assert(guestToHost.contains(guestOffset));
                map(pAddr, guestToHost[guestOffset], level - 1);
            }
        }
    });

    // Allocate a PML4, the root of the page table structure.
    // Note: We cannot allocate the page tables as read-only memory because the
    // vcpu writes the dirty and accessed bits in the page table entries.
    std::pair<void*, u64> const pml4Alloc(addPhysicalMemory(1));
    // Host-side address of the PML4.
    u64 * const hostPml4(reinterpret_cast<u64*>(std::get<0>(pml4Alloc)));
    // Guest-side offset of the PML4.
    u64 const guestPml4(std::get<1>(pml4Alloc));
    assert(!(guestPml4 % PAGE_SIZE));

    // Map each physical frame.
    for (u64 offset(0); offset < physicalMemorySize; offset += PAGE_SIZE) {
        map(offset, hostPml4, 4);
    }
    return guestPml4;
}

std::pair<void*, u64> Vm::addPhysicalMemory(u32 const numPages) {
    if (memorySlots.size() == Util::Kvm::getMaxMemSlots(vmFd)) {
        // We are already using as many slots as we can. Note this will most
        // likely never happen as KVM reports 32k slots on most machines.
        throw KvmError("Reached max. number of memory slots on VM", 0);
    }

    u64 const allocSize(numPages * PAGE_SIZE);

    // Mmap some anonymous memory for the requested size.
    int const prot(PROT_READ | PROT_WRITE);
    int const flags(MAP_PRIVATE | MAP_ANONYMOUS);
    void * const userspace(::mmap(NULL, allocSize, prot, flags, -1, 0));
    if (memory == MAP_FAILED) {
        throw MmapError("Failed to mmap memory for guest", errno);
    }

    u32 const numSlots(memorySlots.size());
    kvm_userspace_memory_region const lastSlot(
        (!!numSlots) ? memorySlots[numSlots-1] : kvm_userspace_memory_region{});

    // The physical address of the new slot starts immediately after the last
    // slot.
    u64 const phyAddr(lastSlot.guest_phys_addr + lastSlot.memory_size);

    // Then map the memory to the guest.
    kvm_userspace_memory_region const kvmMap({
        .slot = static_cast<u32>(memorySlots.size()),
        .flags = 0,
        .guest_phys_addr = phyAddr,
        .memory_size = allocSize,
        .userspace_addr = reinterpret_cast<u64>(userspace),
    });
    if (::ioctl(vmFd, KVM_SET_USER_MEMORY_REGION, &kvmMap) == -1) {
        throw KvmError("Failed to map memory to guest", errno);
    }
    // Adding the slot was successful, add it to the vector.
    memorySlots.push_back(kvmMap);
    return std::pair<void*, u64>(userspace, phyAddr);
}
}
