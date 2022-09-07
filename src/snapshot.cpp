#include <x86lab/snapshot.hpp>
#include <algorithm>

namespace X86Lab {
Snapshot::Snapshot(std::unique_ptr<Vm::State> state) :
    Snapshot(nullptr, std::move(state)) {}

// FIXME: For now snapshots are not actually built on top of previous snapshots.
// This means that we keep a full copy of the full physical memory with the
// Snapshot. Eventually we should work out a way to deduplicate.
Snapshot::Snapshot(std::shared_ptr<Snapshot> const base,
                   std::unique_ptr<Vm::State> state) :
    baseSnapshot(base),
    regs(state->registers()),
    memSize(state->memory().size),
    mem(new u8[memSize]) {
    std::memcpy(mem.get(), state->memory().data.get(), memSize);
}

std::shared_ptr<Snapshot> Snapshot::base() const {
    return baseSnapshot;
}

bool Snapshot::hasBase() const {
    return baseSnapshot != nullptr;
}

Snapshot::Registers const& Snapshot::registers() const {
    return regs;
}

std::unique_ptr<u8> Snapshot::readPhysicalMemory(u64 const offset,
                                                 u64 const size) const {
    std::unique_ptr<u8> buf(new u8[size]);
    std::memset(buf.get(), 0, size);
    if (offset <= memSize) {
        u64 const toRead(std::min(size, memSize - offset));
        std::memcpy(buf.get(), mem.get() + offset, toRead);
    }
    return buf;
}
}
