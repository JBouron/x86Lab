#pragma once
#include <iostream>
#include <memory>
#include <x86lab/util.hpp>

namespace X86Lab {

constexpr size_t PAGE_SIZE(4096);

// Encapsulate state related to a KVM and allows interactions with it.
// As of now under X86Lab, KVMs always have a single vCpu since the goal is to
// analyze a small piece of assembly.
class Vm {
public:
    // Contains a snapshot of the internal state of a VM (registers, memory,
    // ...).
    class State {
    public:
        // Holds the values of all the registers of a VM.
        struct Registers {
            // General-purpose registers.
            u64 rax; u64 rbx; u64 rcx; u64 rdx;
            u64 rdi; u64 rsi; u64 rsp; u64 rbp;
            u64 r8;  u64 r9;  u64 r10; u64 r11;
            u64 r12; u64 r13; u64 r14; u64 r15;

            // Special registers.
            u64 rflags; u64 rip;


            // Segment registers.
            u16 cs; u16 ds; u16 es;
            u16 fs; u16 gs; u16 ss;

            // Control registers.
            u64 cr0; u64 cr2;
            u64 cr3; u64 cr4;
            u64 cr8;
            u64 efer;

            // Tables.
            struct Table {
                u64 base;
                u16 limit;
            };
            Table idt;
            Table gdt;

            // TODO: Include floating point registers.

            // Build a Registers from KVM's data structures.
            // @param regs: The value of general purpose registers.
            // @param sregs: The value of special registers.
            Registers(kvm_regs const& regs, kvm_sregs const& sregs);
        };

        // Snapshot of the VM's physical memory.
        struct Memory {
            // Pointer to the memory snapshot, this is a _copy_ of the full
            // physical memory, changing this as no effect on the running VM.
            std::unique_ptr<u8[]> data;
            // The size of the memory snapshot (and data) in bytes.
            u64 size;
        };

        // Get the value of the registers of this snapshot.
        Registers const& registers() const;

        // Get the physical memory dump of this snapshot.
        Memory const& memory() const;

        // Build a State snapshot.
        // @param regs: The register values.
        // @param mem: The dump of the physical memory.
        State(Registers const& regs, Memory && mem);

    private:
        Registers regs;
        Memory mem;
    };

    // The supported CPU modes to start the Vm in. This indirectly controls the
    // initial value of the registers as well:
    //  - General purpose registers rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp,
    //  r8-r15 always start with zero value, no matter which starting mode is
    //  selected. /!\ This means no stack is setup!
    //  - Rflags starts with value 0x2 (e.g. all non-reserved bits are 0 and
    //  interrupts disabled).
    //  - Segment registers value depend on the mode and are described below.
    //  - Control registers (CR0-CR8 and EFER) are set according to the selected
    //  mode (e.g. PE is set in CR0 if protected mode is requested, etc...).
    //  - IDT and GDT have their base and limit set to 0 (e.g. invalid).
    enum class CpuMode {
        // 16-bit real-mode. All segment registers are set to 0.
        RealMode,
        // 32-bit protected mode with paging disabled. All segment registers are
        // set to a ring 0 flat-segment spanning the entire address space (e.g.
        // base = 0, limit = 0xFFFFF, granularity = 1). No GDT is constructed,
        // only the hidden parts of the segments are set. The "visible" part of
        // the segment registers (e.g. selectors) is set to 0. Attempting to
        // reload or change a segment register will lead to a #GP, you need to
        // setup your own GDT first.
        ProtectedMode,
        // 64-bit long mode with paging enabled. This mode uses identity paging
        // for the entire address space with supervisor and read/write/execute
        // access everywhere. FS and GS are zeroed. No GDT is constructed, only
        // the hidden parts of the segment registers are set. As with
        // protected-mode, The "visible" part of the segment registers (e.g.
        // selectors) is set to 0. Attempting to reload or change a segment
        // register will lead to a #GP, you need to setup your own GDT first.
        LongMode,
    };

    // Creates a KVM with the given amount of memory.
    // @param startMode: The mode in which to start the Vm in.
    // @param memorySize: The amount of physical memory in multiple of PAGE_SIZE
    // to allocate for the VM.
    // @throws: A KvmError is thrown in case of any error related to the KVM
    // initialization.
    // @throws: A MmapError is thrown in case of any error related to
    // mmap'ing.
    Vm(CpuMode const startMode, u64 const memorySize);

