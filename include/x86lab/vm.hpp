#pragma once
#include <x86lab/util.hpp>
#include <x86lab/code.hpp>
#include <iostream>
#include <memory>
#include <vector>
#include <utility>

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

                bool operator==(Table const&) const = default;
            };
            Table idt;
            Table gdt;

            // MMX registers.
            vec64 mmx[8];

            // SSE's MXCSR register.
            u32 mxcsr;

            // XMM registers.
            vec128 xmm[16];

            // TODO: Include floating point registers.

            // YMM registers.
            // FIXME: YMM regs are aliased to their XMM reg for the bottom 128
            // bits. We could save a lot of space by deduplicating those bottom
            // bits in this struct.
            vec256 ymm[16];

            // Default constructor - all registers are set to 0.
            Registers();

            // Build a Registers from KVM's data structures.
            // @param regs: The value of general purpose registers.
            // @param sregs: The value of special registers.
            // @param xsave: The state of the xsave area.
            Registers(kvm_regs const& regs,
                      kvm_sregs const& sregs,
                      Util::Kvm::XSaveArea const& xsave);

            bool operator==(Registers const&) const = default;
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
        Registers m_regs;
        Memory m_mem;
    };

    // The supported CPU modes to start the Vm in. This indirectly controls the
    // initial value of the registers as well:
    //  - General purpose registers rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp,
    //  r8-r15 always start with zero value, no matter which starting mode is
    //  selected. /!\ This means no stack is setup, until loadCode is called.
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
    // to allocate for the VM. Note: In case the Vm is started in LongMode, more
    // physical memory is allocated than requested to hold the page table
    // structure. This extra memory is NOT reported when calling getState(). The
    // rationale here is that in most cases the user is not interested in seeing
    // the page tables.
    // @throws: A KvmError is thrown in case of any error related to the KVM
    // initialization.
    // @throws: A MmapError is thrown in case of any error related to
    // mmap'ing.
    Vm(CpuMode const startMode, u64 const memorySize);

    // Destroy the VM. This deallocates all mmaped physical memory and releases
    // KVM resources.
    ~Vm();

    // Load code in the Vm. The code is placed at address 0 and rip is reset to
    // 0, e.g. pointing to the first instruction, rsp is set to point at the
    // very top of the physical memory. After this function returns, the
    // operating mode of the Vm is set to Runnable and execution can be started.
    // @param code: The Code to be loaded.
    void loadCode(Code const& code);

    // Get a copy of this VM's state. Note that this is an expensive operation
    // since it creates a full copy of the VM's physical memory.
    // @return: An instance of State containing the full state of this Vm.
    std::unique_ptr<Vm::State> getState() const;

    // Get the current values of the registers on the vCpu.
    // @return: A complete State::Registers holding the values of the register
    // as of the time after the last instruction was executed.
    State::Registers getRegisters() const;

    // Set the values of the registers on the vCpu. For now, setting segment
    // register values through this functions is NOT supported, therefore the
    // values of cs, ds, es, fs, gs and ss in `registerValues` are ignored.
    // Control registers, EFER, IDT and GDT are supported.
    // @param registerValues: A State::Registers holding all the values that
    // should be written.
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

    // Setup the control registers to enable the requested cpu mode.
    // @param sregs: The kvm_sregs structure containing the control registers
    // that need to be initialized for the cpu mode.
    // @param mode: The mode to enable on the vcpu.
    // Note: In the case mode == CpuMode::LongMode, this function also sets up
    // the page tables to have identity mapping.
    void enableCpuMode(kvm_sregs& sregs, CpuMode const mode);

    // Only used when the VM is started in Long Mode, this setup the page table
    // structure to identity map the entire physical memory in virtual memory.
    // @return: The guest's physical offset of the PML4 table, to be loaded in
    // CR3.
    u64 createIdentityMapping();

    // Add more physical memory to the guest. The added memory starts at the end
    // of the current physical memory.
    // @param numPages: The number of physical pages frames to allocate.
    // @param isReadOnly: Indicate if this memory should be read-only for the
    // guest VM. The user-space (e.g. this program) always have write permission
    // on the allocated memory regardless of the value of isReadOnly.
    // @return: A pair in which the first value is the host-address at which the
    // new memory was mmap'ed, and the second is the guest-physical-address at
    // which the allocated memory starts.
    // @throws: KvmError or MmapError in case of kvm ioctl error or mmap error.
    std::pair<void*, u64> addPhysicalMemory(u32 const numPages);

    // File descriptor for the KVM.
    int const m_vmFd;
    // File descriptor for the vCpu.
    int const m_vcpuFd;
    // Reference to the kvm_run structure associated with the vCpu. For now we
    // only read the kvm_run structure to get information on the exit reason,
    // hence use const reference.
    kvm_run const& m_kvmRun;
    // The size of the guest's physical memory in bytes.
    size_t m_physicalMemorySize;
    // Description of all the memory slots of this VM.
    std::vector<kvm_userspace_memory_region> m_memorySlots;
    // Pointer to start of physical memory on the host (e.g. userspace).
    void *m_memory;
    // The current OperatingState of the KVM.
    OperatingState m_currState;
};
}
