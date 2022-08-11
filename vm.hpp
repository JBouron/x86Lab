#include <iostream>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

namespace X86Lab {

class Error : public std::runtime_error {
public:
    Error(std::string const& what, int const errNo) :
        std::runtime_error(what),
        errNo(errNo) {}
    int errNo;
};

class KvmError : public Error {
public:
    KvmError(std::string const& what, int const errNo) : Error(what, errNo) {}
};
class MmapError : public Error {
public:
    MmapError(std::string const& what, int const errNo) : Error(what, errNo) {}
};

constexpr size_t PAGE_SIZE(4096);

// Encapsulate state related to a KVM and allows interactions with it.
// As of now under X86Lab, KVMs always have a single vCpu since the goal is to
// analyze a small piece of assembly.
class Vm {
public:
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

    // Dump of register values.
    struct RegisterFile {
        // General-purpose registers.
        u64 rax; u64 rbx; u64 rcx; u64 rdx;
        u64 rdi; u64 rsi; u64 rsp; u64 rbp;
        u64 r8;  u64 r9;  u64 r10; u64 r11;
        u64 r12; u64 r13; u64 r14; u64 r15;

        // Special registers.
        u64 rflags;
        u64 rip;


        // Segment registers.
        u16 cs;
        u16 ds;
        u16 es;
        u16 fs;
        u16 gs;
        u16 ss;

        // Control registers.
        u64 cr0;
        u64 cr2;
        u64 cr3;
        u64 cr4;
        u64 cr8;
        u64 efer;

        // Tables.
        struct Table {
            u64 base;
            u16 limit;
        };
        Table idt;
        Table gdt;
    };

    // Get the current values of the registers on the vCpu.
    // @return: A complete RegisterFile holding the values of the register as of
    // the time after the last instruction was executed.
    RegisterFile getRegisters() const;

    // Set the values of the registers on the vCpu. Not that all registers are
    // set.
    // @param registerValues: A RegisterFile holding all the values that should
    // be written.
    // @throws: KvmError in case of any KVM ioctl error.
    void setRegisters(RegisterFile const& registerValues);

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
    enum class State {
        // The KVM is runnable.
        Runnable,
        // The KVM is non-runnable because no code has been loaded.
        NoCodeLoaded,
        // The KVM is non-runnable because the last run caused an issue.
        SingleStepError,
    };

    // Get the state of the KVM.
    // @return: An enum State indicating if the KVM is runnable or not.
    State state() const;

    // Execute a single instruction in the KVM.
    // @return: The state of the KVM after executing a single instruction.
    // @throws: KvmError in case of any KVM ioctl error.
    State step();

private:
    // Add more physical memory to the guest.
    // @param offset: The offset at which the memory should be added.
    // @param size: The amount of memory to add to the guest in bytes. Must be a
    // multiple of PAGE_SIZE.
    // @return: The userspace address of the allocated memory.
    // @throws: KvmError or MmapError in case of kvm ioctl error or mmap error.
    void *addPhysicalMemory(u64 const offset, size_t const size);

    // File descriptor on /dev/kvm.
    int kvmHandle;
    // File descriptor for the KVM.
    int vmFd;
    // File descriptor for the vCpu.
    int vcpuFd;
    // Pointer to the kvm_run structure associated with the vCpu.
    kvm_run * kvmRun;
    // The size of the guest's physical memory in bytes.
    size_t physicalMemorySize;
    // Pointer to start of physical memory on the host (e.g. userspace).
    void *memory;
    // The current state of the KVM.
    State currState;
    // The number of memory slots used. This corrolates with the number of times
    // addPhysicalMemory has been called.
    u32 usedMemorySlots;
};
}

std::ostream& operator<<(std::ostream& os, X86Lab::Vm::RegisterFile const& r);
