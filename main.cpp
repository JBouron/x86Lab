#include <iostream>

#include <x86lab/vm.hpp>
#include <x86lab/assembler.hpp>
#include <x86lab/ui/cli.hpp>
#include <x86lab/ui/tui.hpp>
#include <x86lab/ui/imgui.hpp>
#include <x86lab/runner.hpp>
#include <filesystem>

using namespace X86Lab;

static void help() {
    std::cerr << "X86Lab: A playground for x86 assembly programming." <<
        std::endl;
    std::cerr << "Usage:" << std::endl;
    std::cerr << "    x86lab [options] <file>" << std::endl << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "    --help This message" << std::endl;
    std::cerr << "<file> is a file path to an assembly file that must be "
        "compatible with the NASM assembler. Any NASM directive within this "
        "file is valid and accepted" << std::endl;
}

static void run(std::string const& fileName) {
    // Run code in `fileName` starting directly in 64 bits mode.
    std::shared_ptr<Ui::Backend> ui(new Ui::Imgui());

    // Initilize UI.
    if (!ui->init()) {
        throw X86Lab::Error("Cannot initialize UI", 0);
    }

    // Assemble the code.
    ui->log("Assembling code in " + fileName);
    std::shared_ptr<Code> const code(new Code(fileName));
    ui->log("Assembled code is " + std::to_string(code->size()) + " bytes");

    // Create the VM and load the code in memory.

    bool exitRequested(false);
    // By default the VM starts in 64-bit long mode. This can be changed through
    // the interface. Changing the start CPU mode resets the VM.
    // FIXME: To avoid any issue when running the example code, hardcode the
    // start mode to be 16 bit when running the example. This is a horrendous
    // hack.
    bool const runningDemo(std::filesystem::path(fileName).filename()==
                           "jumpToProtectedAndLongModes.asm");
    Vm::CpuMode startCpuMode(runningDemo?
                             Vm::CpuMode::RealMode:Vm::CpuMode::LongMode);
    while (!exitRequested) {
        // When the user requests resetting the VM we simply destroy the VM and
        // re-create it from scratch. This is much easier compared to manually
        // resetting the state of the CPU and memory.

        // FIXME: We need a way to specify the size of the VM.
        std::shared_ptr<Vm> vm(new Vm(startCpuMode, 4 * X86Lab::PAGE_SIZE));

        vm->loadCode(*code);
        ui->log("Code loaded");

        // Runner instances are a bit ephemeral, as soon as their run() return
        // they cannot be used anymore.
        Runner runner(vm, code, ui);
        Runner::ReturnReason const retReason(runner.run());

        if (retReason == Runner::ReturnReason::Quit) {
            exitRequested = true;
        } else if (retReason == Runner::ReturnReason::Reset16) {
            startCpuMode = Vm::CpuMode::RealMode;
        } else if (retReason == Runner::ReturnReason::Reset32) {
            startCpuMode = Vm::CpuMode::ProtectedMode;
        } else if (retReason == Runner::ReturnReason::Reset64) {
            startCpuMode = Vm::CpuMode::LongMode;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Error, not enough arguments" << std::endl;
        help();
        std::exit(1);
    }

    for (int i(1); i < argc - 1; ++i) {
        std::string const arg(argv[i]);
        if (arg == "--help") {
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
        run(fileName);
    } catch (Error const& error) {
        std::string const msg(error.what());
        std::perror(("Error: " + msg).c_str());
        std::exit(1);
    }
    return 0;
}
