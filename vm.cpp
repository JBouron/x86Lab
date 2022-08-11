#include "vm.hpp"

namespace X86Lab {

Vm::Vm(u64 const memorySize) {
    // Open handle to kvm subsystem.
    kvmHandle = ::open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvmHandle == -1) {
        throw KvmError("Cannot open /dev/kvm", errno);
    }

    // Actually create VM.
    vmFd = ::ioctl(kvmHandle, KVM_CREATE_VM, 0);
    if (vmFd == -1) {
        throw KvmError("Cannot create VM", errno);
    }

    // Add vCpu to this VM.
    vcpuFd = ::ioctl(vmFd, KVM_CREATE_VCPU, 0);
    if (vcpuFd == -1) {
        throw KvmError("Cannot create VCPU", errno);
    }

    // Map the kvm_run structure to memory.
    int const vcpuRunSize(::ioctl(kvmHandle, KVM_GET_VCPU_MMAP_SIZE, NULL));
    if (vcpuRunSize == -1) {
        throw KvmError("Cannot get kvm_run structure size", errno);
    }
    kvmRun = static_cast<kvm_run*>(::mmap(NULL,
                                   vcpuRunSize,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE,
                                   vcpuFd,
                                   0));
    if (kvmRun == MAP_FAILED) {
        throw MmapError("Failed to mmap kvm_run structure", errno);
    }

    // Create memory for this VM.
    usedMemorySlots = 0;
    physicalMemorySize = memorySize * PAGE_SIZE;
    memory = addPhysicalMemory(0x0, physicalMemorySize);

    // Disable any MSR access filtering.
    kvm_msr_filter msrFilter;
    msrFilter.flags = 0;
    for (size_t i(0); i < KVM_MSR_FILTER_MAX_RANGES; ++i) {
        msrFilter.ranges[i].nmsrs = 0;
    }
    if (::ioctl(vmFd, KVM_X86_SET_MSR_FILTER, &msrFilter) == -1) {
        throw KvmError("Failed to allow MSR access", errno);
    }

    kvm_enable_cap kvmCap;
    kvmCap.cap = KVM_CAP_X86_USER_SPACE_MSR;
    kvmCap.flags = 0;
    kvmCap.args[0] = KVM_MSR_EXIT_REASON_UNKNOWN | KVM_MSR_EXIT_REASON_FILTER;
    for (size_t i(1); i < 4; ++i) {
        kvmCap.args[i] = 0;
    }
    if (::ioctl(vmFd, KVM_ENABLE_CAP, &kvmCap) == -1) {
        throw KvmError("Failed to set capability", errno);
    }

    // FIXME: We should somehow guess what this number should be. Maybe by
    // repeatedly calling the ioctl and increasing it every time it fails?
    size_t const nent(64);
    size_t const structSize(sizeof(kvm_cpuid2) + nent * sizeof(kvm_cpuid_entry2));
    kvm_cpuid2 * const kvmCpuid(reinterpret_cast<kvm_cpuid2*>(malloc(structSize)));
    std::memset(kvmCpuid, 0x0, structSize);
    std::cout << "kvmCpuid = " << kvmCpuid << std::endl;
    assert(!!kvmCpuid);
    kvmCpuid->nent = nent;
    if (::ioctl(kvmHandle, KVM_GET_SUPPORTED_CPUID, kvmCpuid) == -1) {
        throw KvmError("Failed to get supported CPUID", errno);
    }

    // Now set the CPUID capabilities.
    if (::ioctl(vcpuFd, KVM_SET_CPUID2, kvmCpuid) == -1) {
        throw KvmError("Failed to set supported CPUID", errno);
    }

    // The KVM is not runnable until some code is at least loaded.
    currState = State::NoCodeLoaded;
}

