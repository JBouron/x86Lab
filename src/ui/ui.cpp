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

bool State::isVmRunnable() const {
    return runState == Vm::OperatingState::Runnable;
}

std::string const& State::sourceFileName() const {
    return loadedCode->fileName();
}

u64 State::currentLine() const {
    return mapToLine(registers().rip);
}

u64 State::mapToLine(u64 const address) const {
    Assembler::InstructionMap const& map(loadedCode->getInstructionMap());
    // Re-use the mapInstructionPointer to map an arbitrary address.
    return map.mapInstructionPointer(address).line;
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
    oss << "[" << std::put_time(std::localtime(&tm), "%T") << "]";

    // Prefix for log message [<date>].
    doLog(oss.str() + " " + msg);
}
}
