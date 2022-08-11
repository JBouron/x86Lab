#include <iostream>

#include "vm.hpp"

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

void demoWithDirect64BitMode() {
    // Shows how to start a VM directly in 64-bit mode.
    X86Lab::Vm vm(1);

    // mov rax, 0xAABBCCDDEEFF0011
    uint8_t const code[] = {
        0x48, 0xB8, 0x11, 0x00, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA
    };

    try {
        vm.loadCode(code, sizeof(code));
        vm.enable64BitsMode();

        std::cout << vm.getRegisters() << std::endl;
        while (vm.state() == X86Lab::Vm::State::Runnable) {
            vm.step();
            std::cout << vm.getRegisters() << std::endl;
        }
    } catch (X86Lab::Error const& error) {
        std::perror((std::string("X86Lab::Error: ")+error.what()).c_str());
        std::exit(1);
    }
}


int main(int argc, char **argv) {
    demoWithDirect64BitMode();
    return 0;
}
