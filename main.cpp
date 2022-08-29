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

// Start mode for the VM.
enum class Mode {
    RealMode,
    ProtectedMode,
    LongMode,
};

static void run(Mode const mode, std::string const& fileName) {
    // Run code in `fileName` starting directly in 64 bits mode.
    std::shared_ptr<Ui::Backend> ui(new Ui::Tui());

    // Assemble the code.
    ui->log("Assembling code in " + fileName);
    std::shared_ptr<Assembler::Code> const code(Assembler::assemble(fileName));
    ui->log("Assembled code is " + std::to_string(code->size()) + " bytes");

    // Create the VM and load the code in memory.
    std::shared_ptr<Vm> vm(new Vm(1));
    ui->log("Vm created");

    vm->loadCode(code->machineCode(), code->size());
    ui->log("Code loaded");

    // Enable the requested mode. Note that VMs start in real mode by default
    // hence nothing to do if mode == Mode::RealMode.
    if (mode == Mode::ProtectedMode) {
        ui->log("Enable protected mode on VM");
        vm->enableProtectedMode();
    } else if (mode == Mode::LongMode) {
        ui->log("Enable long mode on VM");
        vm->enable64BitsMode();
    } else {
        ui->log("Vm is using default 16 bit real mode");
    }

    Runner runner(vm, code, ui);
    runner.run();
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
    } catch (Error const& error) {
        std::string const msg(error.what());
        std::perror(("Error: " + msg).c_str());
        std::exit(1);
    }
    return 0;
}
