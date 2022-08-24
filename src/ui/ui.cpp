#include <x86lab/ui/ui.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace X86Lab::Ui {

State::State(Vm::State const runState,
             Assembler::Code const * const code,
             Vm::RegisterFile const * const registerValues) :
    runState(runState), 
    loadedCode(code),
    regValues(registerValues) {}

Vm::State State::state() const {
    return runState;
}

Assembler::Code const& State::code() const {
    return *loadedCode;
}

Vm::RegisterFile const& State::registers() const {
    return *regValues;
}

Action Manager::waitForNextAction() {
    return doWaitForNextAction();
}

void Manager::update(State const& newState) {
    doUpdate(newState);
}

void Manager::log(std::string const& msg) {
    // Gotta love std::chrono boilerplate.
    std::chrono::time_point<std::chrono::system_clock> const date(
        std::chrono::system_clock::now());
    std::time_t const tm(std::chrono::system_clock::to_time_t(date));
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&tm), "%T");

    // Prefix for log message [<date>].
    std::string const prefix("[" + oss.str() + "]");
    doLog(prefix + " " + msg);
}
}
