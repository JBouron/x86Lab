#include <x86lab/vm.hpp>
#include <x86lab/code.hpp>
#include <x86lab/test.hpp>
#include <fstream>

// Various tests for the X86Lab::Vm.

namespace X86Lab::Test::Vm {
// Helper function to create a VM and load the given code to memory.
// @param startMode: The cpu mode the VM should start in.
// @param assembly: The assembly code to assemble and load into the Vm.
// @param memorySizePages: The size of the physical memory in number of pages.
// @return: A unique_ptr for the instantiated VM.
static std::unique_ptr<X86Lab::Vm> createVmAndLoadCode(
    X86Lab::Vm::CpuMode const startMode,
    std::string const& assembly,
    u64 const memorySizePages = 1) {

    // Create a temporary file to write the code into, it will be used as input
    // file for the assembler.
    Util::TempFile source("/tmp/x86lab_testcode");
    std::ofstream file(source.ostream());
    if (!file) {
        throw X86Lab::Error("Cannot open temporary file", errno);
    }
    file << assembly;
    file.close();

    Code const code(source.path());
    std::unique_ptr<X86Lab::Vm> vm(new X86Lab::Vm(startMode, memorySizePages));
    vm->loadCode(code);
    return vm;
}

// The simplest test there is: Execute a few NOPs and make sure as well as
// RFALGS are as expected.
DECLARE_TEST(testReadRipAndRflags) {
    // Note: Each instruction is 1 byte long except for xor rax, rax which is 3
    // bytes.
    std::string const assembly(R"(
        BITS 64

        sti
        nop
        cli
        xor     rax, rax
		nop
        hlt
    )");

    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    // Make sure that the VM starts executing the code with interrupts disabled.
    X86Lab::Vm::State::Registers regs(vm->getRegisters());
    TEST_ASSERT(!(regs.rflags & (1 << 9)));
    u64 const codeStart(regs.rip);

    // Run sti.
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm->getRegisters();
    // Interrupts are expected to be enabled at that point.
    TEST_ASSERT(regs.rflags & (1 << 9));
    TEST_ASSERT(regs.rip == codeStart + 1);

    // Run nop.
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm->getRegisters();
    // Interrupts are still enabled.
    TEST_ASSERT(regs.rflags & (1 << 9));
    TEST_ASSERT(regs.rip == codeStart + 2);

    // Run cli.
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm->getRegisters();
    // Interrupts should be disabled.
    TEST_ASSERT(!(regs.rflags & (1 << 9)));
    TEST_ASSERT(regs.rip == codeStart + 3);

    // Run xor rax, rax.
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm->getRegisters();
    // Interrupts should still be disabled.
    TEST_ASSERT(!(regs.rflags & (1 << 9)));
    TEST_ASSERT(regs.rip == codeStart + 6);
    
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
}

// Test running a Vm in real mode, setting the GP registers to specific values
// and reading the register values at each step using getRegisters().
// Segment registers and other special registers are not tested here.
DECLARE_TEST(testRealMode) {
    // Set the 16-bits GP one by one and make sure the values reported by
    // Vm::getRegisters() are as expected.
    std::string const assembly(R"(
        BITS 16

        mov     ax, 0xABCD
        mov     ax, 0

        mov     bx, 0xABCD
        mov     bx, 0

        mov     cx, 0xABCD
        mov     cx, 0

        mov     dx, 0xABCD
        mov     dx, 0

        mov     di, 0xABCD
        mov     di, 0

        mov     si, 0xABCD
        mov     si, 0

        mov     bp, 0xABCD
        mov     bp, 0

        mov     sp, 0xABCD
        mov     sp, 0

        hlt
    )");

    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::RealMode, assembly));
    u16 const defaultSp(X86Lab::PAGE_SIZE);

    // Check the values of the general purpose registers (r8-r15 are ignored
    // since we are running in 16 bits, those are expected to be zero).
    // @param rax, ..., rsp: The expected values of the GP registers.
    auto const checkRegs([&](u64 rax, u64 rbx, u64 rcx, u64 rdx,
                             u64 rdi, u64 rsi, u64 rbp, u64 rsp) {
        X86Lab::Vm::State::Registers const regs(vm->getRegisters());
        TEST_ASSERT(regs.rax == rax); TEST_ASSERT(regs.rbx == rbx);
        TEST_ASSERT(regs.rcx == rcx); TEST_ASSERT(regs.rdx == rdx);
        TEST_ASSERT(regs.rdi == rdi); TEST_ASSERT(regs.rsi == rsi);
        TEST_ASSERT(regs.rbp == rbp); TEST_ASSERT(regs.rsp == rsp);
        TEST_ASSERT(!regs.r8);  TEST_ASSERT(!regs.r9);  TEST_ASSERT(!regs.r10);
        TEST_ASSERT(!regs.r11); TEST_ASSERT(!regs.r12); TEST_ASSERT(!regs.r13);
        TEST_ASSERT(!regs.r14); TEST_ASSERT(!regs.r15);
        // Special registers are not checked.
    });

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Initially all registers are expected to be zeroed.
    checkRegs(0, 0, 0, 0, 0, 0, 0, defaultSp);

    // Run the first instruction setting ax.
    runNSteps(1);

    // The expected value.
    u64 const val(0xABCD);
    checkRegs(val, 0, 0, 0, 0, 0, 0, defaultSp);
    runNSteps(2);
    checkRegs(0, val, 0, 0, 0, 0, 0, defaultSp);
    runNSteps(2);
    checkRegs(0, 0, val, 0, 0, 0, 0, defaultSp);
    runNSteps(2);
    checkRegs(0, 0, 0, val, 0, 0, 0, defaultSp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, val, 0, 0, defaultSp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, val, 0, defaultSp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, val, defaultSp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, val);
}

