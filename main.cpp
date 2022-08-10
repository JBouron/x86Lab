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

int main(int argc, char **argv) {
    demoWithDirectPM();
    return 0;
}
