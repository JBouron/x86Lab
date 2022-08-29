#include <iostream>

#include <x86lab/vm.hpp>
#include <x86lab/assembler.hpp>
#include <x86lab/ui/cli.hpp>
#include <x86lab/ui/tui.hpp>

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
    X86Lab::Ui::Backend * ui(new X86Lab::Ui::Tui());

    // Assemble the code.
    ui->log("Assembling code in " + fileName);
    std::shared_ptr<X86Lab::Assembler::Code> const code(
        X86Lab::Assembler::assemble(fileName));
    ui->log("Assembled code is " + std::to_string(code->size()) + " bytes");

    // Create the VM and load the code in memory.
    X86Lab::Vm vm(1);
    ui->log("Vm created");

    vm.loadCode(code->machineCode(), code->size());
    ui->log("Code loaded");

    // Enable the requested mode. Note that VMs start in real mode by default
    // hence nothing to do if mode == Mode::RealMode.
    if (mode == Mode::ProtectedMode) {
        ui->log("Enable protected mode on VM");
        vm.enableProtectedMode();
    } else if (mode == Mode::LongMode) {
        ui->log("Enable long mode on VM");
        vm.enable64BitsMode();
    } else {
        ui->log("Vm is using default 16 bit real mode");
    }

    // Create the base snapshot.
    std::shared_ptr<X86Lab::Snapshot> latestSnapshot(
        ::new X86Lab::Snapshot(vm.getState()));

    auto const updateUi([&]() {
        X86Lab::Ui::State const s(vm.operatingState(), code, latestSnapshot);
        ui->update(s);
    });

    updateUi();

    // Number of steps executed so far.
    u64 numSteps(0);

    X86Lab::Ui::Action nextAction(ui->waitForNextAction());
    while (nextAction != X86Lab::Ui::Action::Quit) {
        if (nextAction == X86Lab::Ui::Action::Step) {
            if (vm.operatingState() != X86Lab::Vm::OperatingState::Runnable) {
                // The VM is no longer runnable, cannot satisfy the action.
                std::string reason;
                switch (vm.operatingState()) {
                    case X86Lab::Vm::OperatingState::Shutdown:
                        reason = "VM shutdown";
                        break;
                    case X86Lab::Vm::OperatingState::Halted:
                        reason = "VM halted";
                        break;
                    case X86Lab::Vm::OperatingState::NoCodeLoaded:
                        reason = "No code loaded";
                        break;
                    case X86Lab::Vm::OperatingState::SingleStepError:
                        reason = "Single step error";
                        break;
                    default:
                        reason = "Unknown";
                        break;
                }
                ui->log("Vm no longer runnable, reason: " + reason);
            } else {
                numSteps ++;
                vm.step();

                // Build a snapshot on top of the previous one.
                std::shared_ptr<X86Lab::Snapshot> const newSnapshot(
                    ::new X86Lab::Snapshot(latestSnapshot, vm.getState()));
                latestSnapshot = newSnapshot;

                updateUi();
            }
        }

        nextAction = ui->waitForNextAction();
    }
    ui->log("Reached end of execution after "
            + std::to_string(numSteps) + " instructions");
    delete ui;
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
