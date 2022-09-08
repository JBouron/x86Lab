#include <x86lab/runner.hpp>

namespace X86Lab {
Runner::Runner(std::shared_ptr<Vm> const vm,
               std::shared_ptr<Code const> const code,
               std::shared_ptr<Ui::Backend> const ui) :
    vm(vm),
    code(code),
    ui(ui),
    historyIndex(0) {
    if (vm->operatingState() == Vm::OperatingState::NoCodeLoaded) {
        vm->loadCode(code->machineCode(), code->size());
    }

    // Setup the base snapshot.
    history.push_back(std::shared_ptr<Snapshot>(new Snapshot(vm->getState())));
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
    assert(historyIndex < history.size());
    ui->update(Ui::State(vm->operatingState(), code, history[historyIndex]));
}

void Runner::updateLastSnapshot() {
    // Adding a new snapshot can only be done if we are running the vm, eg. not
    // looking at an old state.
    assert(historyIndex == history.size() - 1);
    std::shared_ptr<Snapshot> const nextSnapshot(
        ::new Snapshot(history[historyIndex], vm->getState()));
    history.push_back(nextSnapshot);
    historyIndex ++;
}

void Runner::processAction(Ui::Action const action) {
    assert(action != Ui::Action::Quit);
    switch (action) {
        case Ui::Action::Step:
            doStep();
            break;
        case Ui::Action::ReverseStep:
            doReverseStep();
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
    } else if (historyIndex != history.size() - 1) {
        // We are not on the latest state but instead are looking at an old
        // state back in time. Stepping is merely done as incrementing the
        // historyIndex.
        historyIndex ++;
    } else {
        // We are looking at the latest state of the VM, going to the next state
        // requires actually executing the next instruction.
        vm->step();
        updateLastSnapshot();
    }
}

void Runner::doReverseStep() {
    if (!!historyIndex) {
        historyIndex --;
    }
}
}
