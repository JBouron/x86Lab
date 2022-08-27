#include <x86lab/ui/tui.hpp>
#include <fstream>

namespace X86Lab::Ui {

bool Tui::isNcurseInitialized = false;

Tui::Tui() {
    // Initialize ncurse if needed.
    if (!isNcurseInitialized) {
        isNcurseInitialized = true;
        ::initscr();
        // Disable echoing to avoid printing the keys being pressed.
        ::noecho();
    }

    // Current geometry of the terminal. R = rows, C = cols.
    u32 termR, termC;
    getmaxyx(stdscr, termR, termC);

    // 3 * gap size for each terminal borders and between the register and code
    // windows.
    u32 const codeWinC(codeWinWidth * (termC - gapSize * 3));
    // The rest goes for the register window.
    u32 const regWinC((termC - gapSize * 3) - codeWinC);
    u32 const logWinC(termC - gapSize * 2);

    u32 const codeWinR(codeWinHeight * (termR - gapSize * 3));
    u32 const regWinR(codeWinR);
    u32 const logWinR((termR - gapSize * 3) - codeWinR);

    // Create the windows and place them appropriately.
    codeWin = ::new Window(codeWinR, codeWinC, codeWinTitle);
    codeWin->move(gapSize, gapSize);
    regWin = ::new Window(regWinR, regWinC, regWinTitle);
    regWin->move(gapSize, codeWinC + gapSize * 2);
    logWin = ::new Window(logWinR, logWinC, logWinTitle);
    logWin->move(codeWinR + gapSize * 2, gapSize);
    logWin->enableScrolling(true);
}

Tui::~Tui() {
    delete codeWin;
    delete regWin;
    delete logWin;

    // Disable ncurses.
    ::endwin();
}

Action Tui::doWaitForNextAction() {
    int nextChar(regWin->getChar());
    while (true) {
        if (nextChar == 's') {
            return Action::Step;
        } else if (nextChar == 'q') {
            return Action::Quit;
        }
        nextChar = regWin->getChar();
    }
}

void Tui::doUpdate(State const& newState) {
    doUpdateRegWin(newState.registers());

    Assembler::InstructionMap const& map(newState.code()->getInstructionMap());
    auto const entry(map.mapInstructionPointer(newState.registers().rip));
    if (!!entry) {
        doUpdateCodeWin(newState.code()->fileName(), entry.line);
    }
    refresh();
}

void Tui::doUpdateRegWin(Vm::State::Registers const& newRegs) {
    Window& w(*regWin);
    w.clearAndResetCursor();
    char buf[512];
    sprintf(buf, "rax = 0x%016lx   rbx = 0x%016lx\n", prevRegs.rax, prevRegs.rbx);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.rax, newRegs.rbx);
    w << buf;
    sprintf(buf, "rcx = 0x%016lx   rdx = 0x%016lx\n", prevRegs.rcx, prevRegs.rdx);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.rcx, newRegs.rdx);
    w << buf;
    sprintf(buf, "rdi = 0x%016lx   rsi = 0x%016lx\n", prevRegs.rdi, prevRegs.rsi);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.rdi, newRegs.rsi);
    w << buf;
    sprintf(buf, "rbp = 0x%016lx   rsp = 0x%016lx\n", prevRegs.rbp, prevRegs.rsp);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.rbp, newRegs.rsp);
    w << buf;
    sprintf(buf, "r8  = 0x%016lx   r9  = 0x%016lx\n", prevRegs.r8, prevRegs.r9);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.r8, newRegs.r9);
    w << buf;
    sprintf(buf, "r10 = 0x%016lx   r11 = 0x%016lx\n", prevRegs.r10, prevRegs.r11);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.r10, newRegs.r11);
    w << buf;
    sprintf(buf, "r12 = 0x%016lx   r13 = 0x%016lx\n", prevRegs.r12, prevRegs.r13);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.r12, newRegs.r13);
    w << buf;
    sprintf(buf, "r14 = 0x%016lx   r15 = 0x%016lx\n", prevRegs.r14, prevRegs.r15);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.r14, newRegs.r15);
    w << buf;
    sprintf(buf, "rip = 0x%016lx   rfl = 0x%016lx\n", prevRegs.rip, prevRegs.rflags);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.rip, newRegs.rflags);
    w << buf;
    w << "\n";
    sprintf(buf, "cs = 0x%04x                ds = 0x%04x\n", prevRegs.cs, prevRegs.ds);
    w << buf;
    sprintf(buf, " +-> 0x%04x                 +-> 0x%04x\n", newRegs.cs, newRegs.ds);
    w << buf;
    sprintf(buf, "es = 0x%04x                fs = 0x%04x\n", prevRegs.es, prevRegs.fs);
    w << buf;
    sprintf(buf, " +-> 0x%04x                 +-> 0x%04x\n", newRegs.es, newRegs.fs);
    w << buf;
    sprintf(buf, "gs = 0x%04x                ss = 0x%04x\n", prevRegs.gs, prevRegs.ss);
    w << buf;
    sprintf(buf, " +-> 0x%04x                 +-> 0x%04x\n", newRegs.gs, newRegs.ss);
    w << buf;
    w << "\n";
    sprintf(buf, "cr0 = 0x%016lx   cr2 = 0x%016lx\n", prevRegs.cr0, prevRegs.cr2);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.cr0, newRegs.cr2);
    w << buf;
    sprintf(buf, "cr3 = 0x%016lx   cr4 = 0x%016lx\n", prevRegs.cr3, prevRegs.cr4);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", newRegs.cr3, newRegs.cr4);
    w << buf;
    sprintf(buf, "cr8 = 0x%016lx  efer = 0x%016lx\n", prevRegs.cr8, prevRegs.efer);
    w << buf;
    sprintf(buf, " +--> 0x%016lx   +---> 0x%016lx\n", newRegs.cr8, newRegs.efer);
    w << buf;
    w << "\n";
    sprintf(buf, "idt: base = 0x%016lx   limit = 0x%08x\n", prevRegs.idt.base, prevRegs.idt.limit);
    w << buf;
    sprintf(buf, " +-> base = 0x%016lx   limit = 0x%08x\n", newRegs.idt.base, newRegs.idt.limit);
    w << buf;
    sprintf(buf, "gdt: base = 0x%016lx   limit = 0x%08x\n", prevRegs.gdt.base, prevRegs.gdt.limit);
    w << buf;
    sprintf(buf, " +-> base = 0x%016lx   limit = 0x%08x\n", newRegs.gdt.base, newRegs.gdt.limit);
    w << buf;

    prevRegs = newRegs;
}

