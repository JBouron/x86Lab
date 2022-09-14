#include <x86lab/vm.hpp>
#include <x86lab/test.hpp>
#include <x86lab/snapshot.hpp>
#include <random>

namespace X86Lab::Test::Snapshot {
// Allocate an X86Lab::Vm::State that is filled with random values.
// @param memSize: The size of memory in bytes.
static std::unique_ptr<X86Lab::Vm::State> genRandomState(u64 const memSize) {
    static std::mt19937_64 generator;

    // Those value might not be valid, but it does not matter for Snapshot
    // tests.
    X86Lab::Vm::State::Registers regs({}, {}, {});
    regs.rax = generator();
    regs.rbx = generator();
    regs.rcx = generator();
    regs.rdx = generator();
    regs.rdi = generator();
    regs.rsi = generator();
    regs.rsp = generator();
    regs.rbp = generator();
    regs.r8 = generator();
    regs.r9 = generator();
    regs.r10 = generator();
    regs.r11 = generator();
    regs.r12 = generator();
    regs.r13 = generator();
    regs.r14 = generator();
    regs.r15 = generator();
    regs.rflags = generator();
    regs.rip = generator();
    regs.cs = generator();
    regs.ds = generator();
    regs.es = generator();
    regs.fs = generator();
    regs.gs = generator();
    regs.ss = generator();
    regs.cr0 = generator();
    regs.cr2 = generator();
    regs.cr3 = generator();
    regs.cr4 = generator();
    regs.cr8 = generator();
    regs.efer = generator();
    regs.idt = { .base = generator(), .limit = static_cast<u16>(generator()) };
    regs.gdt = { .base = generator(), .limit = static_cast<u16>(generator()) };
    regs.mm0 = generator();
    regs.mm1 = generator();
    regs.mm2 = generator();
    regs.mm3 = generator();
    regs.mm4 = generator();
    regs.mm5 = generator();
    regs.mm6 = generator();
    regs.mm7 = generator();

    TEST_ASSERT(!(memSize % 8));

    std::unique_ptr<u8[]> memData(new u8[memSize]);
    u64 * const raw(reinterpret_cast<u64*>(memData.get()));
    for (u64 i(0); i < (memSize / 8); ++i) {
        raw[i] = generator();
    }
    X86Lab::Vm::State::Memory mem({.data = std::move(memData),.size = memSize});
    return std::unique_ptr<X86Lab::Vm::State>(new X86Lab::Vm::State(
        regs, std::move(mem)));
}

static std::unique_ptr<u8> copyMemory(X86Lab::Vm::State::Memory const& mem) {
    u64 const memSize(mem.size);
    std::unique_ptr<u8> memCopy(new u8[memSize]);
    std::memcpy(memCopy.get(), mem.data.get(), memSize);
    return memCopy;
}

// Test that the linked-list of snapshot is correct and gives the correct state
// at each step.
DECLARE_TEST(testBasicSnapshots) {
    u64 const memSize(4 * X86Lab::PAGE_SIZE);

    std::vector<X86Lab::Vm::State::Registers> regsHistory;
    std::vector<std::unique_ptr<u8>> memHistory;

    int const numSnapshots(32);

    // Create numSnapshots snapshots each with a different state.
    std::shared_ptr<X86Lab::Snapshot> prevSnap(nullptr);
    for (int i(0); i < numSnapshots; ++i) {
        std::unique_ptr<X86Lab::Vm::State> state(genRandomState(memSize));
        regsHistory.push_back(state->registers());
        memHistory.push_back(copyMemory(state->memory()));

        prevSnap = std::shared_ptr<X86Lab::Snapshot>(
            new X86Lab::Snapshot(prevSnap, std::move(state)));
    }

    // Now walk down the linked-list of snapshot, starting with the most recent
    // one, and each time comparing against the expected state (registers and
    // memory).
    std::shared_ptr<X86Lab::Snapshot> currSnap(prevSnap);
    for (int i(numSnapshots - 1); i >= 0; --i) {
        if (!!i) {
            TEST_ASSERT(currSnap->hasBase());
        } else {
            TEST_ASSERT(!currSnap->hasBase());
        }
        TEST_ASSERT(currSnap->registers() == regsHistory[i]);
        std::unique_ptr<u8> currMem(currSnap->readPhysicalMemory(0, memSize));
        TEST_ASSERT(!std::memcmp(currMem.get(), memHistory[i].get(), memSize));
        currSnap = currSnap->base();
    }
}
}
