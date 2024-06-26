#include <x86lab/ui/ui.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace X86Lab::Ui {

State::State(Vm::OperatingState const runState,
             std::shared_ptr<Code const> const code,
             std::shared_ptr<Snapshot const> const snapshot) :
    m_runState(runState), 
    m_loadedCode(code),
    m_latestSnapshot(snapshot) {}

bool State::isVmRunnable() const {
    return m_runState == Vm::OperatingState::Runnable;
}

std::string const& State::sourceFileName() const {
    return m_loadedCode->fileName();
}

u64 State::currentLine() const {
    return mapToLine(registers().rip);
}

u64 State::mapToLine(u64 const address) const {
    if (!!m_loadedCode) {
        return m_loadedCode->offsetToLine(address);
    } else {
        return 0;
    }
}

Snapshot::Registers const& State::registers() const {
    if (!!m_latestSnapshot) {
        return m_latestSnapshot->registers();
    } else {
        static Snapshot::Registers defaultRegs;
        return defaultRegs;
    }
}

Snapshot::Registers State::prevRegisters() const {
    if (!!m_latestSnapshot && m_latestSnapshot->hasBase()) {
        return m_latestSnapshot->base()->registers();
    } else {
        return Snapshot::Registers();
    }
}

std::shared_ptr<Snapshot const> State::snapshot() const {
    return m_latestSnapshot;
}

u64 State::codeLinearAddr() const {
    // The code is always loaded at linear address 0x0.
    return 0x0;
}

u64 State::codeSize() const {
    return m_loadedCode->size();
}

Backend::~Backend() {}

bool Backend::init() {
    return doInit();
}

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
