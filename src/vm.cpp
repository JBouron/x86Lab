#include <x86lab/vm.hpp>
#include <functional>
#include <map>

namespace X86Lab {

Vm::State::Registers::Registers() :
    Vm::State::Registers::Registers({}, {}, Util::Kvm::XSaveArea()) {}

Vm::State::Registers::Registers(kvm_regs const& regs,
                                kvm_sregs const& sregs,
                                Util::Kvm::XSaveArea const& xsave) :
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
    gdt({.base = sregs.gdt.base, .limit = sregs.gdt.limit}),

    mxcsr(xsave.mxcsr) {
    for (u8 i(0); i < NumMmxRegs; ++i) {
        mmx[i] = xsave.mmx[i];
    }

    for (u8 i(0); i < NumXmmRegs; ++i) {
        xmm[i] = vec128(xsave.zmm[i].elem<u64>(1), xsave.zmm[i].elem<u64>(0));
    }

    for (u8 i(0); i < NumYmmRegs; ++i) {
        ymm[i] = vec256(xsave.zmm[i].elem<u64>(3),
                        xsave.zmm[i].elem<u64>(2),
                        xsave.zmm[i].elem<u64>(1),
                        xsave.zmm[i].elem<u64>(0));
    }

    for (u8 i(0); i < NumZmmRegs; ++i) {
        zmm[i] = xsave.zmm[i];
    }
    for (u8 i(0); i < NumKRegs; ++i) {
        k[i] = xsave.k[i];
    }
}

Vm::State::Registers const& Vm::State::registers() const {
    return m_regs;
}

Vm::State::Memory const& Vm::State::memory() const {
    return m_mem;
}

Vm::State::State(Registers const& regs, Memory && mem) :
    m_regs(regs),
    m_mem(std::move(mem)) {}

// Compute ceil(a / b);
static u64 ceil(u64 const a, u64 const b) {
    return a / b + ((a % b == 0) ? 0 : 1);
}

static u64 roundUp(u64 const val, u64 const multiple) {
    return multiple * ceil(val, multiple);
}


Vm::Vm(CpuMode const startMode, u64 const memorySize) :
    m_vmFd(Util::Kvm::createVm()),
    m_vcpuFd(Util::Kvm::createVcpu(m_vmFd)),
    m_kvmRun(Util::Kvm::getVcpuRunStruct(m_vcpuFd)),
    m_currState(OperatingState::NoCodeLoaded) {
    // VM and VCPU are created in the initialization list. However we do need to
    // add memory. Compute the number of pages that should be allocated for the
    // guest's physical memory. The number of pages is the requested physical
    // memory size (rounded-up to multiple of PAGE_SIZE) + any page required for
    // the vcpu's data structures, this includes page tables when starting in
    // long mode.
    m_physicalMemorySize = roundUp(memorySize, PAGE_SIZE);
    // Save where the extra physical memory is added. Any cpu data structure
    // will start at m_extraMemoryOffset.
    m_extraMemoryOffset = m_physicalMemorySize;
    if (startMode == CpuMode::LongMode) {
        // All physical memory is continuous so we simply need to divide by each
        // page table level coverage.
        u64 const numFrames(m_physicalMemorySize / PAGE_SIZE);
        u64 const numPageTables(ceil(numFrames, 512));
        u64 const numPageDirs(ceil(numPageTables, 512));
        u64 const numPageDirPtrs(ceil(numPageDirs, 512));
        // Add space for all the tables that will need to be allocated. The +1
        // is for the PML4/root table which must be allocated.
        m_physicalMemorySize +=
            (1 + numPageTables + numPageDirs + numPageDirPtrs) * PAGE_SIZE;
    }

    // Allocate the guest's physical memory.
    m_memory = createPhysicalMemory(m_physicalMemorySize);

    // We require some Kvm extension to implement some of the features of this
    // class. Check that all extension are supported on the host's KVM API now
    // instead of doing it at every corresponding KVM_* ioctl later.
    Util::Kvm::requiresExension(m_vmFd, KVM_CAP_X86_MSR_FILTER);
    Util::Kvm::requiresExension(m_vmFd, KVM_CAP_NR_MEMSLOTS);
    Util::Kvm::requiresExension(m_vmFd, KVM_CAP_XSAVE);
    Util::Kvm::requiresExension(m_vmFd, KVM_CAP_XCRS);

    // Disable any MSR access filtering. KVM's doc indicate that if this is not
    // done then the default behaviour is used. However it's not really clear if
    // the default behaviour allows access to MSRs or not. Hence disable it here
    // completely.
    Util::Kvm::disableMsrFiltering(m_vmFd);

    // Setup access to CPUID information. We don't want to "hide" anything from
    // the guest, having CPUID instruction available can always be useful.
    Util::Kvm::setupCpuid(m_vcpuFd);

    // Setup the registers depending on the requested mode and honor the initial
    // value of registers documented in .hpp.
    setRegistersInitialValue(startMode);
}

Vm::~Vm() {
    // FIXME: The kvm_run structure is an mmap on the m_vcpuFd. Unmap it before
    // closing the m_vcpuFd. This could be solved using a custom type returned
    // by getVcpuRunStruct, with RAII doing the munmap in its destructor.
    if (::close(m_vcpuFd) == -1) {
        std::perror("Cannot close KVM Vcpu file descriptor:");
    } else if (::close(m_vmFd) == -1) {
        std::perror("Cannot close KVM VM file descriptor:");
    }

    // Un-map all physical memory.
    if (::munmap(m_memory, m_physicalMemorySize) == -1) {
        // Virtually impossible if we are passing the output of mmap here.
        std::perror("Failed to unmap memory region:");
    }
}

void Vm::loadCode(Code const& code) {
    // For now the code is always loaded at address 0x0.
    std::memcpy(m_memory, code.machineCode(), code.size());

    State::Registers regs(getRegisters());
    // Set RIP to first instruction.
    regs.rip = 0x0;
    // Set RSP to point after the end of physical memory that is usable (meaning
    // that we don't use the extra allocated memory as it potentially contains
    // sensitive data like page tables).
    regs.rsp = m_extraMemoryOffset;
    setRegisters(regs);

    // The KVM is now runnable.
    m_currState = OperatingState::Runnable;
}

std::unique_ptr<Vm::State> Vm::getState() const {
    State::Registers const regs(getRegisters());
    Vm::State::Memory mem({
        .data = std::unique_ptr<u8[]>(new u8[m_physicalMemorySize]),
        .size = m_physicalMemorySize,
    });
    std::memcpy(mem.data.get(), m_memory, m_physicalMemorySize);
    return std::unique_ptr<Vm::State>(new Vm::State(regs, std::move(mem)));
}

Vm::State::Registers Vm::getRegisters() const {
    kvm_regs const regs(Util::Kvm::getRegs(m_vcpuFd));
    kvm_sregs const sregs(Util::Kvm::getSRegs(m_vcpuFd));
    std::unique_ptr<Util::Kvm::XSaveArea> const xsave(
        Util::Kvm::getXSave(m_vcpuFd));
    return State::Registers(regs, sregs, *xsave);
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
    Util::Kvm::setRegs(m_vcpuFd, regs);

    // State::Registers doesn't quite contain all the values that kvm_sregs has.
    // Hence for those missing values, we read the current kvm_sregs to re-use
    // them in the call to KVM_SET_SREGS.
    kvm_sregs sregs(Util::Kvm::getSRegs(m_vcpuFd));

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

    Util::Kvm::setSRegs(m_vcpuFd, sregs);


    std::unique_ptr<Util::Kvm::XSaveArea> xsave(Util::Kvm::getXSave(m_vcpuFd));
    // Set the MMX registers.
    for (u8 i(0); i < State::Registers::NumMmxRegs; ++i) {
        xsave->mmx[i] = registerValues.mmx[i];
    }

    // MXCSR_MASK indicates the writable bits in MXCSR.
    xsave->mxcsr = registerValues.mxcsr & xsave->mxcsrMask;

    // ZMM registers. This also sets the YMM and XMM registers.
    for (u8 i(0); i < State::Registers::NumZmmRegs; ++i) {
        xsave->zmm[i] = registerValues.zmm[i];
    }
    for (u8 i(0); i < State::Registers::NumKRegs; ++i) {
        xsave->k[i] = registerValues.k[i];
    }

    Util::Kvm::setXSave(m_vcpuFd, *xsave);
}

Vm::OperatingState Vm::operatingState() const {
    return m_currState;
}

Vm::OperatingState Vm::step() {
    // Enable debug on guest vcpu in order to be able to do single
    // stepping.
    // The documentation is sparse on this, but it seems that single
    // stepping gets disabled everytime registers are set using
    // KVM_SET_REGS. Hence do it right before the call to KVM_RUN.
    kvm_guest_debug dbg;
    dbg.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
    if (::ioctl(m_vcpuFd, KVM_SET_GUEST_DEBUG, &dbg) == -1) {
        m_currState = OperatingState::SingleStepError;
        throw KvmError("Cannot set guest debug", errno);
    }

    if (::ioctl(m_vcpuFd, KVM_RUN, NULL) != 0) {
        m_currState = OperatingState::SingleStepError;
        throw KvmError("Cannot run VM", errno);
    }

    if (m_kvmRun.exit_reason == KVM_EXIT_DEBUG) {
        // The execution stopped after one step, we are still runnable.
        m_currState = OperatingState::Runnable;
    } else if (m_kvmRun.exit_reason == KVM_EXIT_SHUTDOWN) {
        // Execution stopped the host. This is most likely a triple-fault hehe.
        m_currState = OperatingState::Shutdown;
    } else if (m_kvmRun.exit_reason == KVM_EXIT_HLT) {
        // The single step executed a halt instruction.
        m_currState = OperatingState::Halted;
    } else {
        // For now consider everything else as an error.
        m_currState = OperatingState::SingleStepError;
    }
    return m_currState;
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
    seg.db = (mode == Vm::CpuMode::ProtectedMode);

    // Must be 1.
    seg.s = 1;

    // Only set if the current mode is 64-bit. If L is set then DB must be
    // unset.
    seg.l = (mode == Vm::CpuMode::LongMode);

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
    kvm_sregs sregs(Util::Kvm::getSRegs(m_vcpuFd));

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

    Util::Kvm::setSRegs(m_vcpuFd, sregs);

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
    sregs.cr0 |= 1;

    // Prepare MMX in both Protected and Long modes. Set MP to 1, EM to 0 and TS
    // to 0 as recommended by Intel's docs.
    if (Util::Extension::hasMmx()) {
        sregs.cr0 |= (1 << 1);
        sregs.cr0 &= (~((1 << 2) | (1 << 3)));
    }

    // Setup control registers for SSE.
    if (Util::Extension::hasSse()) {
        // Technically we should provide an exception handler for #XM, as
        // indicated by SSE's doc. However, we can't do much in case this
        // happens, hence let the VM triple fault in that case.
        // OSFXSR bit.
        sregs.cr4 |= (1 << 9);
        // OSXMMEXECPT bit
        sregs.cr4 |= (1 << 10);
        // CR0.EM is already cleared and CR0.MP is already set.
    }

    // Setup AVX.
    if (Util::Extension::hasAvx()) {
        // Set CR4.OSXSAVE[bit18] to enable AVX state saving using XSAVE/XRSTOR.
        sregs.cr4 |= (1 << 18);
        // Set bits 1 and 2 in XCR0 (bit 1 must always be set) to enable AVX
        // state in XSAVE.
        u64 const xcr0(Util::Kvm::getXcr0(m_vcpuFd));
        Util::Kvm::setXcr0(m_vcpuFd, xcr0 | 0x7);
    }

    // Setup AVX512.
    if (Util::Extension::hasAvx512()) {
        // Enable AVX-512 execution and save/restore through XSAVE in XCR0.
        u64 const xcr0(Util::Kvm::getXcr0(m_vcpuFd));
        // Set bits:
        //  - XCR0.opmask (bit 5)
        //  - XCR0.ZMM_Hi256 (bit 6)
        //  - XCR0.Hi16_ZMM (bit 7)
        Util::Kvm::setXcr0(m_vcpuFd, xcr0 | (1 << 5) | (1 << 6) | (1 << 7));
    }

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

    // The number of tables allocated, used by allocTable() to keep track of
    // where the next table should be allocated.
    u32 numTablesAlloc(0);

    // Allocate a table in the extra physical memory.
    // @return: A pair <void*, u64>. The first element is the virtual address
    // in the host's address space pointing to the table, this address is used
    // to read/write the table. The second element is the physical offset of the
    // allocated table in the guest's physical memory.
    auto const allocTable([&]() {
        u64 const allocOffset(m_extraMemoryOffset + numTablesAlloc * PAGE_SIZE);
        // To make it simple each allocation moves the m_extraMemoryOffset to
        // the next free page/frame. This is fine because the extra allocated
        // physical memory contains the exact amount of pages/tables we need and
        // tables are never deallocated.
        // The assert is here to make sure we computed the extra memory size
        // correctly and we are not trying to allocate outside the guest's
        // addressable physical memory.
        assert(allocOffset < m_physicalMemorySize);
        void * const hostAddr(static_cast<u8*>(m_memory) + allocOffset);
        numTablesAlloc++;
        return std::make_pair(hostAddr, allocOffset);
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
                std::pair<void*, u64> const alloc(allocTable());
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
    std::pair<void*, u64> const pml4Alloc(allocTable());
    // Host-side address of the PML4.
    u64 * const hostPml4(reinterpret_cast<u64*>(std::get<0>(pml4Alloc)));
    // Guest-side offset of the PML4.
    u64 const guestPml4(std::get<1>(pml4Alloc));
    assert(!(guestPml4 % PAGE_SIZE));

    // Map each physical frame. Note: For now we only ID map the amount of
    // memory that was requested when creating the VM. We do not ID map the
    // extra physical memory allocated for page tables. The reason: ID mapping
    // the page tables themselves might require too much memory if the physical
    // address space is too big.
    for (u64 offset(0); offset < m_extraMemoryOffset; offset += PAGE_SIZE) {
        map(offset, hostPml4, 4);
    }
    return guestPml4;
}

void *Vm::createPhysicalMemory(u64 const memorySize) {
    // Mmap some anonymous memory for the requested size.
    int const prot(PROT_READ | PROT_WRITE);
    int const flags(MAP_PRIVATE | MAP_ANONYMOUS);
    void * const userspaceAddr(::mmap(NULL, memorySize, prot, flags, -1, 0));
    if (userspaceAddr == MAP_FAILED) {
        throw MmapError("Failed to mmap memory for guest", errno);
    }

    // Zero the allocated memory area.
    std::memset(userspaceAddr, 0x0, memorySize);

    // Then map the memory to the guest.
    kvm_userspace_memory_region const kvmMap({
        // Only using a single slot. It does not matter much which one we
        // choose.
        .slot = 0,
        .flags = 0,
        .guest_phys_addr = 0,
        .memory_size = memorySize,
        .userspace_addr = reinterpret_cast<u64>(userspaceAddr),
    });
    if (::ioctl(m_vmFd, KVM_SET_USER_MEMORY_REGION, &kvmMap) == -1) {
        throw KvmError("Failed to map memory to guest", errno);
    }
    return userspaceAddr;
}

}