void Tui::doUpdateCodeWin(std::string const& fileName, u64 const currLine) {
    std::ifstream file(fileName, std::ios::in);
    std::string line;
    Window& cWin(*codeWin);
    cWin.clearAndResetCursor();
    u64 lineNum(0);
    while (std::getline(file, line)) {
        lineNum ++;
        std::string const prefix((lineNum == currLine) ? ">>> " : "    ");
        cWin << (prefix + line + "\n").c_str();
    }
}

void Tui::doLog(std::string const& msg) {
    std::string const toPrint(msg + "\n");
    logWin->operator<<(toPrint.c_str());
    this->refresh();
}

void Tui::refresh() {
    codeWin->refresh();
    regWin->refresh();
    logWin->refresh();
}

Tui::Window::Window(u32 const height, u32 const width, std::string const& title)
    :
    borderWin(::newwin(height, width, defaultPosY, defaultPosX)),
    win(::newwin(height-(innerPadY*2),width-(innerPadX*2),innerPadY,innerPadX)),
    title(std::string(" ") + title + " "),
    height(height),
    width(width) {
    // Enable keypad on the (inner) window in order to capture function keys.
    ::keypad(win, true);
    // Prepare the border window and add the title to it.
    ::wborder(borderWin, '|', '|', '-', '-', '+', '+', '+', '+');
    ::mvwprintw(borderWin, 0, titleOffset, this->title.c_str());
    ::leaveok(win, true);
}

Tui::Window::~Window() {
    ::delwin(win);
    ::delwin(borderWin);
}

void Tui::Window::move(u32 const y, u32 const x) {
    ::mvwin(borderWin, y, x);
    ::mvwin(win, y + innerPadY, x + innerPadX);
    ::wrefresh(borderWin);
    ::wrefresh(win);
}

void Tui::Window::refresh() {
    // Only refresh the inner window here, this is because refreshing the
    // borderWin will overwrite the content of the inner window. The borderWin
    // is only meant to be refreshed when moved.
    ::wrefresh(win);
}

void Tui::Window::clearAndResetCursor() {
    ::werase(win);
    wmove(win, 0, 0);
}

Tui::Window& Tui::Window::operator<<(char const * const str) {
    ::wprintw(win, str);
    return *this;
}

int Tui::Window::getChar() {
    return ::wgetch(win);
}

void Tui::Window::enableScrolling(bool const enabled) {
    ::scrollok(win, enabled);
    if (enabled) {
        ::wsetscrreg(win, 0, height);
    }
}
}