// Test running a Vm in protected mode, setting the GP registers to specific
// values and reading the register values at each step using getRegisters().
// Segment registers and other special registers are not tested here.
DECLARE_TEST(testProtectedMode) {
    // Set the 32-bits GP one by one and make sure the values reported by
    // Vm::getRegisters() are as expected.
    std::string const assembly(R"(
        BITS 32

        mov     eax, 0xABCD1234
        mov     eax, 0

        mov     ebx, 0xABCD1234
        mov     ebx, 0

        mov     ecx, 0xABCD1234
        mov     ecx, 0

        mov     edx, 0xABCD1234
        mov     edx, 0

        mov     edi, 0xABCD1234
        mov     edi, 0

        mov     esi, 0xABCD1234
        mov     esi, 0

        mov     ebp, 0xABCD1234
        mov     ebp, 0

        mov     esp, 0xABCD1234
        mov     esp, 0

        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::ProtectedMode, assembly));
    u32 const defaultEsp(X86Lab::PAGE_SIZE);

    // Check the values of the general purpose registers (r8-r15 are ignored
    // since we are running in 32 bits, those are expected to be zero).
    // @param rax, ..., rsp: The expected values of the GP registers.
    auto const checkRegs([&](u64 rax, u64 rbx, u64 rcx, u64 rdx,
                             u64 rdi, u64 rsi, u64 rbp, u64 rsp) {
        X86Lab::Vm::State::Registers const regs(vm->getRegisters());
        TEST_ASSERT(regs.rax == rax); TEST_ASSERT(regs.rbx == rbx);
        TEST_ASSERT(regs.rcx == rcx); TEST_ASSERT(regs.rdx == rdx);
        TEST_ASSERT(regs.rdi == rdi); TEST_ASSERT(regs.rsi == rsi);
        TEST_ASSERT(regs.rbp == rbp); TEST_ASSERT(regs.rsp == rsp);
        TEST_ASSERT(!regs.r8);  TEST_ASSERT(!regs.r9);  TEST_ASSERT(!regs.r10);
        TEST_ASSERT(!regs.r11); TEST_ASSERT(!regs.r12); TEST_ASSERT(!regs.r13);
        TEST_ASSERT(!regs.r14); TEST_ASSERT(!regs.r15);
        // Special registers are not checked.
    });

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Initially all registers are expected to be zeroed.
    checkRegs(0, 0, 0, 0, 0, 0, 0, defaultEsp);

    // Run the first instruction setting ax.
    runNSteps(1);

    // The expected value.
    u64 const val(0xABCD1234);
    checkRegs(val, 0, 0, 0, 0, 0, 0, defaultEsp);
    runNSteps(2);
    checkRegs(0, val, 0, 0, 0, 0, 0, defaultEsp);
    runNSteps(2);
    checkRegs(0, 0, val, 0, 0, 0, 0, defaultEsp);
    runNSteps(2);
    checkRegs(0, 0, 0, val, 0, 0, 0, defaultEsp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, val, 0, 0, defaultEsp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, val, 0, defaultEsp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, val, defaultEsp);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, val);
}

