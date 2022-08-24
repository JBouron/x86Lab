#pragma once
#include <x86lab/ui/ui.hpp>

namespace X86Lab::Ui {

// Implementation of Manager for "raw" output in stdout. Each update prints out
// the current value of the registers. The code is stepped through until the Vm
// is no longer runnable.
class Cli : public Backend {
private:
    // Implementation of waitForNextAction.
    virtual Action doWaitForNextAction();

    // Implementation of update.
    virtual void doUpdate(State const& newState);

    // Implementation of log.
    virtual void doLog(std::string const& msg);

    // True if the last call to update indicated that the VM was still runnable.
    // This is used to know when to terminate and send an Action::Quit in the
    // next call to doWaitForNextAction.
    bool isVmRunnable{true};
};
}