    // Load code in the Vm. This function also sets the instruction pionter to
    // the start of the loaded code. The address at which the code is actually
    // loaded is un-defined.
    // @param shellCode: Pointer to x86 machine code to be loaded.
    // @param shellCodeSize: The size of the code to be loaded in bytes.
    void loadCode(u8 const * const shellCode, u64 const shellCodeSize);

    // Get a copy of this VM's state. Note that this is an expensive operation
    // since it creates a full copy of the VM's physical memory.
    // @return: An instance of State containing the full state of this Vm.
    std::unique_ptr<Vm::State> getState() const;

    // Get the current values of the registers on the vCpu.
    // @return: A complete State::Registers holding the values of the register as of
    // the time after the last instruction was executed.
    State::Registers getRegisters() const;

    // Set the values of the registers on the vCpu. Not that all registers are
    // set.
    // @param registerValues: A State::Registers holding all the values that should
    // be written.
    // @throws: KvmError in case of any KVM ioctl error.
    void setRegisters(State::Registers const& registerValues);

    // State of the KVM.
    enum class OperatingState {
        // The KVM is runnable.
        Runnable,
        // The KVM has been shutdown.
        Shutdown,
        // The KVM executed a halt instruction.
        Halted,
        // The KVM is non-runnable because no code has been loaded.
        NoCodeLoaded,
        // The KVM is non-runnable because the last run caused an issue.
        SingleStepError,
    };

    // Get the OperatingState of the KVM.
    // @return: An enum OperatingState indicating if the KVM is runnable or not.
    OperatingState operatingState() const;

    // Execute a single instruction in the KVM.
    // @return: The OperatingState of the KVM after executing a single
    // instruction.
    // @throws: KvmError in case of any KVM ioctl error.
    OperatingState step();

private:
    // Set the registers to their initial value depending on the mode. This
    // function also takes care of setting the vCpu for the desired mode.
    // @param mode: The starting mode of the vCpu. This defines the initial
    // register values.
    void setRegistersInitialValue(CpuMode const mode);

    // Enable 32-bit protected mode on the vCpu. This does NOT enable paging.
    // Note that using this function does not setup a GDT. It merely sets the
    // segment registers' hidden base/limit/access flags so that the vCpu is
    // tricked into using 32-bit PM. GDTR is left untouched, therefore code
    // running (in the Vm) after calling this function mustn't reload segment
    // registers as this would cause an exception (unless you actually setup a
    // GDT and GDTR yourself before hand.
    // The segmentation model used is flat-segments, e.g segments have base 0
    // and limit 0xFFFFF with 4KiB page granularity.
    // All segments are in ring 0.
    // @throws: KvmError in case of any KVM ioctl error.
    void enableProtectedMode();

    // Enable 64-bit/Long mode on the vCpu. This function does setup a simple
    // paging setup where the first 1GiB of the guest physical memory is
    // identity mapped. As with enableProtectedMode() there no GDT is setup,
    // instead segment registers have their hidden written.
    // @throws: KvmError in case of any KVM ioctl error, MmapError in case of a
    // mmap error.
    void enable64BitsMode();

    // Add more physical memory to the guest.
    // @param offset: The offset at which the memory should be added.
    // @param size: The amount of memory to add to the guest in bytes. Must be a
    // multiple of PAGE_SIZE.
    // @return: The userspace address of the allocated memory.
    // @throws: KvmError or MmapError in case of kvm ioctl error or mmap error.
    void *addPhysicalMemory(u64 const offset, size_t const size);

    // File descriptor for the KVM.
    int const vmFd;
    // File descriptor for the vCpu.
    int const vcpuFd;
    // Reference to the kvm_run structure associated with the vCpu. For now we
    // only read the kvm_run structure to get information on the exit reason,
    // hence use const reference.
    kvm_run const& kvmRun;
    // The number of memory slots used. This corrolates with the number of times
    // addPhysicalMemory has been called.
    u32 usedMemorySlots;
    // The size of the guest's physical memory in bytes.
    size_t physicalMemorySize;
    // Pointer to start of physical memory on the host (e.g. userspace).
    void *memory;
    // The current OperatingState of the KVM.
    OperatingState currState;
    // Is the state of the VM currently real mode.
    bool isRealMode;
};
}
