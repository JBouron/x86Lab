#include <x86lab/vm.hpp>
#include <x86lab/assembler.hpp>
#include <cassert>
#include <fstream>

// Various tests for the X86Lab::Vm.

// Write some assembly code into a temporary file and return the name of the
// file.
static std::string writeCode(std::string const& code) {
    // For now we re-use the same file for all tests. We should however allocate
    // temporary files.
    std::string const fileName("/tmp/x86lab_testcode.S");
    std::ofstream file(fileName, std::ios::out);
    if (!file) {
        throw X86Lab::Error("Cannot open temporary file", errno);
    }

    file << code;
    file.close();
    return fileName;
}

namespace X86Lab::Test::Vm {
// The simplest test there is: Execute a few NOPs and make sure as well as
// RFALGS are as expected.
static void testReadRipAndRflags() {
    // Note: Each instruction is 1 byte long except for xor rax, rax which is 3
    // bytes.
    std::string const assembly(R"(
        BITS 64

        sti
        nop
        cli
        xor     rax, rax
    )");

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::LongMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    // Make sure that the VM starts executing the code with interrupts disabled.
    X86Lab::Vm::State::Registers regs(vm.getRegisters());
    assert(!(regs.rflags & (1 << 9)));
    u64 const codeStart(regs.rip);

    // Run sti.
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm.getRegisters();
    // Interrupts are expected to be enabled at that point.
    assert(regs.rflags & (1 << 9));
    assert(regs.rip == codeStart + 1);

    // Run nop.
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm.getRegisters();
    // Interrupts are still enabled.
    assert(regs.rflags & (1 << 9));
    assert(regs.rip == codeStart + 2);

    // Run cli.
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm.getRegisters();
    // Interrupts should be disabled.
    assert(!(regs.rflags & (1 << 9)));
    assert(regs.rip == codeStart + 3);

    // Run xor rax, rax.
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
    regs = vm.getRegisters();
    // Interrupts should still be disabled.
    assert(!(regs.rflags & (1 << 9)));
    assert(regs.rip == codeStart + 6);
    
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
}

// Test running a Vm in real mode, setting the GP registers to specific values
// and reading the register values at each step using getRegisters().
// Segment registers and other special registers are not tested here.
static void testRealMode() {
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

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::RealMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    // Check the values of the general purpose registers (r8-r15 are ignored
    // since we are running in 16 bits, those are expected to be zero).
    // @param rax, ..., rsp: The expected values of the GP registers.
    auto const checkRegs([&](u64 rax, u64 rbx, u64 rcx, u64 rdx,
                             u64 rdi, u64 rsi, u64 rbp, u64 rsp) {
        X86Lab::Vm::State::Registers const regs(vm.getRegisters());
        assert(regs.rax == rax); assert(regs.rbx == rbx);
        assert(regs.rcx == rcx); assert(regs.rdx == rdx);
        assert(regs.rdi == rdi); assert(regs.rsi == rsi);
        assert(regs.rbp == rbp); assert(regs.rsp == rsp);
        assert(!regs.r8);  assert(!regs.r9);  assert(!regs.r10);
        assert(!regs.r11); assert(!regs.r12); assert(!regs.r13);
        assert(!regs.r14); assert(!regs.r15);
        // Special registers are not checked.
    });

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Initially all registers are expected to be zeroed.
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0);

    // Run the first instruction setting ax.
    runNSteps(1);

    // The expected value.
    u64 const val(0xABCD);
    checkRegs(val, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, val, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, val, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, val, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, val, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, val, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, val, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, val);
}

// Test running a Vm in protected mode, setting the GP registers to specific
// values and reading the register values at each step using getRegisters().
// Segment registers and other special registers are not tested here.
static void testProtectedMode() {
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

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::ProtectedMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    // Check the values of the general purpose registers (r8-r15 are ignored
    // since we are running in 32 bits, those are expected to be zero).
    // @param rax, ..., rsp: The expected values of the GP registers.
    auto const checkRegs([&](u64 rax, u64 rbx, u64 rcx, u64 rdx,
                             u64 rdi, u64 rsi, u64 rbp, u64 rsp) {
        X86Lab::Vm::State::Registers const regs(vm.getRegisters());
        assert(regs.rax == rax); assert(regs.rbx == rbx);
        assert(regs.rcx == rcx); assert(regs.rdx == rdx);
        assert(regs.rdi == rdi); assert(regs.rsi == rsi);
        assert(regs.rbp == rbp); assert(regs.rsp == rsp);
        assert(!regs.r8);  assert(!regs.r9);  assert(!regs.r10);
        assert(!regs.r11); assert(!regs.r12); assert(!regs.r13);
        assert(!regs.r14); assert(!regs.r15);
        // Special registers are not checked.
    });

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Initially all registers are expected to be zeroed.
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0);

    // Run the first instruction setting ax.
    runNSteps(1);

    // The expected value.
    u64 const val(0xABCD1234);
    checkRegs(val, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, val, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, val, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, val, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, val, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, val, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, val, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, 0, val);
}

