#pragma once
#include <x86lab/vm.hpp>
#include <x86lab/ui/ui.hpp>

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
           std::shared_ptr<Assembler::Code const> const code,
           std::shared_ptr<Ui::Backend> const ui);

    // Run the main-loop. This function only returns once the user has requested
    // to exit the program.
    void run();

private:
    std::shared_ptr<Vm> vm;
    std::shared_ptr<Assembler::Code const> code;
    std::shared_ptr<Ui::Backend> ui;

    // Points to the lastest snapshot of the VM. Initially the initial state of
    // the VM.
    std::shared_ptr<X86Lab::Snapshot> lastSnapshot;

    // Update the UI with the latest state of the VM.
    void updateUi();

    // Process the next action.
    // @param action: The action to process.
    void processAction(Ui::Action const action);

    // Process an Action::Step request.
    void doStep();
};
}
