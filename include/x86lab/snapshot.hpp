#pragma once

#include <x86lab/vm.hpp>
#include <memory>

namespace X86Lab {
// Opaque type performing the actual memory snapshot deduplication.
class BlockTree;

// A snapshot of the VM state. Each snapshot is built on a previous snapshot,
// called a base. The goal of snapshots is to explore the state of the machine
// at any point in the execution.
class Snapshot {
public:
    // Create a snapshot that does not build on top of a base snapshot. This is
    // meant for the very first snapshot.
    // @param state: The state associated with this snapshot.
    Snapshot(std::unique_ptr<Vm::State> state);

    // Construct a snapshot from a base snapshot.
    // @param base: The base snapshot to build on top of. If this pointer is
    // nullptr then this is a "root snapshot".
    // @param state: The state of this new snapshot.
    Snapshot(std::shared_ptr<Snapshot> const base,
             std::unique_ptr<Vm::State> state);

    // Get the base of this snapshot.
    // @return: The base of this snapshot. If the snapshot has no base then this
    // returns nullptr.
    std::shared_ptr<Snapshot> base() const;

    // Check if this snapshot has a base eg base() != std::nullptr.
    // @return: true if this snapshot has a base, false otherwise.
    bool hasBase() const;

    // For now use Vm::State::Registers as the underlying type for the set of
    // register values. This typedef will make it easier to change this in the
    // future if need be.
    using Registers = Vm::State::Registers;

    // Get the value of the Vm's registers in this snapshot.
    // @return: A ref on the Vm::Registers associated to this snapshot.
    Registers const& registers() const;

    // Read from the snapshot of the VM's physical memory. Reading outside the
    // physical memory's boundary leads to reading zeroes.
    // @param offset: The offset to read from.
    // @param size: The number of bytes to read.
    std::unique_ptr<u8> readPhysicalMemory(u64 const offset,
                                           u64 const size) const;

private:
    // The snapshot this snapshot is built on top of.
    std::shared_ptr<Snapshot> m_baseSnapshot;
    // The value of all the register at that snapshot.
    Vm::State::Registers m_regs;
    // Underlying BlockTree holding the snapshot of memory.
    std::shared_ptr<BlockTree> m_blockTree;
};
}
