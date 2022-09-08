#include <iostream>

#include <x86lab/vm.hpp>
#include <x86lab/assembler.hpp>
#include <x86lab/ui/cli.hpp>
#include <x86lab/ui/tui.hpp>
#include <x86lab/runner.hpp>

using namespace X86Lab;

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

static void run(Vm::CpuMode const vmStartMode, std::string const& fileName) {
    // Run code in `fileName` starting directly in 64 bits mode.
    std::shared_ptr<Ui::Backend> ui(new Ui::Tui());

    // Assemble the code.
    ui->log("Assembling code in " + fileName);
    std::shared_ptr<Assembler::Code> const code(new Assembler::Code(fileName));
    ui->log("Assembled code is " + std::to_string(code->size()) + " bytes");

    // Create the VM and load the code in memory.

    std::shared_ptr<Vm> vm(new Vm(vmStartMode, 1));
    ui->log("Vm created");
    if (vmStartMode == Vm::CpuMode::ProtectedMode) {
        ui->log("VM is using 32-bit protected-mode");
    } else if (vmStartMode == Vm::CpuMode::LongMode) {
        ui->log("VM is using 64-bit long-mode");
    } else {
        ui->log("Vm is using 16-bit real-mode");
    }

    vm->loadCode(code->machineCode(), code->size());
    ui->log("Code loaded");


    Runner runner(vm, code, ui);
    runner.run();
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Error, not enough arguments" << std::endl;
        help();
        std::exit(1);
    }

    // By default use long-mode on the VM.
    Vm::CpuMode startMode(Vm::CpuMode::LongMode);
    for (int i(1); i < argc - 1; ++i) {
        std::string const arg(argv[i]);
        if (arg == "--16") {
            startMode = Vm::CpuMode::RealMode;
        } else if (arg == "--32") {
            startMode = Vm::CpuMode::ProtectedMode;
        } else if (arg == "--64") {
            startMode = Vm::CpuMode::LongMode;
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
    } catch (Error const& error) {
        std::string const msg(error.what());
        std::perror(("Error: " + msg).c_str());
        std::exit(1);
    }
    return 0;
}
