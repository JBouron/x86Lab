#include <x86lab/ui/cli.hpp>

namespace X86Lab::Ui {
Action Cli::doWaitForNextAction() {
    if (isVmRunnable) {
        return Action::Step;
    } else {
        return Action::Quit;
    }
}

void Cli::doUpdate(State const& newState) {
    // Simply dump the state in stdout.
    Vm::RegisterFile const& r(newState.registers());

    // Using printf for hexadecimal output is just way simpler than std::cout.
    printf("-- @ rip = 0x%016lx --------------------------\n", r.rip);
    printf("rax = 0x%016lx\trbx = 0x%016lx\n", r.rax, r.rbx);
    printf("rcx = 0x%016lx\trdx = 0x%016lx\n", r.rcx, r.rdx);
    printf("rdi = 0x%016lx\trsi = 0x%016lx\n", r.rdi, r.rsi);
    printf("rbp = 0x%016lx\trsp = 0x%016lx\n", r.rbp, r.rsp);
    printf("r8  = 0x%016lx\tr9  = 0x%016lx\n", r.r8, r.r9);
    printf("r10 = 0x%016lx\tr11 = 0x%016lx\n", r.r10, r.r11);
    printf("r12 = 0x%016lx\tr13 = 0x%016lx\n", r.r12, r.r13);
    printf("r14 = 0x%016lx\tr15 = 0x%016lx\n", r.r14, r.r15);
    printf("rip = 0x%016lx\trfl = 0x%016lx\n", r.rip, r.rflags);
    printf("cs = 0x%04x\tds = 0x%04x\n", r.cs, r.ds);
    printf("es = 0x%04x\tfs = 0x%04x\n", r.es, r.fs);
    printf("gs = 0x%04x\tss = 0x%04x\n", r.gs, r.ss);
    printf("cr0 = 0x%016lx\tcr2 = 0x%016lx\n", r.cr0, r.cr2);
    printf("cr3 = 0x%016lx\tcr4 = 0x%016lx\n", r.cr3, r.cr4);
    printf("cr8 = 0x%016lx\n", r.cr8);
    printf("idt :  base = 0x%016lx\tlimit = 0x%08x\n", r.idt.base, r.idt.limit);
    printf("gdt :  base = 0x%016lx\tlimit = 0x%08x\n", r.gdt.base, r.gdt.limit);
    printf("efer = 0x%016lx\n", r.efer);

    // Print information on the instruction being executed.
    Assembler::InstructionMap const& map(newState.code().getInstructionMap());
    auto const entry(map.mapInstructionPointer(r.rip));
    if (!!entry) {
        std::cout << "Line        = " << entry.line << std::endl;
        std::cout << "Next instr. = " << entry.instruction << std::endl;
    } else {
        std::cout << "Line        = ?" << std::endl;
        std::cout << "Next instr. = ?" << std::endl;
    }

    isVmRunnable = newState.state() == Vm::State::Runnable;
}

void Cli::doLog(std::string const& msg) {
    std::cout << msg << std::endl;
}
}