// Test running a Vm in long mode, setting the GP registers to specific values
// and reading the register values at each step using getRegisters().  Segment
// registers and other special registers are not tested here.
DECLARE_TEST(testLongMode) {
    // Set the 64-bits GP one by one and make sure the values reported by
    // Vm::getRegisters() are as expected.
    std::string const assembly(R"(
        BITS 64

        mov     rax, 0xABCDEF1234567890
        mov     rax, 0

        mov     rbx, 0xABCDEF1234567890
        mov     rbx, 0

        mov     rcx, 0xABCDEF1234567890
        mov     rcx, 0

        mov     rdx, 0xABCDEF1234567890
        mov     rdx, 0

        mov     rdi, 0xABCDEF1234567890
        mov     rdi, 0

        mov     rsi, 0xABCDEF1234567890
        mov     rsi, 0

        mov     rbp, 0xABCDEF1234567890
        mov     rbp, 0

        mov     rsp, 0xABCDEF1234567890
        mov     rsp, 0

        mov     r8, 0xABCDEF1234567890
        mov     r8, 0

        mov     r9, 0xABCDEF1234567890
        mov     r9, 0

        mov     r10, 0xABCDEF1234567890
        mov     r10, 0

        mov     r11, 0xABCDEF1234567890
        mov     r11, 0

        mov     r12, 0xABCDEF1234567890
        mov     r12, 0

        mov     r13, 0xABCDEF1234567890
        mov     r13, 0

        mov     r14, 0xABCDEF1234567890
        mov     r14, 0

        mov     r15, 0xABCDEF1234567890
        mov     r15, 0

        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));
    u64 const defaultRsp(X86Lab::PAGE_SIZE);

    // Check the values of the general purpose registers
    // @param rax, ..., r15: The expected values of the GP registers.
    auto const checkRegs([&](u64 rax, u64 rbx, u64 rcx, u64 rdx,
                             u64 rdi, u64 rsi, u64 rbp, u64 rsp,
                             u64 r8,  u64 r9,  u64 r10, u64 r11,
                             u64 r12, u64 r13, u64 r14, u64 r15) {
        X86Lab::Vm::State::Registers const regs(vm->getRegisters());
        TEST_ASSERT(regs.rax == rax); TEST_ASSERT(regs.rbx == rbx);
        TEST_ASSERT(regs.rcx == rcx); TEST_ASSERT(regs.rdx == rdx);
        TEST_ASSERT(regs.rdi == rdi); TEST_ASSERT(regs.rsi == rsi);
        TEST_ASSERT(regs.rbp == rbp); TEST_ASSERT(regs.rsp == rsp);
        TEST_ASSERT(regs.r8 == r8);   TEST_ASSERT(regs.r9 == r9);
        TEST_ASSERT(regs.r10 == r10); TEST_ASSERT(regs.r11 == r11);
        TEST_ASSERT(regs.r12 == r12); TEST_ASSERT(regs.r13 == r13);
        TEST_ASSERT(regs.r14 == r14); TEST_ASSERT(regs.r15 == r15);
        // Special registers are not checked.
    });

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Initially all registers are expected to be zeroed.
    checkRegs(0, 0, 0, 0, 0, 0, 0, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);

    // Run the first instruction setting ax.
    runNSteps(1);

    // The expected value.
    u64 const val(0xABCDEF1234567890);
    checkRegs(val, 0, 0, 0, 0, 0, 0, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, val, 0, 0, 0, 0, 0, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, val, 0, 0, 0, 0, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, val, 0, 0, 0, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, val, 0, 0, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, val, 0, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, val, defaultRsp, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, val, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, val, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, val, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, val, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, val, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, val, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, val, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, val, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, val);
}

// Test that getRegisters() returns the correct values for segment registers.
DECLARE_TEST(testReadSegmentRegisters) {
    // Use a VM running in RealMode so that we can set the segment registers to
    // arbitrary values without having to build a GDT first.
    std::string const assembly(R"(
        BITS 16
            ; Far jump to start.
            jmp     0x1:0x0

        align   16
        start:
            mov     ax, 0xDDDD 
            mov     ds, ax
            mov     ax, 0xEEEE
            mov     es, ax
            mov     ax, 0xFFFF
            mov     fs, ax
            mov     ax, 0x1111
            mov     gs, ax
            mov     ax, 0x2222
            mov     ss, ax
            ; See note below as to why the nop is required here.
            nop
            hlt
    )");
    // Notice the nop after the mov to SS. This is needed because moving to SS
    // inhibits the interrupts until after the next instruction has executed.
    // Hence, if we are to call vm->step() on the "mov  ss, ax", we would in
    // fact run two instrutions. Without the nop this would run the mov and the
    // hlt, leaving the VM in a non-runnable operatingState.
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::RealMode, assembly));

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Run first instruction, this sets CS to 0x1.
    runNSteps(1);
    TEST_ASSERT(vm->getRegisters().cs == 0x1);
    runNSteps(2);
    TEST_ASSERT(vm->getRegisters().ds == 0xDDDD);
    runNSteps(2);
    TEST_ASSERT(vm->getRegisters().es == 0xEEEE);
    runNSteps(2);
    TEST_ASSERT(vm->getRegisters().fs == 0xFFFF);
    runNSteps(2);
    TEST_ASSERT(vm->getRegisters().gs == 0x1111);
    runNSteps(2);
    TEST_ASSERT(vm->getRegisters().ss == 0x2222);
}