// Test running a Vm in long mode, setting the GP registers to specific values
// and reading the register values at each step using getRegisters().  Segment
// registers and other special registers are not tested here.
static void testLongMode() {
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

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::LongMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    // Check the values of the general purpose registers
    // @param rax, ..., r15: The expected values of the GP registers.
    auto const checkRegs([&](u64 rax, u64 rbx, u64 rcx, u64 rdx,
                             u64 rdi, u64 rsi, u64 rbp, u64 rsp,
                             u64 r8,  u64 r9,  u64 r10, u64 r11,
                             u64 r12, u64 r13, u64 r14, u64 r15) {
        X86Lab::Vm::State::Registers const regs(vm.getRegisters());
        assert(regs.rax == rax); assert(regs.rbx == rbx);
        assert(regs.rcx == rcx); assert(regs.rdx == rdx);
        assert(regs.rdi == rdi); assert(regs.rsi == rsi);
        assert(regs.rbp == rbp); assert(regs.rsp == rsp);
        assert(regs.r8 == r8);   assert(regs.r9 == r9);
        assert(regs.r10 == r10); assert(regs.r11 == r11);
        assert(regs.r12 == r12); assert(regs.r13 == r13);
        assert(regs.r14 == r14); assert(regs.r15 == r15);
        // Special registers are not checked.
    });

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Initially all registers are expected to be zeroed.
    checkRegs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    // Run the first instruction setting ax.
    runNSteps(1);

    // The expected value.
    u64 const val(0xABCDEF1234567890);
    checkRegs(val, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, val, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, val, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, val, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, val, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, val, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    runNSteps(2);
    checkRegs(0, 0, 0, 0, 0, 0, val, 0, 0, 0, 0, 0, 0, 0, 0, 0);
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
static void testReadSegmentRegisters() {
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
    // Hence, if we are to call vm.step() on the "mov  ss, ax", we would in fact
    // run two instrutions. Without the nop this would run the mov and the hlt,
    // leaving the VM in a non-runnable operatingState.

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::RealMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    // Run the VM for N steps, asserting at each step that the Vm is still
    // runnable.
    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });

    // Run first instruction, this sets CS to 0x1.
    runNSteps(1);
    assert(vm.getRegisters().cs == 0x1);
    runNSteps(2);
    assert(vm.getRegisters().ds == 0xDDDD);
    runNSteps(2);
    assert(vm.getRegisters().es == 0xEEEE);
    runNSteps(2);
    assert(vm.getRegisters().fs == 0xFFFF);
    runNSteps(2);
    assert(vm.getRegisters().gs == 0x1111);
    runNSteps(2);
    assert(vm.getRegisters().ss == 0x2222);
}

// Check that getRegisters returns the correct values for GDTR and IDTR.
static void testReadGdtIdt() {
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

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::LongMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    // Run the lgdt and lidt instructions.
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);

    // Check the reported values.
    X86Lab::Vm::State::Registers const regs(vm.getRegisters());
    assert(regs.gdt.base == 0xFFFFFFF8CAFEBABE);
    assert(regs.gdt.limit == 0x8887);
    assert(regs.idt.base == 0xFFFFFFF8ABCDEF12);
    assert(regs.idt.limit == 0xABC7);
}

// Check that getRegisters() reads the correct values of all control registers.
static void testReadControlRegisters() {
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

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::LongMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    X86Lab::Vm::State::Registers const prev(vm.getRegisters());

    auto const runNSteps([&](u32 const n) {
        for (u32 i(0); i < n; ++i) {
            assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);
        }
    });
    // Execute all 19 instructions (not the hlt).
    runNSteps(19);

    // Check the new values of the control regs.
    X86Lab::Vm::State::Registers const regs(vm.getRegisters());
    assert(regs.cr0 == (prev.cr0 ^ ((1 << 30) | (1 << 29))));
    assert(regs.cr2 == 0xDEADBEEFCAFEBABE);
    assert(regs.cr3 == (prev.cr3 ^ (1 << 3)));
    assert(regs.cr4 == (prev.cr4 ^ (1 << 2)));
    assert(regs.cr8 == (prev.cr8 ^ 0xF));
    assert(regs.efer == (prev.efer ^ (1 << 11)));
}

// Check that Vm::setRegisters() actually set the registers to their correct
// value. For now all registers (GP, control registers, GDTR and IDTR) are
// tested. Segment registers are not tested here since changing their value is
// not yet fully specified (FIXME).
// This test assumes that Vm::getRegisters() works as intended an returns the
// correct value of all registers (getRegisters is tested above).
static void testSetRegisters() {
    // A single instruction that 
    std::string const assembly(R"(
        BITS 64
        dq  0x0
        ; The instruction expected to be pointed to after setting rip using
        ; setRegisters.
        nop
        hlt
    )");

    std::string const fileName(writeCode(assembly));
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));

    X86Lab::Vm vm(X86Lab::Vm::CpuMode::LongMode, 1);
    vm.loadCode(code->machineCode(), code->size());

    X86Lab::Vm::State::Registers expected(vm.getRegisters());

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

    // Set the registers on the VM.
    vm.setRegisters(expected);

    // Run the single nop instruction.
    assert(vm.step() == X86Lab::Vm::OperatingState::Runnable);

    X86Lab::Vm::State::Registers current(vm.getRegisters());
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
    assert(current == expected);
}

// Run all the tests under this namespace.
void run() {
    testReadRipAndRflags();
    testRealMode();
    testProtectedMode();
    testLongMode();
    testReadSegmentRegisters();
    testReadGdtIdt();
    testReadControlRegisters();
    testSetRegisters();
}
}