void Vm::loadCode(u8 const * const shellCode, u64 const shellCodeSize) {
    // For now the code is always loaded at address 0x0.
    std::memcpy(memory, shellCode, shellCodeSize);

    // Set RIP to first instruction.
    RegisterFile regs(getRegisters());
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
    kvm_sregs sregs;
    if (::ioctl(vcpuFd, KVM_GET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot get guest special registers", errno);
    }
    sregs.cs.base = 0x0;
    // Per Intel's documentation, if the guest is running in real-mode then the
    // limit MUST be 0xFFFF. See "26.3.1.2 Checks on Guest Segment Registers" in
    // Vol. 3C.
    sregs.cs.limit = 0xFFFF;
    sregs.cs.selector = 0;
    if (::ioctl(vcpuFd, KVM_SET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot set guest special registers", errno);
    }

    // The KVM is now runnable.
    currState = State::Runnable;
}

Vm::RegisterFile Vm::getRegisters() const {
    kvm_regs regs;
    if (::ioctl(vcpuFd, KVM_GET_REGS, &regs) == -1) {
        throw KvmError("Cannot get guest registers", errno);
    }
    kvm_sregs sregs;
    if (::ioctl(vcpuFd, KVM_GET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot get guest special registers", errno);
    }

    RegisterFile const regFile({
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

void Vm::setRegisters(RegisterFile const& registerValues) {
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
    if (::ioctl(vcpuFd, KVM_SET_REGS, &regs) == -1) {
        throw KvmError("Cannot set guest registers", errno);
    }

    // RegisterFile doesn't quite contain all the values that kvm_sregs has.
    // Hence for those missing values, we read the current kvm_sregs to re-use
    // them in the call to KVM_SET_SREGS.
    kvm_sregs sregs;
    if (::ioctl(vcpuFd, KVM_GET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot get guest special registers", errno);
    }

    sregs.cs.selector = registerValues.cs,
    sregs.ds.selector = registerValues.ds,
    sregs.es.selector = registerValues.es,
    sregs.fs.selector = registerValues.fs,
    sregs.gs.selector = registerValues.gs,
    sregs.ss.selector = registerValues.ss,
    sregs.cr0 = registerValues.cr0,
    sregs.cr2 = registerValues.cr2,
    sregs.cr3 = registerValues.cr3,
    sregs.cr4 = registerValues.cr4,
    sregs.cr8 = registerValues.cr8,
    sregs.efer = registerValues.efer,
    sregs.idt.base = registerValues.idt.base;
    sregs.idt.limit = registerValues.idt.limit;
    sregs.gdt.base = registerValues.gdt.base;
    sregs.gdt.limit = registerValues.gdt.limit;

    if (::ioctl(vcpuFd, KVM_SET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot set guest special registers", errno);
    }
}

void Vm::enableProtectedMode() {
    kvm_sregs sregs;
    if (::ioctl(vcpuFd, KVM_GET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot get guest special registers", errno);
    }

    // Enable protected mode.
    sregs.cr0 |= 1;

    // Set the hidden parts of the segment registers.
    // The selectors are not exactly required, because the CPU uses the hidden
    // parts anyway. We set it here so that it is non-zero when showing register
    // values.

    // Code segment. Pretend that GDT[1] is a flat code segment.
    sregs.cs.selector = 0x8;
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

    // Data and stack segments. Pretend that GDT[2] is a flat data segment with
    // R/W access flags.
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

    if (::ioctl(vcpuFd, KVM_SET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot set guest special registers", errno);
    }
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
    kvm_sregs sregs;
    if (::ioctl(vcpuFd, KVM_GET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot get guest special registers", errno);
    }

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
    sregs.cs.selector = 20;
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

    // Data and stack segments. Pretend that GDT[2] is a flat data segment with
    // R/W access flags.
    sregs.ds.selector = 0x18;
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

    if (::ioctl(vcpuFd, KVM_SET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot set guest special registers", errno);
    }
}

void* Vm::getMemory() {
    return memory;
}

Vm::State Vm::state() const {
    return currState;
}

constexpr char const* exitReasonToString[] = {
    [KVM_EXIT_UNKNOWN        ] = "KVM_EXIT_UNKNOWN",
    [KVM_EXIT_EXCEPTION      ] = "KVM_EXIT_EXCEPTION",
    [KVM_EXIT_IO             ] = "KVM_EXIT_IO",
    [KVM_EXIT_HYPERCALL      ] = "KVM_EXIT_HYPERCALL",
    [KVM_EXIT_DEBUG          ] = "KVM_EXIT_DEBUG",
    [KVM_EXIT_HLT            ] = "KVM_EXIT_HLT",
    [KVM_EXIT_MMIO           ] = "KVM_EXIT_MMIO",
    [KVM_EXIT_IRQ_WINDOW_OPEN] = "KVM_EXIT_IRQ_WINDOW_OPEN",
    [KVM_EXIT_SHUTDOWN       ] = "KVM_EXIT_SHUTDOWN",
    [KVM_EXIT_FAIL_ENTRY     ] = "KVM_EXIT_FAIL_ENTRY",
    [KVM_EXIT_INTR           ] = "KVM_EXIT_INTR",
    [KVM_EXIT_SET_TPR        ] = "KVM_EXIT_SET_TPR",
    [KVM_EXIT_TPR_ACCESS     ] = "KVM_EXIT_TPR_ACCESS",
    [KVM_EXIT_S390_SIEIC     ] = "KVM_EXIT_S390_SIEIC",
    [KVM_EXIT_S390_RESET     ] = "KVM_EXIT_S390_RESET",
    [KVM_EXIT_DCR            ] = "KVM_EXIT_DCR",
    [KVM_EXIT_NMI            ] = "KVM_EXIT_NMI",
    [KVM_EXIT_INTERNAL_ERROR ] = "KVM_EXIT_INTERNAL_ERROR",
    [KVM_EXIT_OSI            ] = "KVM_EXIT_OSI",
    [KVM_EXIT_PAPR_HCALL	 ] = "KVM_EXIT_PAPR_HCALL",
    [KVM_EXIT_S390_UCONTROL	 ] = "VM_EXIT_S390_UCONTROL",
    [KVM_EXIT_WATCHDOG       ] = "KVM_EXIT_WATCHDOG",
    [KVM_EXIT_S390_TSCH      ] = "KVM_EXIT_S390_TSCH",
    [KVM_EXIT_EPR            ] = "KVM_EXIT_EPR",
    [KVM_EXIT_SYSTEM_EVENT   ] = "KVM_EXIT_SYSTEM_EVENT",
    [KVM_EXIT_S390_STSI      ] = "KVM_EXIT_S390_STSI",
    [KVM_EXIT_IOAPIC_EOI     ] = "KVM_EXIT_IOAPIC_EOI",
    [KVM_EXIT_HYPERV         ] = "KVM_EXIT_HYPERV",
    [KVM_EXIT_ARM_NISV       ] = "KVM_EXIT_ARM_NISV",
    [KVM_EXIT_X86_RDMSR      ] = "KVM_EXIT_X86_RDMSR",
    [KVM_EXIT_X86_WRMSR      ] = "KVM_EXIT_X86_WRMSR",
    [KVM_EXIT_DIRTY_RING_FULL] = "KVM_EXIT_DIRTY_RING_FULL",
    [KVM_EXIT_AP_RESET_HOLD  ] = "KVM_EXIT_AP_RESET_HOLD",
    [KVM_EXIT_X86_BUS_LOCK   ] = "KVM_EXIT_X86_BUS_LOCK",
    [KVM_EXIT_XEN            ] = "KVM_EXIT_XEN",
    [KVM_EXIT_RISCV_SBI      ] = "KVM_EXIT_RISCV_SBI",
};

Vm::State Vm::step() {
    // Enable debug on guest vcpu in order to be able to do single
    // stepping.
    // The documentation is sparse on this, but it seems that single
    // stepping gets disabled everytime registers are set using
    // KVM_SET_REGS. Hence do it right before the call to KVM_RUN.
    kvm_guest_debug dbg;
    dbg.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
    if (::ioctl(vcpuFd, KVM_SET_GUEST_DEBUG, &dbg) == -1) {
        currState = State::SingleStepError;
        throw KvmError("Cannot set guest debug", errno);
    }

    if (::ioctl(vcpuFd, KVM_RUN, NULL) != 0) {
        currState = State::SingleStepError;
        throw KvmError("Cannot run VM", errno);
    }

    std::cout << "Exit reason = " << exitReasonToString[kvmRun->exit_reason] << std::endl;
    if (kvmRun->exit_reason != KVM_EXIT_DEBUG) {
        currState = State::SingleStepError;
    }
    return currState;
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

std::ostream& operator<<(std::ostream& os, X86Lab::Vm::RegisterFile const& r) {
    // TODO: Fix this mess.
    char buf[512];
    sprintf(buf, "-- @ rip = 0x%016x --------------------------", r.rip);
    os << buf << std::endl;
    sprintf(buf, "rax = 0x%016llx\trbx = 0x%016llx", r.rax, r.rbx);
    os << buf << std::endl;
    sprintf(buf, "rcx = 0x%016llx\trdx = 0x%016llx", r.rcx, r.rdx);
    os << buf << std::endl;
    sprintf(buf, "rdi = 0x%016llx\trsi = 0x%016llx", r.rdi, r.rsi);
    os << buf << std::endl;
    sprintf(buf, "rbp = 0x%016llx\trsp = 0x%016llx", r.rbp, r.rsp);
    os << buf << std::endl;
    sprintf(buf, "r8  = 0x%016llx\tr9  = 0x%016llx", r.r8, r.r9);
    os << buf << std::endl;
    sprintf(buf, "r10 = 0x%016llx\tr11 = 0x%016llx", r.r10, r.r11);
    os << buf << std::endl;
    sprintf(buf, "r12 = 0x%016llx\tr13 = 0x%016llx", r.r12, r.r13);
    os << buf << std::endl;
    sprintf(buf, "r14 = 0x%016llx\tr15 = 0x%016llx", r.r14, r.r15);
    os << buf << std::endl;
    sprintf(buf, "rip = 0x%016llx\trfl = 0x%016llx", r.rip, r.rflags);
    os << buf << std::endl;
    sprintf(buf, "cs = 0x%08llx\tds = 0x%08llx", r.cs, r.ds);
    os << buf << std::endl;
    sprintf(buf, "es = 0x%08llx\tfs = 0x%08llx", r.es, r.fs);
    os << buf << std::endl;
    sprintf(buf, "gs = 0x%08llx\tss = 0x%08llx", r.gs, r.ss);
    os << buf << std::endl;
    sprintf(buf, "cr0 = 0x%016llx\tcr2 = 0x%016llx", r.cr0, r.cr2);
    os << buf << std::endl;
    sprintf(buf, "cr3 = 0x%016llx\tcr4 = 0x%016llx", r.cr3, r.cr4);
    os << buf << std::endl;
    sprintf(buf, "cr8 = 0x%016llx", r.cr8);
    os << buf << std::endl;
    sprintf(buf, "idt :  base = 0x%016llx\tlimit = %08llx", r.idt.base, r.idt.limit);
    os << buf << std::endl;
    sprintf(buf, "gdt :  base = 0x%016llx\tlimit = %08llx", r.gdt.base, r.gdt.limit);
    os << buf << std::endl;
    sprintf(buf, "efer = 0x%016llx", r.efer);
    os << buf << std::endl;
    return os;
}
