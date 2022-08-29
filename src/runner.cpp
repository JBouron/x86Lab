#include <x86lab/runner.hpp>

namespace X86Lab {
Runner::Runner(std::shared_ptr<Vm> const vm,
               std::shared_ptr<Assembler::Code const> const code,
               std::shared_ptr<Ui::Backend> const ui) :
    vm(vm),
    code(code),
    ui(ui) {
    if (vm->operatingState() == Vm::OperatingState::NoCodeLoaded) {
        vm->loadCode(code->machineCode(), code->size());
    }

    // Setup the base snapshot.
    lastSnapshot = std::shared_ptr<Snapshot>(::new Snapshot(vm->getState()));
}

void Runner::run() {
    // Show the initial condition of the VM.
    updateUi();
    ui->log("Ready to run");
    // Termination condition in body.
    while (true) {
        Ui::Action const action(ui->waitForNextAction());
        if (action == Ui::Action::Quit) {
            // Termination condition.
            return;
        } else {
            processAction(action);
            updateUi();
        }
    }
}

void Runner::updateUi() {
    ui->update(Ui::State(vm->operatingState(), code, lastSnapshot));
}

void Runner::processAction(Ui::Action const action) {
    assert(action != Ui::Action::Quit);
    switch (action) {
        case Ui::Action::Step:
            doStep();
            break;
        default:
            break;
    }
}

void Runner::doStep() {
    if (vm->operatingState() != Vm::OperatingState::Runnable) {
        // The VM is no longer runnable, cannot satisfy the action.
        std::string reason;
        switch (vm->operatingState()) {
            case Vm::OperatingState::Shutdown:
                reason = "VM shutdown";
                break;
            case Vm::OperatingState::Halted:
                reason = "VM halted";
                break;
            case Vm::OperatingState::NoCodeLoaded:
                reason = "No code loaded";
                break;
            case Vm::OperatingState::SingleStepError:
                reason = "Single step error";
                break;
            default:
                reason = "Unknown";
                break;
        }
        ui->log("Vm no longer runnable, reason: " + reason);
    } else {
        vm->step();
        // Insert new snapshot and update lastSnapshot pointer.
        std::shared_ptr<Snapshot> const newSnapshot(
            ::new Snapshot(lastSnapshot, vm->getState()));
        lastSnapshot = newSnapshot;
    }
}
}
