#pragma once
#include <x86lab/vm.hpp>
#include <x86lab/code.hpp>
#include <x86lab/snapshot.hpp>
#include <string>
#include <memory>

// Gather logic around user-interface.
// UI implementation revolves around having two methods:
//  - Wait for user input
//  - Update the UI's content with the latest state of the VM.
// The "main loop" consists of waiting for the next action from the user, carry
// out the requested action, and update the UI.
namespace X86Lab::Ui {

// Possible actions from the user.
enum class Action {
    // No action.
    None,
    // Run the next instruction in the VM.
    Step,
    // Go backward one instruction in the execution flow.
    ReverseStep,
    // Terminate the process.
    Quit,
    // Reset the VM.
    Reset,
};

// State represent anything that needs to be displayed on the UI implementation.
// This obviously contains the state of the VM, but can easily be extended to
// more information.
// Each action from the user creates a new state which is then passed to the UI
// implementation through the Backend::update() function (see below).
class State {
public:
    // Default, empty state.
    State() = default;

    // Construct a State.
    // @param runState: The VM's runnable state.
    // @param code: The code that is currently loaded and running on the Vm.
    // @param snapshot: The latest snapshot of the VM.
    State(Vm::OperatingState const runState,
          std::shared_ptr<Code const> const code, 
          std::shared_ptr<Snapshot const> const snapshot);

    // @return: true if the VM is runnable, false otherwise.
    bool isVmRunnable() const;

    // Get the name of the source file.
    // @return: The path to the source file.
    std::string const& sourceFileName() const;

    // Get the line number associated to the current rip. This is equivalent to
    // mapToLine(registers().rip).
    // @return: The line number corresponding to the instruction pointed by rip.
    // If rip cannot be mapped then this function returns 0.
    u64 currentLine() const;

    // Map an address to a line number in the source file.
    // @return: If the address can be mapped to a line number then this line is
    // returned, otherwise 0 is returned.
    u64 mapToLine(u64 const address) const;

    // Get the values of the registers.
    Snapshot::Registers const& registers() const;

    // Get the values of the registers prior to executing the last instruction.
    Snapshot::Registers prevRegisters() const;

    // Get a pointer on the full snapshot associated to this State.
    std::shared_ptr<Snapshot const> snapshot() const;

private:
    Vm::OperatingState m_runState;
    std::shared_ptr<Code const> m_loadedCode;
    std::shared_ptr<Snapshot const> m_latestSnapshot;
};

// Backend implementation of the user interface. This is meant to be derived in
// a sub-class that actually implements the input/output routines.
class Backend {
public:
    // Virtual destructor to ensure calling derived destructors.
    virtual ~Backend() = 0;

    // Initialize the backend.
    // @return: true if the initialization was successful, false otherwise.
    bool init();

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
    // Implementation of init, to be defined by sub-class.
    virtual bool doInit() = 0;

    // Implementation of waitForNextAction, to be defined by sub-class.
    virtual Action doWaitForNextAction() = 0;

    // Implementation of update, to be defined by sub-class.
    virtual void doUpdate(State const& newState) = 0;

    // Implementation of log, to be defined by sub-class.
    virtual void doLog(std::string const& msg) = 0;
};
}
