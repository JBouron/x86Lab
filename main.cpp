#include <iostream>

#include "vm.hpp"
#include "assembler.hpp"

void demoWithDirectPM() {
    // Shows how to start a VM directly in 32-bit protected mode.
    X86Lab::Vm vm(1);

    // mov eax, 0xdeadbeef
    // ror eax, 16
    // hlt
    uint8_t const code[] = {0xB8, 0xEF, 0xBE, 0xAD, 0xDE, 0xC1, 0xC8, 0x10, 0xF4};

    try {
        vm.loadCode(code, sizeof(code));
        vm.enableProtectedMode();

        std::cout << vm.getRegisters() << std::endl;
        while (vm.state() == X86Lab::Vm::State::Runnable) {
            vm.step();
            std::cout << vm.getRegisters() << std::endl;
        }
    } catch (X86Lab::Error const& error) {
        std::perror("X86Lab::Error");
        std::exit(1);
    }
}

void run64Bits(std::string const& fileName) {
    // Run code in `fileName` starting directly in 64 bits mode.
    X86Lab::Assembler::Code const code(X86Lab::Assembler::assemble(fileName));
    X86Lab::Vm vm(1);
    vm.loadCode(code.machineCode(), code.size());
    vm.enable64BitsMode();

    std::cout << vm.getRegisters() << std::endl;
    while (vm.state() == X86Lab::Vm::State::Runnable) {
        vm.step();
        std::cout << vm.getRegisters() << std::endl;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Not enough arguments, expected path to code" << std::endl;
        std::exit(1);
    }

    std::string const fileName(argv[1]);

    try {
        run64Bits(fileName);
    } catch (X86Lab::Error const& error) {
        std::string const msg(error.what());
        std::perror(("Error: " + msg).c_str());
        std::exit(1);
    }
    return 0;
}
