#pragma once
#include <x86lab/vm.hpp>
#include <x86lab/ui/ui.hpp>
#include <vector>

namespace X86Lab {
// Implements the main-loop logic of the program: wait for user input, process
// next step, update UI.
class Runner {
public:
    // Instantiate a runner to work with a given Vm and run the given code.
    // @param vm: The vm to use for the program. If no code is loaded in the Vm
    // then this also takes care of loading the given code.
    // @param code: The code to run on the Vm.
    // @param ui: The UI to use as input/output.
    Runner(std::shared_ptr<Vm> const vm,
           std::shared_ptr<Code const> const code,
           std::shared_ptr<Ui::Backend> const ui);

    // Value returned by run() to indicate why the run() function returned.
    enum class ReturnReason {
        // User explicitly requested to exit the application.
        Quit,
        // User requested resetting the VM.
        Reset,
        // Reset the VM into 16-bit real mode.
        Reset16,
        // Reset the VM into 32-bit protected mode.
        Reset32,
        // Reset the VM into 64-bit protected mode.
        Reset64,
    };

    // Run the main-loop. This can only be called once! This function only
    // returns when this Runner is not longer runnable this happens when the
    // user requests exiting the application or when the VM needs reset, in
    // which case a new VM and new Runner instances must be created. See
    // ReturnReason for other reasons.
    // @return: The reason for the return.
    ReturnReason run();

private:
    std::shared_ptr<Vm> m_vm;
    std::shared_ptr<Code const> m_code;
    std::shared_ptr<Ui::Backend> m_ui;

    // The full execution history. This vector contains the snapshots of the Vm
    // after each instruction/step in-order. Entry i points to the snapshot
    // after executing the ith instruction. Entry 0 is the initial condition of
    // the VM.
    // This vector is used to implement reverse stepping in an efficient way.
    std::vector<std::shared_ptr<Snapshot>> m_history;

    // This is the index of the currently shown state in the history. If this
    // index == history.size() then this is the lastest state of the VM.
    u64 m_historyIndex;

    // Update the UI with the latest state of the VM.
    void updateUi();

    // Get a new snapshot of the VM state and update the lastSnapshot pointer.
    void updateLastSnapshot();

    // Process the next action.
    // @param action: The action to process.
    void processAction(Ui::Action const action);

    // Process an Action::Step request.
    void doStep();

    // Process an Action::ReverseStep request.
    void doReverseStep();
};
}
