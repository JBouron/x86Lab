#include <x86lab/ui/ui.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace X86Lab::Ui {

State::State(Vm::OperatingState const runState,
             std::shared_ptr<Assembler::Code const> const code,
             std::shared_ptr<Snapshot const> const snapshot) :
    runState(runState), 
    loadedCode(code),
    latestSnapshot(snapshot) {}

Vm::OperatingState State::state() const {
    return runState;
}

std::shared_ptr<Assembler::Code const> State::code() const {
    return loadedCode;
}

Snapshot::Registers const& State::registers() const {
    return latestSnapshot->registers();
}

Snapshot::Registers State::prevRegisters() const {
    if (latestSnapshot->hasBase()) {
        return latestSnapshot->base()->registers();
    } else {
        return Snapshot::Registers{};
    }
}

Backend::~Backend() {}

Action Backend::waitForNextAction() {
    return doWaitForNextAction();
}

void Backend::update(State const& newState) {
    doUpdate(newState);
}

void Backend::log(std::string const& msg) {
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
