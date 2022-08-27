#pragma once
#include <iostream>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
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
        };

        // Snapshot of the VM's physical memory.
        struct Memory {
            // Pointer to the memory snapshot, this is a _copy_ of the full
            // physical memory, changing this as no effect on the running VM.
            std::unique_ptr<u8> data;
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

    // Creates a KVM with the given amount of memory.
    // @param memorySize: The amount of physical memory in multiple of PAGE_SIZE
    // to allocate for the VM.
    // @throws: A KvmError is thrown in case of any error related to the KVM
    // initialization.
    // @throws: A MmapError is thrown in case of any error related to
    // mmap'ing.
    Vm(u64 const memorySize);

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

    // Get pointer to guest's physical memory.
    // Very unsafe, but certainly very fun.
    void *getMemory();

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
    OperatingState state() const;

    // Execute a single instruction in the KVM.
    // @return: The OperatingState of the KVM after executing a single
    // instruction.
    // @throws: KvmError in case of any KVM ioctl error.
    OperatingState step();

private:
    // The following methods are used as part of the constructor. They allow to
    // have const members for this class.

    // Get a file descriptor on /dev/kvm.
    // @return: The file descriptor.
    // @throws: An Error in case of error.
    static int getKvmHandle();

    // Create a KVM VM.
    // @param kvmHandle: File descriptor on /dev/kvm for ioctl calls.
    // @return: The file descriptor associated to the created VM.
    // @throws: An Error in case of error.
    static int createKvmVm(int const kvmHandle);

    // Create a Vcpu to a KVM Vm.
    // @param vmFd: File descriptor on the KVM Vm for which to add the vcpu to.
    // @return: The file descriptor associated to the vcpu.
    // @throws: An Error in case of error.
    static int createKvmVcpu(int const vmFd);

    // Mmap the run structure of the vcpu. Assumes that vCpu has been
    // initialized.
    // @param kvmHandle: File descriptor on /dev/kvm for ioctl calls.
    // @param vcpuFd: File descriptor for the vCpu.
    // @return: A reference to the kvm_run structure associated with vcpuFd.
    // @throws: An Error in case of error.
    static kvm_run& getKvmRunStruct(int const kvmHandle, int const vcpuFd);

    // Some low-level function wrapper for KVM ioctl calls.

    // Perform a KVM_GET_REGS ioctl on this Vm.
    // @return: A kvm_regs holding the current values of the registers on the
    // VM. Note: This returns by value!
    // @throws: A KvmError in case of error.
    kvm_regs kvmGetRegs() const;

    // Perform a KVM_SET_REGS ioctl on this Vm.
    // @param regs: The kvm_regs to write.
    // @throws: A KvmError in case of error.
    void kvmSetRegs(kvm_regs const& regs);

    // Perform a KVM_GET_SREGS ioctl on this Vm.
    // @return: A kvm_sregs holding the current values of the special registers
    // on the VM. Note: This returns by value!
    // @throws: A KvmError in case of error.
    kvm_sregs kvmGetSRegs() const;

    // Perform a KVM_SET_SREGS ioctl on this Vm.
    // @param regs: The kvm_sregs to write.
    // @throws: A KvmError in case of error.
    void kvmSetSRegs(kvm_sregs const& regs);

    // Add more physical memory to the guest.
    // @param offset: The offset at which the memory should be added.
    // @param size: The amount of memory to add to the guest in bytes. Must be a
    // multiple of PAGE_SIZE.
    // @return: The userspace address of the allocated memory.
    // @throws: KvmError or MmapError in case of kvm ioctl error or mmap error.
    void *addPhysicalMemory(u64 const offset, size_t const size);

    // File descriptor on /dev/kvm.
    int const kvmHandle;
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
