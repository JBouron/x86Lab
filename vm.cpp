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
    // First mmap some memory.
    int const prot(PROT_READ | PROT_WRITE);
    int const flags(MAP_PRIVATE | MAP_ANONYMOUS);
    // Round mmap size to nearest page boundary.
    physicalMemorySize = memorySize * PAGE_SIZE;
    memory = ::mmap(NULL, physicalMemorySize, prot, flags, -1, 0);
    if (memory == MAP_FAILED) {
        throw MmapError("Failed to mmap memory for guest", errno);
    }

    // Then map the memory to the guest. The memory is always mapped to 0.
    kvm_userspace_memory_region const kvmMap = {
        .slot = 0,
        .flags = 0,
        .guest_phys_addr = 0,
        .memory_size = physicalMemorySize,
        .userspace_addr = reinterpret_cast<u64>(memory),
    };
    if (::ioctl(vmFd, KVM_SET_USER_MEMORY_REGION, &kvmMap) == -1) {
        throw KvmError("Failed to map memory to guest", errno);
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
}

std::ostream& operator<<(std::ostream& os, X86Lab::Vm::RegisterFile const& r) {
    // TODO: Fix this mess.
    char buf[512];
    sprintf(buf, "-- @ rip = 0x%016x --------------------------", r.rip);
    os << buf << std::endl;
    sprintf(buf, "rax = 0x%016x\trbx = 0x%016x", r.rax, r.rbx);
    os << buf << std::endl;
    sprintf(buf, "rcx = 0x%016x\trdx = 0x%016x", r.rcx, r.rdx);
    os << buf << std::endl;
    sprintf(buf, "rdi = 0x%016x\trsi = 0x%016x", r.rdi, r.rsi);
    os << buf << std::endl;
    sprintf(buf, "rbp = 0x%016x\trsp = 0x%016x", r.rbp, r.rsp);
    os << buf << std::endl;
    sprintf(buf, "r8  = 0x%016x\tr9  = 0x%016x", r.r8, r.r9);
    os << buf << std::endl;
    sprintf(buf, "r10 = 0x%016x\tr11 = 0x%016x", r.r10, r.r11);
    os << buf << std::endl;
    sprintf(buf, "r12 = 0x%016x\tr13 = 0x%016x", r.r12, r.r13);
    os << buf << std::endl;
    sprintf(buf, "r14 = 0x%016x\tr15 = 0x%016x", r.r14, r.r15);
    os << buf << std::endl;
    sprintf(buf, "rip = 0x%016x\trfl = 0x%016x", r.rip, r.rflags);
    os << buf << std::endl;
    sprintf(buf, "cs = 0x%08x\tds = 0x%08x", r.cs, r.ds);
    os << buf << std::endl;
    sprintf(buf, "es = 0x%08x\tfs = 0x%08x", r.es, r.fs);
    os << buf << std::endl;
    sprintf(buf, "gs = 0x%08x\tss = 0x%08x", r.gs, r.ss);
    os << buf << std::endl;
    sprintf(buf, "cr0 = 0x%016x\tcr2 = 0x%016x", r.cr0, r.cr2);
    os << buf << std::endl;
    sprintf(buf, "cr3 = 0x%016x\tcr4 = 0x%016x", r.cr3, r.cr4);
    os << buf << std::endl;
    sprintf(buf, "cr8 = 0x%016x", r.cr8);
    os << buf << std::endl;
    sprintf(buf, "idt :  base = 0x%016x\tlimit = %08x", r.idt.base, r.idt.limit);
    os << buf << std::endl;
    sprintf(buf, "gdt :  base = 0x%016x\tlimit = %08x", r.gdt.base, r.gdt.limit);
    os << buf << std::endl;
    return os;
}
