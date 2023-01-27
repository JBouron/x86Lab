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

    for (u8 i(0); i < Vm::State::Registers::NumMmxRegs; ++i) {
        regs.mmx[i] = generator();
    }
    for (u8 i(0); i < Vm::State::Registers::NumXmmRegs; ++i) {
        regs.xmm[i] = vec128(generator(), generator());
    }
    for (u8 i(0); i < Vm::State::Registers::NumYmmRegs; ++i) {
        regs.ymm[i] = vec256(generator(),
                             generator(),
                             generator(),
                             generator());
    }
    for (u8 i(0); i < Vm::State::Registers::NumZmmRegs; ++i) {
        // FIXME: We do not honor the relationship between ZMMi, YMMi and
        // XMMi for i < 16 (e.g. aliased lower bits). However it does not matter
        // much for these tests.
        regs.zmm[i] = vec512(generator(),
                             generator(),
                             generator(),
                             generator(),
                             generator(),
                             generator(),
                             generator(),
                             generator());
    }

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
        std::vector<u8> const currMem(currSnap->readPhysicalMemory(0, memSize));
        TEST_ASSERT(!std::memcmp(currMem.data(), memHistory[i].get(), memSize));
        currSnap = currSnap->base();
    }
}

// Test the readLinearMemory method of Snapshot.
DECLARE_TEST(testReadLinearMemory) {
    // This is a somewhat hacky test: we create a VM in long mode so that it
    // gets page tables allocated an set-up. Those page tables form an identity
    // map over the entire VM's physical memory.
    // We then write random data over the VM's physical memory, while being
    // careful not to overwrite the page table.
    // Then we create a Snapshot of this random memory state, we then check that
    // reading from physical or linear memory returns the same thing (due to
    // identity mapping).
    u64 const memSize(4 * X86Lab::PAGE_SIZE);
    // Start the VM in long mode so paging is enabled.
    X86Lab::Vm::CpuMode const startMode(X86Lab::Vm::CpuMode::LongMode);
    std::unique_ptr<X86Lab::Vm> vm(new X86Lab::Vm(startMode, memSize));
    std::unique_ptr<X86Lab::Vm::State> state(vm->getState());

    X86Lab::Vm::State::Memory const& memory(state->memory());

    // Fill the first memSize bytes of the memory with random stuff. This does
    // not touch the page tables since those are allocated after `memSize`
    // bytes.
    std::mt19937_64 generator;
    u64 * const rawPtr(reinterpret_cast<u64*>(memory.data.get()));
    for (u64 i(0); i < memSize / sizeof(u64); ++i) {
        rawPtr[i] = generator();
    }

    // Now create a snapshot of the state.
    X86Lab::Snapshot const snap(std::move(state));

    // Now compare reading linear and physical memory. Due to the identity
    // mapping we should read the same thing in both.
    // /!\ memory.size != memSize here because of the additional physical frames
    // allocated for the page tables.
    u64 const size(memory.size);
    std::vector<u8> const phyMem(snap.readPhysicalMemory(0, size));
    std::vector<u8> const linMem(snap.readLinearMemory(0, size));
    TEST_ASSERT(phyMem == linMem);
}
}
