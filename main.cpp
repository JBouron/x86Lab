#include <iostream>

#include "vm.hpp"
#include "assembler.hpp"

static void help() {
    std::cerr << "X86Lab: A x86 instruction analyzer" << std::endl;
    std::cerr << "Usage:" << std::endl;
    std::cerr << "    x86lab [options] <file>" << std::endl << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "    --16   Execute the code in 16 bit real mode" << std::endl;
    std::cerr << "    --32   Execute the code in 32 bit" << std::endl;
    std::cerr << "    --64   Execute the code in 64 bit, default" << std::endl;
    std::cerr << "    --help This message" << std::endl;
    std::cerr << "<file> is a file path to an assembly file that must be "
        "compatible with the NASM assembler. Any NASM directive within this "
        "file is valid and accepted" << std::endl;
}

// Start mode for the VM.
enum class Mode {
    RealMode,
    ProtectedMode,
    LongMode,
};

static void run(Mode const mode, std::string const& fileName) {
    // Run code in `fileName` starting directly in 64 bits mode.

    // Assemble the code.
    X86Lab::Assembler::Code const code(X86Lab::Assembler::assemble(fileName));

    // Create the VM and load the code in memory.
    X86Lab::Vm vm(1);
    vm.loadCode(code.machineCode(), code.size());

    // Enable the requested mode. Note that VMs start in real mode by default
    // hence nothing to do if mode == Mode::RealMode.
    if (mode == Mode::ProtectedMode) {
        vm.enableProtectedMode();
    } else if (mode == Mode::LongMode) {
        vm.enable64BitsMode();
    }

    X86Lab::Assembler::InstructionMap const& map(code.getInstructionMap());
    // Print the current state of the registers and the next instruction to be
    // executed.
    auto const printRegsAndInstruction([&]() {
        X86Lab::Vm::RegisterFile const regs(vm.getRegisters());
        std::cout << regs << std::endl;
        auto const entry(map.mapInstructionPointer(regs.rip));
        if (!!entry) {
            std::cout << std::dec;
            std::cout << "Line        = " << entry.line << std::endl;
            std::cout << "Next instr. = " << entry.instruction << std::endl;
        }
    });

    // Run step by step.
    printRegsAndInstruction();
    while (vm.state() == X86Lab::Vm::State::Runnable) {
        std::cout << "--------------------------------------------------------";
        std::cout << std::endl;
        vm.step();
        printRegsAndInstruction();
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Error, not enough arguments" << std::endl;
        help();
        std::exit(1);
    }

    Mode startMode(Mode::LongMode);
    for (u32 i(1); i < argc - 1; ++i) {
        std::string const arg(argv[i]);
        if (arg == "--16") {
            startMode = Mode::RealMode;
        } else if (arg == "--32") {
            startMode = Mode::ProtectedMode;
        } else if (arg == "--64") {
            startMode = Mode::LongMode;
        } else if (arg == "--help") {
            help();
            std::exit(0);
        } else {
            std::cerr << "Error, invalid argument " << arg << std::endl;
            help();
            std::exit(0);
        }
    }

    std::string const fileName(argv[argc - 1]);

    try {
        run(startMode, fileName);
    } catch (X86Lab::Error const& error) {
        std::string const msg(error.what());
        std::perror(("Error: " + msg).c_str());
        std::exit(1);
    }
    return 0;
}