// Check that getRegisters returns the correct values for GDTR and IDTR.
DECLARE_TEST(testReadGdtIdt) {
    // We actually can set the GDTR and IDTR to arbitrary values, as long as we
    // are not reloading the segment registers.
    // The GDT and IDT limits are supposed to be for the form 8*N - 1 per x86-64
    // specs.
    // Lastly, we are in 64-bit mode and therefore subject to canonical
    // addresses. We could compute the number of physical bits used by the VM
    // but that would be too cumbersome. Instead assume a conservative number of
    // phys bits (36).
    std::string const assembly(R"(
        BITS 64

        lgdt    [gdtValue]
        lidt    [idtValue]
        nop
        hlt

        ; GDT descriptor
        gdtValue:
        dw 0x8887
        dq 0xFFFFFFF8CAFEBABE

        ; IDT descriptor
        idtValue:
        dw 0xABC7
        dq 0xFFFFFFF8ABCDEF12
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    // Run the lgdt and lidt instructions.
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);

    // Check the reported values.
    X86Lab::Vm::State::Registers const regs(vm->getRegisters());
    TEST_ASSERT(regs.gdt.base == 0xFFFFFFF8CAFEBABE);
    TEST_ASSERT(regs.gdt.limit == 0x8887);
    TEST_ASSERT(regs.idt.base == 0xFFFFFFF8ABCDEF12);
    TEST_ASSERT(regs.idt.limit == 0xABC7);
}

// Check that getRegisters() reads the correct values of all control registers.
DECLARE_TEST(testReadControlRegisters) {
    // Modify the control register as best as we can. More specifically:
    //  - Toggle the Cache Disable (CD, bit 30) and Not Write-through (NW, bit
    //  29) bits in CR0. Toggle both of them is always a valid configuration
    //  (either both 0 or both 1).
    //  - Setup CR2 to an arbitrary 64-bit value. This is surprisingly
    //  supported even if the value is not 4KiB aligned or canonical.
    //  - Toggle the Page-level Write-through (PWT, bit 3) of CR3.
    //  - Toggle Time Stamp Disable (TSD, bit 2) of CR4.
    //  - Toggle the Task Priority Level (TPL, bits 0-3) of CR8.
    //  - Toggle the No-Execute Enable (NXE, bit 11) of EFER.
    std::string const assembly(R"(
        BITS 64

        mov     rax, cr0
        xor     rax, (1 << 30)
        xor     rax, (1 << 29)
        mov     cr0, rax

        mov     rax, 0xDEADBEEFCAFEBABE
        mov     cr2, rax

        mov     rax, cr3
        xor     rax, (1 << 3)
        mov     cr3, rax

        mov     rax, cr4
        xor     rax, (1 << 2)
        mov     cr4, rax

        mov     rax, cr8
        xor     rax, 0xF
        mov     cr8, rax

        mov     ecx, 0xC0000080
        rdmsr
        xor     eax, (1 << 11)
        wrmsr

        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    X86Lab::Vm::State::Registers const prev(vm->getRegisters());

    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });
    // Execute all 19 instructions (not the hlt).
    runNSteps(19);

    // Check the new values of the control regs.
    X86Lab::Vm::State::Registers const regs(vm->getRegisters());
    TEST_ASSERT(regs.cr0 == (prev.cr0 ^ ((1 << 30) | (1 << 29))));
    TEST_ASSERT(regs.cr2 == 0xDEADBEEFCAFEBABE);
    TEST_ASSERT(regs.cr3 == (prev.cr3 ^ (1 << 3)));
    TEST_ASSERT(regs.cr4 == (prev.cr4 ^ (1 << 2)));
    TEST_ASSERT(regs.cr8 == (prev.cr8 ^ 0xF));
    TEST_ASSERT(regs.efer == (prev.efer ^ (1 << 11)));
}

