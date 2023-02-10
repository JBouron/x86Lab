#include <x86lab/runner.hpp>

namespace X86Lab {
Runner::Runner(std::shared_ptr<Vm> const vm,
               std::shared_ptr<Code const> const code,
               std::shared_ptr<Ui::Backend> const ui) :
    m_vm(vm),
    m_code(code),
    m_ui(ui),
    m_historyIndex(0) {
    if (m_vm->operatingState() == Vm::OperatingState::NoCodeLoaded) {
        m_vm->loadCode(*m_code);
    }

    // Setup the base snapshot.
    m_history.push_back(
        std::shared_ptr<Snapshot>(new Snapshot(m_vm->getState())));
}

Runner::ReturnReason Runner::run() {
    // Show the initial condition of the VM.
    updateUi();
    m_ui->log("Ready to run");
    // Termination condition in body.
    while (true) {
        Ui::Action const action(m_ui->waitForNextAction());
        if (action == Ui::Action::Quit) {
            // Termination condition.
            return ReturnReason::Quit;
        } else if (action == Ui::Action::Reset) {
            // User requested resetting the VM. In this case the Runner let's
            // the caller (e.g. main()) takes care of this. At this point this
            // runner instance is done running and ready to be destroyed.
            return ReturnReason::ResetVm;
        } else {
            processAction(action);
            updateUi();
        }
    }
}

void Runner::updateUi() {
    assert(m_historyIndex < m_history.size());
    m_ui->update(Ui::State(m_vm->operatingState(),
                           m_code,
                           m_history[m_historyIndex]));
}

void Runner::updateLastSnapshot() {
    // Adding a new snapshot can only be done if we are running the vm, eg. not
    // looking at an old state.
    assert(m_historyIndex == m_history.size() - 1);
    std::shared_ptr<Snapshot> const nextSnapshot(
        ::new Snapshot(m_history[m_historyIndex], m_vm->getState()));
    m_history.push_back(nextSnapshot);
    m_historyIndex ++;
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
            // This includes Action::None.
            break;
    }
}

void Runner::doStep() {
    if (m_vm->operatingState() != Vm::OperatingState::Runnable) {
        // The VM is no longer runnable, cannot satisfy the action.
        std::string reason;
        switch (m_vm->operatingState()) {
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
        m_ui->log("Vm no longer runnable, reason: " + reason);
    } else if (m_historyIndex != m_history.size() - 1) {
        // We are not on the latest state but instead are looking at an old
        // state back in time. Stepping is merely done as incrementing the
        // m_historyIndex.
        m_historyIndex ++;
    } else {
        // We are looking at the latest state of the VM, going to the next state
        // requires actually executing the next instruction.
        m_vm->step();
        updateLastSnapshot();
    }
}

void Runner::doReverseStep() {
    if (!!m_historyIndex) {
        m_historyIndex --;
    }
}
}
