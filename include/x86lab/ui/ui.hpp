#pragma once
#include <x86lab/vm.hpp>
#include <x86lab/assembler.hpp>
#include <string>

// Gather logic around user-interface.
// UI implementation revolves around having two methods:
//  - Wait for user input
//  - Update the UI's content with the latest state of the VM.
// The "main loop" consists of waiting for the next action from the user, carry
// out the requested action, and update the UI.
namespace X86Lab::Ui {

// Possible actions from the user.
enum class Action {
    // Run the next instruction in the VM.
    Step,
    // Terminate the process.
    Quit,
};

// State represent anything that needs to be displayed on the UI implementation.
// This obviously contains the state of the VM, but can easily be extended to
// more information.
// Each action from the user creates a new state which is then passed to the UI
// implementation through the Backend::update() function (see below).
class State {
public:
    // Construct a State.
    // @param runState: The VM's runnable state.
    // @param code: The code that is currently loaded and running on the Vm.
    // @param registerValues: Pointer on a Vm::RegisterFile containing the
    // latest value for each registers of the VM. This pointer is not owned by
    // this class.
    State(Vm::State const runState,
          Assembler::Code const * const code, 
          Vm::RegisterFile const * const registerValues);

    // Get the VM's run state.
    Vm::State state() const;

    // Get the code loaded in the Vm.
    Assembler::Code const& code() const;

    // Get the values of the registers.
    Vm::RegisterFile const& registers() const;

private:
    Vm::State runState;
    Assembler::Code const * loadedCode;
    Vm::RegisterFile const * regValues;
};

// Backend implementation of the user interface. This is meant to be derived in
// a sub-class that actually implements the input/output routines.
class Backend {
public:
    // Virtual destructor to ensure calling derived destructors.
    virtual ~Backend() = 0;

    // Wait for the next action from the user.
    // @return: The action selected by the user.
    Action waitForNextAction();

    // Update the UI with the latest state of the VM.
    // @param newState: The latest state of the VM.
    void update(State const& newState);

    // Print a log message.
    // @param msg: The message to be printed.
    void log(std::string const& msg);

private:
    // Implementation of waitForNextAction, to be defined by sub-class.
    virtual Action doWaitForNextAction() = 0;

    // Implementation of update, to be defined by sub-class.
    virtual void doUpdate(State const& newState) = 0;

    // Implementation of log, to be defined by sub-class.
    virtual void doLog(std::string const& msg) = 0;
};
}
