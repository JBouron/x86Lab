#include <x86lab/vm.hpp>
#include <x86lab/assembler.hpp>
#include <x86lab/util.hpp>
#include <stdlib.h>
#include <fstream>

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

// Sets all the general purpose registers on a VM before starting it. Run some
// code that modifies each register, and read their value back once the
// execution is done. Assert on the read values.
static void testSetRegisters() {
    std::string const assembly(R"(BITS 64
shl rax, 0x8
shl rbx, 0x8
shl rcx, 0x8
shl rdx, 0x8
shl rdi, 0x8
shl rsi, 0x8
shl rbp, 0x8
shl rsp, 0x8
shl r8,  0x8
shl r9,  0x8
shl r10, 0x8
shl r11, 0x8
shl r12, 0x8
shl r13, 0x8
shl r14, 0x8
shl r15, 0x8
cli
hlt)");
    std::string const fileName(writeCode(assembly));
    X86Lab::Assembler::Code const code(X86Lab::Assembler::assemble(fileName));

    // Create the VM and load the code in memory.
    X86Lab::Vm vm(1);
    vm.loadCode(code.machineCode(), code.size());
    vm.enable64BitsMode();

    // Set the initial values of the registers.
    X86Lab::Vm::RegisterFile regs(vm.getRegisters());
    regs.rax = 0x00AAAAAAAAAAAAAAULL;
    regs.rbx = 0x00BBBBBBBBBBBBBBULL;
    regs.rcx = 0x00CCCCCCCCCCCCCCULL;
    regs.rdx = 0x00DDDDDDDDDDDDDDULL;
    regs.rdi = 0x00EEEEEEEEEEEEEEULL;
    regs.rsi = 0x00FFFFFFFFFFFFFFULL;
    regs.rbp = 0x0011111111111111ULL;
    regs.rsp = 0x0022222222222222ULL;
    regs.r8  = 0x0088888888888888ULL;
    regs.r9  = 0x0099999999999999ULL;
    regs.r10 = 0x0010101010101010ULL;
    regs.r11 = 0x0011001100110011ULL;
    regs.r12 = 0x0012121212121212ULL;
    regs.r13 = 0x0013131313131313ULL;
    regs.r14 = 0x0014141414141414ULL;
    regs.r15 = 0x0015151515151515ULL;
    // For EFLAGS, set only the interrupt bit. The assembly code will disable
    // it. Note: Bit 1 is hardcoded to 1 per x86 spec.
    regs.rflags = (1 << 9) | (1 << 1);
    vm.setRegisters(regs);

    // Check that the registers have been set as expected.
    regs = vm.getRegisters();
    assert(regs.rax == 0x00AAAAAAAAAAAAAAULL);
    assert(regs.rbx == 0x00BBBBBBBBBBBBBBULL);
    assert(regs.rcx == 0x00CCCCCCCCCCCCCCULL);
    assert(regs.rdx == 0x00DDDDDDDDDDDDDDULL);
    assert(regs.rdi == 0x00EEEEEEEEEEEEEEULL);
    assert(regs.rsi == 0x00FFFFFFFFFFFFFFULL);
    assert(regs.rbp == 0x0011111111111111ULL);
    assert(regs.rsp == 0x0022222222222222ULL);
    assert(regs.r8  == 0x0088888888888888ULL);
    assert(regs.r9  == 0x0099999999999999ULL);
    assert(regs.r10 == 0x0010101010101010ULL);
    assert(regs.r11 == 0x0011001100110011ULL);
    assert(regs.r12 == 0x0012121212121212ULL);
    assert(regs.r13 == 0x0013131313131313ULL);
    assert(regs.r14 == 0x0014141414141414ULL);
    assert(regs.r15 == 0x0015151515151515ULL);
    assert(regs.rflags == (1 << 9) | (1 << 1));

    // The RIP must point to address 0.
    assert(regs.rip == 0x0);

    // Now run the whole code.
    while (vm.state() == X86Lab::Vm::State::Runnable) {
        vm.step();
    }

    // Check the values of the registers, they should have been shifted left.
    regs = vm.getRegisters();
    assert(regs.rax == 0xAAAAAAAAAAAAAA00ULL);
    assert(regs.rbx == 0xBBBBBBBBBBBBBB00ULL);
    assert(regs.rcx == 0xCCCCCCCCCCCCCC00ULL);
    assert(regs.rdx == 0xDDDDDDDDDDDDDD00ULL);
    assert(regs.rdi == 0xEEEEEEEEEEEEEE00ULL);
    assert(regs.rsi == 0xFFFFFFFFFFFFFF00ULL);
    assert(regs.rbp == 0x1111111111111100ULL);
    assert(regs.rsp == 0x2222222222222200ULL);
    assert(regs.r8  == 0x8888888888888800ULL);
    assert(regs.r9  == 0x9999999999999900ULL);
    assert(regs.r10 == 0x1010101010101000ULL);
    assert(regs.r11 == 0x1100110011001100ULL);
    assert(regs.r12 == 0x1212121212121200ULL);
    assert(regs.r13 == 0x1313131313131300ULL);
    assert(regs.r14 == 0x1414141414141400ULL);
    assert(regs.r15 == 0x1515151515151500ULL);
    // Interrupts should have beed disabled hence bit 9 should be un-set. The
    // last instruction "shl r15, 8" Should have set the parity flag (bit 2).
    assert(regs.rflags == (1 << 2) | (1 << 1));

    // RIP must point after the last instruction (HLT).
    assert(regs.rip == code.size());
}


// All the tests to be ran.
using TestFunction = void (*)();
std::vector<TestFunction> const tests({
    testSetRegisters,
});

int main(int argc, char **argv) {
    for (TestFunction f : tests) {
        f();
    }
    return 0;
}