// Check that Vm::setRegisters() actually set the registers to their correct
// value. For now GP registers, control registers, GDTR and IDTR and XMM
// registers  are tested. Segment registers are not tested here since changing
// their value is not yet fully specified (FIXME).
// This test assumes that Vm::getRegisters() works as intended an returns the
// correct value of all registers (getRegisters is tested above).
DECLARE_TEST(testSetRegisters) {
    // A single instruction that 
    std::string const assembly(R"(
        BITS 64
        dq  0x0
        ; The instruction expected to be pointed to after setting rip using
        ; setRegisters.
        nop
        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    X86Lab::Vm::State::Registers expected(vm->getRegisters());

    // Set GPs to arbitrary values.
    expected.rax = 0x1111111111111111;
    expected.rbx = 0x2222222222222222;
    expected.rcx = 0x3333333333333333;
    expected.rdx = 0x4444444444444444;
    expected.rdi = 0x5555555555555555;
    expected.rsi = 0x6666666666666666;
    expected.rbp = 0x7777777777777777;
    expected.rsp = 0x5656565656565656;
    expected.r8  = 0x8888888888888888;
    expected.r9  = 0x9999999999999999;
    expected.r10 = 0xAAAAAAAAAAAAAAAA;
    expected.r11 = 0xBBBBBBBBBBBBBBBB;
    expected.r12 = 0xCCCCCCCCCCCCCCCC;
    expected.r13 = 0xDDDDDDDDDDDDDDDD;
    expected.r14 = 0xEEEEEEEEEEEEEEEE;
    expected.r15 = 0xFFFFFFFFFFFFFFFF;

    // Toggle the same bits in the control registers as
    // testReadControlRegisters.
    expected.cr0 ^= ((1 << 30) | (1 << 29));
    expected.cr2 = 0xDEADBEEFCAFEBABE;
    expected.cr3 ^= (1 << 3);
    expected.cr4 ^= (1 << 2);
    expected.cr8 ^= 0xF;
    expected.efer ^= (1 << 11);

    expected.gdt.base = 0xFFFFFFF8CAFEBABE;
    expected.gdt.limit = 0x8887;
    expected.idt.base = 0xFFFFFFF8ABCDEF12;
    expected.idt.limit = 0xABC7;

    // Set RIP to point to the nop instruction which is 8 bytes from the current
    // RIP.
    expected.rip += 0x8;

    // Toggle IF flag in RFLAGS.
    expected.rflags ^= (1 << 9);

    // Set the MMX registers to some arbitrary values.
    expected.mm0 = 0x0101010101010101;
    expected.mm1 = 0x0202020202020202;
    expected.mm2 = 0x0303030303030303;
    expected.mm3 = 0x0404040404040404;
    expected.mm4 = 0x0505050505050505;
    expected.mm5 = 0x0606060606060606;
    expected.mm6 = 0x0707070707070707;
    expected.mm7 = 0x0808080808080808;

    expected.mxcsr ^= (3 << 13);

    // XMM and YMM registers. Just some arbitrary patterns/values.
    for (u8 i(0); i < 16; ++i) {
        u64 const high(0x1111111111111111ULL * i);
        u64 const low(~high);
        u128 val(high);
        val <<= 64;
        val |= low;
        expected.xmm[i] = val;
        // FIXME: XMM and YMM are supposed to share some bits. However in the
        // Registers struct this is not the case, hence we need to make sure
        // that YMMi[128:0] == XMMi.
        expected.ymm[i][0] = val;
        expected.ymm[i][1] = ~val;
    }

    // Set the registers on the VM.
    vm->setRegisters(expected);

    // Run the single nop instruction.
    TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);

    X86Lab::Vm::State::Registers current(vm->getRegisters());
    // The NOP instruction does not change RFLAGS. However it incremented RIP by
    // one. Fixup RIP before doing the comparison with operator==.
    current.rip --;

    // FIXME: There seems to be an issue when setting CR8 through KVM_SET_SREGS.
    // For some reason, upon executing the first instruction after the
    // KVM_SET_SREGS, CR8 is reset. I unfortunately do not know enough about
    // Task Managment in x86 to understand if this is an issue with KVM itself
    // or expected behaviour (there is some coupling between CR8 and APIC).
    // Therefore, for now, ignore the value of CR8 in the comparison.
    current.cr8 = expected.cr8;
    
    // The registers should have the values with explicitely set with
    // setRegisters.
    TEST_ASSERT(current == expected);
}

// Test that values of segment registers are ignored when calling setRegisters,
// as writing those is not supported.
DECLARE_TEST(testSetRegistersSegmentRegisters) {
    // We are not actually running the VM in this test, merely setting the
    // registers.
    std::string const assembly(R"(
        BITS 64
        nop
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    X86Lab::Vm::State::Registers const origRegs(vm->getRegisters());
    X86Lab::Vm::State::Registers regs(origRegs);
    // Attempt to set the segment registers to their 1-complement value.
    regs.cs = !regs.cs;
    regs.ds = !regs.ds;
    regs.es = !regs.es;
    regs.fs = !regs.fs;
    regs.gs = !regs.gs;
    regs.ss = !regs.ss;

    vm->setRegisters(regs);

    // Re-read the registers and make sure that the segment registers did not
    // change.
    X86Lab::Vm::State::Registers const currRegs(vm->getRegisters());
    TEST_ASSERT(currRegs.cs == origRegs.cs);
    TEST_ASSERT(currRegs.ds == origRegs.ds);
    TEST_ASSERT(currRegs.es == origRegs.es);
    TEST_ASSERT(currRegs.fs == origRegs.fs);
    TEST_ASSERT(currRegs.gs == origRegs.gs);
    TEST_ASSERT(currRegs.ss == origRegs.ss);
}

// Test ensuring that 64-bits VMs are started with their entire physical memory
// being identity mapped.
DECLARE_TEST(test64BitIdentityMapping) {
    // The goal here is to test the mapping by writing a special value at the
    // beginning of each page in virtual memory, and then check that the value
    // is read back when reading the guest's physical memory.
    // Simple code writting RCX into the pointer RAX. Instead of using a loop,
    // we will manually set the value of rip to execute it as much as we want
    // while keeping the memory footprint the same.
    std::string const assembly(R"(
        BITS 64
        dq 0x0
        ; Next instruction starts at offset 0x8.
        mov     [rax], rcx
        nop
        hlt
    )");
    // 1024 pages = 4MiB of memory. This should be enough to test the identity
    // mapping.
    u64 const memSize(1024);
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly, memSize));

    for (u64 i(0); i < memSize; ++i) {
        u64 const writeOff(i * X86Lab::PAGE_SIZE);
        // Reset the registers to point to the mov instruction with the correct
        // address in RAX.
        X86Lab::Vm::State::Registers regs(vm->getRegisters());
        regs.rip = 0x8;
        regs.rcx = 0xDEADBEEFCAFEBABEULL;
        regs.rax = writeOff;
        vm->setRegisters(regs);

        // Run the mov.
        TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);

        std::unique_ptr<X86Lab::Vm::State> const state(vm->getState());
        u64 const read(
            *(reinterpret_cast<u64*>(state->memory().data.get() + writeOff)));
        TEST_ASSERT(read == regs.rcx);
    }
}

