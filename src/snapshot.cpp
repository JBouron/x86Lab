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
    vmState(std::move(state)) {}

std::shared_ptr<Snapshot> Snapshot::base() const {
    return baseSnapshot;
}

bool Snapshot::hasBase() const {
    return baseSnapshot != nullptr;
}

Snapshot::Registers const& Snapshot::registers() const {
    return vmState->registers();
}

std::unique_ptr<u8> Snapshot::readPhysicalMemory(u64 const offset,
                                                 u64 const size) {
    std::unique_ptr<u8> buf(new u8[size]);
    std::memset(buf.get(), 0, size);
    Vm::State::Memory const& mem(vmState->memory());
    if (offset <= mem.size) {
        u64 const toRead(std::min(size, vmState->memory().size - offset));
        std::memcpy(buf.get(), vmState->memory().data.get() + offset, toRead);
    }
    return buf;
}
}
