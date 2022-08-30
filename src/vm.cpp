#include <x86lab/vm.hpp>

namespace X86Lab {

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
    usedMemorySlots(0),
    physicalMemorySize(memorySize * PAGE_SIZE),
    memory(addPhysicalMemory(0x0, physicalMemorySize)),
    currState(OperatingState::NoCodeLoaded),
    isRealMode(startMode == CpuMode::RealMode)
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

    // One tricky thing that we need to deal with is the value of CS after INIT
    // or RESET which is set to 0xF000. Additionally, the hidden base of CS is
    // set to 0xFFFF0000 and EIP to 0xFFF0, this is done so that the first
    // instruction executed by the cpu is at 0xFFFFFFF0.
    // KVM obviously does this. However, since we are doing everything
    // ourselves, we have two options:
    //  1. Set a jmp @ 0xFFFFFFF0 to jump to the first instruction.
    //  2. Overwrite the hidden base and CS to 0.
    // Option 1 would mean having to map another page in the guest physical
    // memory (namely at 0xFFFFF000) and that the first call to step() does not
    // actually run the first instruction. Option 2 is easier to implement.
    kvm_sregs sregs(Util::Kvm::getSRegs(vcpuFd));
    sregs.cs.base = 0x0;
    // Per Intel's documentation, if the guest is running in real-mode then the
    // limit MUST be 0xFFFF. See "26.3.1.2 Checks on Guest Segment Registers" in
    // Vol. 3C.
    sregs.cs.limit = 0xFFFF;
    sregs.cs.selector = 0;
    Util::Kvm::setSRegs(vcpuFd, sregs);

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

    State::Registers const regFile({
        .rax = regs.rax,
        .rbx = regs.rbx,
        .rcx = regs.rcx,
        .rdx = regs.rdx,
        .rdi = regs.rdi,
        .rsi = regs.rsi,
        .rsp = regs.rsp,
        .rbp = regs.rbp,
        .r8  = regs.r8,
        .r9  = regs.r9,
        .r10 = regs.r10,
        .r11 = regs.r11,
        .r12 = regs.r12,
        .r13 = regs.r13,
        .r14 = regs.r14,
        .r15 = regs.r15,

        .rflags = regs.rflags,
        .rip = regs.rip,

        .cs = sregs.cs.selector,
        .ds = sregs.ds.selector,
        .es = sregs.es.selector,
        .fs = sregs.fs.selector,
        .gs = sregs.gs.selector,
        .ss = sregs.ss.selector,

        .cr0 = sregs.cr0,
        .cr2 = sregs.cr2,
        .cr3 = sregs.cr3,
        .cr4 = sregs.cr4,
        .cr8 = sregs.cr8,

        .efer = sregs.efer,

        .idt = {
            .base = sregs.idt.base,
            .limit = sregs.idt.limit,
        },
        .gdt = {
            .base = sregs.gdt.base,
            .limit = sregs.gdt.limit,
        },
    });
    return regFile;
}