DECLARE_TEST(testReadMemory) {
    // Basic code that will be repeated to fill memory with a pattern this code
    // is 4 bytes long.
    std::string const assembly(R"(
        BITS 64
        rep     stosw
        hlt
    )");
    u64 const codeSize(4);

    u64 const memSize(128);
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly, memSize));

    // The number of WORDs to write, hence the entire memory, minus the code,
    // divided by the number of bytes per WORD (2). This is guaranteed to be
    // divisible by 2.
    u64 const numWordsToFill((memSize * PAGE_SIZE - codeSize) / 2);

    // Run the VM until the rep stosw has been fully executed. This is needed
    // because rep'ed instructions do not inhibit interrupts.
    auto const run([&]() {
        // Run until we reach the address of the hlt instruction. Note that in
        // case of rep'ed instruction, RIP points to the rep'ed instruction when
        // interrupted, not the next instruction.
        while(vm->getRegisters().rip != codeSize - 1) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    auto const checkMem([&](u16 const fillVal) {
        std::unique_ptr<X86Lab::Vm::State> const firstPass(vm->getState());
        u16 const * const raw(
            reinterpret_cast<u16 const*>(firstPass->memory().data.get()));

        // The first 4 bytes are f3 66 ab f4, aka what the code above assembles
        // to.
        u16 const firstWord(0x66f3);
        u16 const secondWord(0xf4ab);

        TEST_ASSERT(raw[0] == firstWord);
        TEST_ASSERT(raw[1] == secondWord);

        for (u64 i(0); i < numWordsToFill; ++i) {
            TEST_ASSERT(raw[2 + i] == fillVal);
        }
    });

	// When the VM is created, the physical memory should have been zero'ed.
	checkMem(0x0);

    // First fill: 0x00EF.
    X86Lab::Vm::State::Registers regs(vm->getRegisters());
    regs.rip = 0x0;
    regs.rax = 0x00EF;
    // RCX is the number of time to repeat the stosw, hence the number of word
    // to write.
    regs.rcx = numWordsToFill;
    // Start writing after the code.
    regs.rdi = codeSize;

	vm->setRegisters(regs);
    run();
    checkMem(regs.rax);

	// Second fill: 0xBE00.
    regs.rip = 0x0;
    regs.rax = 0xBE00;
    regs.rcx = numWordsToFill;
    regs.rdi = codeSize;

	vm->setRegisters(regs);
    run();
    checkMem(regs.rax);
}

// Test reading the MMX registers from Vm::getRegisters().
DECLARE_TEST(testReadMmxRegisters) {
    std::string const assembly(R"(
        BITS 64
        xor     rax, rax
        mov     rbx, 0xDEADBEEFCAFEBABE

        movq    mm0, rbx
        movq    mm0, rax

        movq    mm1, rbx
        movq    mm1, rax

        movq    mm2, rbx
        movq    mm2, rax

        movq    mm3, rbx
        movq    mm3, rax

        movq    mm4, rbx
        movq    mm4, rax

        movq    mm5, rbx
        movq    mm5, rax

        movq    mm6, rbx
        movq    mm6, rax

        movq    mm7, rbx
        movq    mm7, rax

        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    // Check the MMX register against the expected values.
    // @param mm0-mm7: The expected value of each MMX register.
    auto const checkRegs([&](u64 mm0, u64 mm1, u64 mm2, u64 mm3,
                             u64 mm4, u64 mm5, u64 mm6, u64 mm7) {
        X86Lab::Vm::State::Registers const regs(vm->getRegisters());
        TEST_ASSERT(regs.mm0 == mm0);
        TEST_ASSERT(regs.mm1 == mm1);
        TEST_ASSERT(regs.mm2 == mm2);
        TEST_ASSERT(regs.mm3 == mm3);
        TEST_ASSERT(regs.mm4 == mm4);
        TEST_ASSERT(regs.mm5 == mm5);
        TEST_ASSERT(regs.mm6 == mm6);
        TEST_ASSERT(regs.mm7 == mm7);
    });

    // Step the VM multiple times, asserting everytime that the VM remains
    // Runnable.
    // @param n: The number of steps to execute.
    auto const runVm([&](u64 const n) {
        for (u64 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Run the prelogue.
    runVm(2);

    u64 const v(0xDEADBEEFCAFEBABEULL);
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0);
    runVm(1);
    checkRegs(v, 0, 0, 0, 0, 0, 0, 0);
    runVm(2);
    checkRegs(0, v, 0, 0, 0, 0, 0, 0);
    runVm(2);
    checkRegs(0, 0, v, 0, 0, 0, 0, 0);
    runVm(2);
    checkRegs(0, 0, 0, v, 0, 0, 0, 0);
    runVm(2);
    checkRegs(0, 0, 0, 0, v, 0, 0, 0);
    runVm(2);
    checkRegs(0, 0, 0, 0, 0, v, 0, 0);
    runVm(2);
    checkRegs(0, 0, 0, 0, 0, 0, v, 0);
    runVm(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, v);
}

// Check that MMX is supported and properly initialized by running an MMX
// instruction.
DECLARE_TEST(testMmxInstruction) {
    // Simple packed word add with unsigned saturation.
    std::string const assembly(R"(
        BITS 64
        mov     rax, 0xBABECACAF00F1337
        mov     rbx, 0x1EAD1EEFCAFEBABE

        movq    mm0, rax
        movq    mm1, rbx

        paddusw mm0, mm1

        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    // Step the VM multiple times, asserting everytime that the VM remains
    // Runnable.
    // @param n: The number of steps to execute.
    auto const runVm([&](u64 const n) {
        for (u64 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    runVm(5);

    // The expected value is simply the word-wise add of the two values, clamped
    // to 0xFFFF.
    u64 const expected(0xD96BE9B9FFFFCDF5);

    u64 const mm0(vm->getRegisters().mm0);
    TEST_ASSERT(mm0 == expected);
}

DECLARE_TEST(testReadXmmRegisters) {
    std::string const assembly(R"(
        BITS 64
        
        ; Push two double-quadword onto the stack, to be loaded in xmm
        ; registers, one 0 (rsp + 16) and another with an arbitrary value (rsp).
        xor     rax, rax
        push    rax
        push    rax
        mov     rax, 0xDEADBEEFCAFEBABE
        push    rax
        mov     rax, 0xF00F1337CA7D0516
        push    rax

        movups  xmm0, [rsp] 
        movups  xmm0, [rsp + 16] 
        movups  xmm1, [rsp] 
        movups  xmm1, [rsp + 16] 
        movups  xmm2, [rsp] 
        movups  xmm2, [rsp + 16] 
        movups  xmm3, [rsp] 
        movups  xmm3, [rsp + 16] 
        movups  xmm4, [rsp] 
        movups  xmm4, [rsp + 16] 
        movups  xmm5, [rsp] 
        movups  xmm5, [rsp + 16] 
        movups  xmm6, [rsp] 
        movups  xmm6, [rsp + 16] 
        movups  xmm7, [rsp] 
        movups  xmm7, [rsp + 16] 
        movups  xmm8, [rsp] 
        movups  xmm8, [rsp + 16] 
        movups  xmm9, [rsp] 
        movups  xmm9, [rsp + 16] 
        movups  xmm10, [rsp] 
        movups  xmm10, [rsp + 16] 
        movups  xmm11, [rsp] 
        movups  xmm11, [rsp + 16] 
        movups  xmm12, [rsp] 
        movups  xmm12, [rsp + 16] 
        movups  xmm13, [rsp] 
        movups  xmm13, [rsp + 16] 
        movups  xmm14, [rsp] 
        movups  xmm14, [rsp + 16] 
        movups  xmm15, [rsp] 

        sub     rsp, 8
        stmxcsr [rsp]
        mov     eax, [rsp]
        xor     eax, (3 << 13)
        mov     [rsp], eax
        ldmxcsr [rsp]

        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    // Check the XMM registers against the expected values.
    // @param exp: The expected value of each XMM register.
    auto const checkRegs([&](u128 const * const expected) {
        X86Lab::Vm::State::Registers const regs(vm->getRegisters());
        for (u8 i(0); i < 16; ++i) {
            TEST_ASSERT(regs.xmm[i] == expected[i]);
        }
    });

    // Step the VM multiple times, asserting everytime that the VM remains
    // Runnable.
    // @param n: The number of steps to execute.
    auto const runVm([&](u64 const n) {
        for (u64 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    u32 const origMxcsr(vm->getRegisters().mxcsr);

    // Run the prelogue.
    runVm(8);

    // Need to construct the expected value manually since we cannot just put a
    // 128 constant in code.
    u128 v(0xDEADBEEFCAFEBABE);
    v <<= 64;
    v |= 0xF00F1337CA7D0516ULL;
    // Holds the expected values of each XMM register.
    u128 exp[16] = {0};
    for (u8 i(0); i < 16; ++i) {
        exp[i] = v;
        checkRegs(exp);
        // The register will be overwritten with 0 on the next instruction.
        exp[i] = 0;
        if (i < 15) {
            // Only run if we did not check the last register.
            runVm(2);
        }
    }

    // Run code modifying MXCSR. This toggles bits 13 and 14.
    runVm(6);
    u32 const expMxcsr(origMxcsr ^ (3 << 13));
    TEST_ASSERT(vm->getRegisters().mxcsr == expMxcsr);
}

DECLARE_TEST(testReadYmmRegisters) {
    std::string const assembly(R"(
        BITS 64

        ; Push two double-quadword onto the stack, to be loaded in xmm
        ; registers, one 0 (rsp + 32) and another with an arbitrary value (rsp).
        xor     rax, rax

        ; 256-bit word 0x0.
        push    rax
        push    rax
        push    rax
        push    rax

        ; 256-bit word
        ;   0xDEADBEEFCAFEBABEF00F1337CA7D0516ABCDEF0123456789F1E2D3C4B5A69788
        mov     rax, 0xDEADBEEFCAFEBABE
        push    rax
        mov     rax, 0xF00F1337CA7D0516
        push    rax
        mov     rax, 0xABCDEF0123456789
        push    rax
        mov     rax, 0xF1E2D3C4B5A69788
        push    rax

        vmovdqu ymm0, [rsp]
        vmovdqu ymm0, [rsp + 32]
        vmovdqu ymm1, [rsp]
        vmovdqu ymm1, [rsp + 32]
        vmovdqu ymm2, [rsp]
        vmovdqu ymm2, [rsp + 32]
        vmovdqu ymm3, [rsp]
        vmovdqu ymm3, [rsp + 32]
        vmovdqu ymm4, [rsp]
        vmovdqu ymm4, [rsp + 32]
        vmovdqu ymm5, [rsp]
        vmovdqu ymm5, [rsp + 32]
        vmovdqu ymm6, [rsp]
        vmovdqu ymm6, [rsp + 32]
        vmovdqu ymm7, [rsp]
        vmovdqu ymm7, [rsp + 32]
        vmovdqu ymm8, [rsp]
        vmovdqu ymm8, [rsp + 32]
        vmovdqu ymm9, [rsp]
        vmovdqu ymm9, [rsp + 32]
        vmovdqu ymm10, [rsp]
        vmovdqu ymm10, [rsp + 32]
        vmovdqu ymm11, [rsp]
        vmovdqu ymm11, [rsp + 32]
        vmovdqu ymm12, [rsp]
        vmovdqu ymm12, [rsp + 32]
        vmovdqu ymm13, [rsp]
        vmovdqu ymm13, [rsp + 32]
        vmovdqu ymm14, [rsp]
        vmovdqu ymm14, [rsp + 32]
        vmovdqu ymm15, [rsp]
        vmovdqu ymm15, [rsp + 32]

        nop
        hlt
    )");
    std::unique_ptr<X86Lab::Vm> const vm(
        createVmAndLoadCode(X86Lab::Vm::CpuMode::LongMode, assembly));

    // Step the VM multiple times, asserting everytime that the VM remains
    // Runnable.
    // @param n: The number of steps to execute.
    auto const runVm([&](u64 const n) {
        for (u64 i(0); i < n; ++i) {
            TEST_ASSERT(vm->step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Asserts the values of the YMM registers.
    // @param idx: The index of the YMM register which is expected to contain
    // the 256-bit word.
    // If only YMM_idx contains the word and every other YMM register is 0 then
    // the check is passing, otherwise this is an assert failure.
    auto const checkRegs([&](u8 const idx) {
        // The high and low 128-bit word of the 256-bit word written into the
        // YMM registers.
        u128 high(0xDEADBEEFCAFEBABEULL);
        high <<= 64;
        high |= 0xF00F1337CA7D0516ULL;
        u128 low(0xABCDEF0123456789ULL);
        low <<= 64;
        low |= 0xF1E2D3C4B5A69788ULL;
        X86Lab::Vm::State::Registers const regs(vm->getRegisters());
        for (u8 i(0); i < 16; ++i) {
            if (i == idx) {
                TEST_ASSERT(regs.ymm[i][0] == low);
                TEST_ASSERT(regs.ymm[i][1] == high);
            } else {
                TEST_ASSERT(!regs.ymm[i][0]);
                TEST_ASSERT(!regs.ymm[i][1]);
            }
        }
    });

    // Run prologue.
    runVm(13);

    for (u8 i(0); i < 16; ++i) {
        // Run the first vmovdqu setting the reg to the special value.
        runVm(1);
        checkRegs(i);
        runVm(1);
        // Run the second vmovdqu zero'ing the reg.
    }
}
}