void Vm::setRegisters(State::Registers const& registerValues) {
    kvm_regs const regs({
        .rax    = registerValues.rax,
        .rbx    = registerValues.rbx,
        .rcx    = registerValues.rcx,
        .rdx    = registerValues.rdx,
        .rsi    = registerValues.rsi,
        .rdi    = registerValues.rdi,
        .rsp    = registerValues.rsp,
        .rbp    = registerValues.rbp,
        .r8     = registerValues.r8,
        .r9     = registerValues.r9,
        .r10    = registerValues.r10,
        .r11    = registerValues.r11,
        .r12    = registerValues.r12,
        .r13    = registerValues.r13,
        .r14    = registerValues.r14,
        .r15    = registerValues.r15,
        .rip    = registerValues.rip,
        .rflags = registerValues.rflags,
    });
    Util::Kvm::setRegs(vcpuFd, regs);

    // State::Registers doesn't quite contain all the values that kvm_sregs has.
    // Hence for those missing values, we read the current kvm_sregs to re-use
    // them in the call to KVM_SET_SREGS.
    kvm_sregs sregs(Util::Kvm::getSRegs(vcpuFd));

    sregs.cs.selector = registerValues.cs;
    sregs.ds.selector = registerValues.ds;
    sregs.es.selector = registerValues.es;
    sregs.fs.selector = registerValues.fs;
    sregs.gs.selector = registerValues.gs;
    sregs.ss.selector = registerValues.ss;

    if (isRealMode) {
        // When operating in real-mode it seems that we need to manually set the
        // hidden base and limit of each segment register.
        sregs.cs.base = sregs.cs.selector << 4;
        sregs.cs.limit = 0xFFFF;
        sregs.ds.base = sregs.ds.selector << 4;
        sregs.ds.limit = 0xFFFF;
        sregs.es.base = sregs.es.selector << 4;
        sregs.es.limit = 0xFFFF;
        sregs.fs.base = sregs.fs.selector << 4;
        sregs.fs.limit = 0xFFFF;
        sregs.gs.base = sregs.gs.selector << 4;
        sregs.gs.limit = 0xFFFF;
        sregs.ss.base = sregs.ss.selector << 4;
        sregs.ss.limit = 0xFFFF;
    }

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

void Vm::setRegistersInitialValue(CpuMode const mode) {
    // Now setup the requested mode. If startMode == CpuMode::RealMode then we
    // have nothing to do as this is the default mode in KVM.
    if (mode == CpuMode::ProtectedMode) {
        enableProtectedMode();
    } else if (mode == CpuMode::LongMode) {
        enable64BitsMode();
    }

    // Honor the documented initial values of the registers. Don't touch the
    // segment registers as those have been set when enabling the requested cpu
    // mode above.
    State::Registers regs(getRegisters());
    regs.rax = 0;  regs.rbx = 0;  regs.rcx = 0;  regs.rdx = 0;
    regs.rdi = 0;  regs.rsi = 0;  regs.rsp = 0;  regs.rbp = 0;
    regs.r8  = 0;  regs.r9  = 0;  regs.r10 = 0;  regs.r11 = 0;
    regs.r12 = 0;  regs.r13 = 0;  regs.r14 = 0;  regs.r15 = 0;
    regs.rflags = 0x2;
    // rip will be set when loading code.
    regs.gdt.base = 0; regs.gdt.limit = 0;
    regs.idt.base = 0; regs.idt.limit = 0;
    setRegisters(regs);
}

void Vm::enableProtectedMode() {
    kvm_sregs sregs(Util::Kvm::getSRegs(vcpuFd));

    // Enable protected mode.
    sregs.cr0 |= 1;

    // Set the hidden parts of the segment registers.
    // The selectors are not exactly required, because the CPU uses the hidden
    // parts anyway. We set it here so that it is non-zero when showing register
    // values.

    // Flat code segment.
    sregs.cs.selector = 0x0;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFF;
    sregs.cs.type = 10;
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;
    sregs.cs.db = 1;
    sregs.cs.s = 1;
    sregs.cs.l = 0;
    sregs.cs.g = 1;
    sregs.cs.avl = 0;
    // Per Intel's documentation unusable must be 0 for the access flags to be
    // loaded in the segment register when entering the VM.
    sregs.cs.unusable = 0;

    // Flat R/W data and stack segments.
    sregs.ds.selector = 0x10;
    sregs.ds.base = 0;
    sregs.ds.limit = 0xFFFFF;
    sregs.ds.type = 2;
    sregs.ds.present = 1;
    sregs.ds.dpl = 0;
    sregs.ds.db = 1;
    sregs.ds.s = 1;
    sregs.ds.l = 0;
    sregs.ds.g = 1;
    sregs.ds.avl = 0;
    sregs.ds.unusable = 0;

    sregs.es = sregs.ds;
    sregs.fs = sregs.ds;
    sregs.gs = sregs.ds;
    sregs.ss = sregs.ds;

    Util::Kvm::setSRegs(vcpuFd, sregs);
}

void Vm::enable64BitsMode() {
    // First off we need to setup a set of page tables, as paging is mandatory
    // when in Long Mode.
    // We can allocate the pages used for the PML4 and PDPT after the requested
    // memory.
    
    // These are guaranteed to be page aligned since the physical memory size is
    // a multiple of page size.
    u64 const pml4Offset(physicalMemorySize);
    u64 const pdptOffset(pml4Offset + PAGE_SIZE);
    assert(!(pml4Offset % PAGE_SIZE));

    // Userspace addresses for the PML4 and PDPT.
    void * const pml4Addr(addPhysicalMemory(pml4Offset, PAGE_SIZE));
    void * const pdptAddr(addPhysicalMemory(pdptOffset, PAGE_SIZE));

    // FIXME: As of now we only R/W Identity-Map the first 1GiB. Because we are
    // lazy we use huge 1GiB pages. Everything is in supervisor mode.
    // Entry 0: Pointing to the allocated PDPT with R/W permission.
    *reinterpret_cast<u64*>(pml4Addr) = (pdptOffset & 0xFFFFFFFFFFFFF000ULL) | 3;
    // Entry 0: Pointing to 1GiB frame starting at offset 0, R/W permissions.
    // Bit 7 indicates that this is a 1GiB page.
    *reinterpret_cast<u64*>(pdptAddr) = (1 << 7) | 3;

    // Page table is ready now setup the registers as they would be in long
    // mode.
    kvm_sregs sregs(Util::Kvm::getSRegs(vcpuFd));

    // Enable PAE, mandatory for 64-bits.
    sregs.cr4 = 0x20;

    // Load PML4 into CR3.
    sregs.cr3 = pml4Offset & 0xFFFFFFFFFFFFF000ULL;

    // Set LME and LMA bits to 1 in EFER.
    sregs.efer = 0x500;

    // CR0: Enable PG and PE bits.
    // Enable PG bit in CR0. FIXME: Remove the Cache Disable and Not
    // Write-through bits.
    // Note: Per Intel's documentation if PG is 1 then PE (bit 0) must be 1 as
    // well. Otherwise VMX entry fails.
    sregs.cr0 = 0xe0000011;

    // Setup segment registers' hidden parts.
    sregs.cs.selector = 0;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.type = 0xa;
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;
    sregs.cs.db = 0;
    sregs.cs.s = 1;
    sregs.cs.l = 1;
    sregs.cs.g = 1;
    sregs.cs.avl = 0;
    // Per Intel's documentation unusable must be 0 for the access flags to be
    // loaded in the segment register when entering the VM.
    sregs.cs.unusable = 0;

    // Flat R/W data and stack segments.
    sregs.ds.selector = 0;
    sregs.ds.base = 0;
    sregs.ds.limit = 0xFFFFFFFF;
    sregs.ds.type = 2;
    sregs.ds.present = 1;
    sregs.ds.dpl = 0;
    sregs.ds.db = 1;
    sregs.ds.s = 1;
    sregs.ds.l = 0;
    sregs.ds.g = 1;
    sregs.ds.avl = 0;
    sregs.ds.unusable = 0;

    sregs.es = sregs.ds;
    sregs.fs = sregs.ds;
    sregs.gs = sregs.ds;
    sregs.ss = sregs.ds;

    Util::Kvm::setSRegs(vcpuFd, sregs);
}

void* Vm::addPhysicalMemory(u64 const offset, size_t const size) {
    assert(!(size % PAGE_SIZE));

    // Mmap some anonymous memory for the requested size.
    int const prot(PROT_READ | PROT_WRITE);
    int const flags(MAP_PRIVATE | MAP_ANONYMOUS);
    void * const userspace(::mmap(NULL, size, prot, flags, -1, 0));
    if (memory == MAP_FAILED) {
        throw MmapError("Failed to mmap memory for guest", errno);
    }

    // Then map the memory to the guest.
    kvm_userspace_memory_region const kvmMap({
        .slot = usedMemorySlots,
        .flags = 0,
        .guest_phys_addr = offset,
        .memory_size = size,
        .userspace_addr = reinterpret_cast<u64>(userspace),
    });
    if (::ioctl(vmFd, KVM_SET_USER_MEMORY_REGION, &kvmMap) == -1) {
        throw KvmError("Failed to map memory to guest", errno);
    }
    usedMemorySlots++;
    return userspace;
}
}
