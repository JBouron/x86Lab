#include <x86lab/ui/tui.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>

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

    // Start with General purpose mode.
    m_currentMode = RegisterWindowMode::GeneralPurpose;
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
        } else if (nextChar == 'r') {
            return Action::ReverseStep;
        } else if (nextChar == 'q') {
            return Action::Quit;
        } else if (nextChar == KEY_LEFT || nextChar == KEY_RIGHT) {
            cycleTabs(nextChar == KEY_RIGHT);
        }
        nextChar = regWin->getChar();
    }
}

void Tui::cycleTabs(bool const toRight) {
    int const curr(static_cast<int>(m_currentMode));
    int const max(static_cast<int>(RegisterWindowMode::NumRegisterWindowMode));
    bool const needUpdate((toRight && curr != max - 1) || (!toRight && !!curr));
    if (needUpdate) {
        int const delta(toRight ? 1 : -1);
        m_currentMode = static_cast<RegisterWindowMode>(curr + delta);

        // Show the new tab.
        doUpdateRegWin(prevRegs, currRegs);
        refresh();
    }
}

void Tui::doUpdate(State const& newState) {
    prevRegs = newState.prevRegisters();
    currRegs = newState.registers();
    doUpdateRegWin(prevRegs, currRegs);

    u64 const currLine(newState.currentLine());
    if (!!currLine) {
        doUpdateCodeWin(newState.sourceFileName(), currLine);
    }
    refresh();
}

// Get the title for a RegisterWindowMode.
// @param mode: The mode to get the title for.
std::string Tui::titleForRegisterWindowMode(RegisterWindowMode const& mode) {
    switch (mode) {
        case RegisterWindowMode::GeneralPurpose:
            return "Registers [General Purpose]";
        case RegisterWindowMode::FpuMmx:
            return "Registers [FPU & MMX]";
        case RegisterWindowMode::NumRegisterWindowMode:
            throw Error("NumRegisterWindowMode is not a valid mode", 0);
        default:
            throw Error("Unsupported RegisterWindowMode", 0);
    }
}

void Tui::doUpdateRegWin(Snapshot::Registers const& prevRegs,
                         Snapshot::Registers const& newRegs) {
    Window& w(*regWin);
    w.clearAndResetCursor();

    // Update the title of the registers window depending on the mode.
    regWin->setTitle(titleForRegisterWindowMode(m_currentMode));

    switch (m_currentMode) {
        case RegisterWindowMode::GeneralPurpose:
            doUpdateRegWinGp(prevRegs, newRegs);
            break;
        case RegisterWindowMode::FpuMmx:
            doUpdateRegWinFpuMmx(prevRegs, newRegs);
            break;
        case RegisterWindowMode::NumRegisterWindowMode:
            throw Error("NumRegisterWindowMode is not a valid mode", 0);
            break;
        default:
            throw Error("Unsupported RegisterWindowMode", 0);
    }
}

void Tui::doUpdateRegWinGp(Snapshot::Registers const& prevRegs,
                           Snapshot::Registers const& newRegs) {
    assert(m_currentMode == RegisterWindowMode::GeneralPurpose);

    Window& w(*regWin);
    Snapshot::Registers const& p(prevRegs);
    Snapshot::Registers const& n(newRegs);

    char buf[512];
    sprintf(buf, "rax = 0x%016lx   rbx = 0x%016lx\n", p.rax, p.rbx);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.rax, n.rbx);
    w << buf;
    sprintf(buf, "rcx = 0x%016lx   rdx = 0x%016lx\n", p.rcx, p.rdx);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.rcx, n.rdx);
    w << buf;
    sprintf(buf, "rdi = 0x%016lx   rsi = 0x%016lx\n", p.rdi, p.rsi);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.rdi, n.rsi);
    w << buf;
    sprintf(buf, "rbp = 0x%016lx   rsp = 0x%016lx\n", p.rbp, p.rsp);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.rbp, n.rsp);
    w << buf;
    sprintf(buf, "r8  = 0x%016lx   r9  = 0x%016lx\n", p.r8, p.r9);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.r8, n.r9);
    w << buf;
    sprintf(buf, "r10 = 0x%016lx   r11 = 0x%016lx\n", p.r10, p.r11);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.r10, n.r11);
    w << buf;
    sprintf(buf, "r12 = 0x%016lx   r13 = 0x%016lx\n", p.r12, p.r13);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.r12, n.r13);
    w << buf;
    sprintf(buf, "r14 = 0x%016lx   r15 = 0x%016lx\n", p.r14, p.r15);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.r14, n.r15);
    w << buf;
    sprintf(buf, "rip = 0x%016lx   rfl = 0x%016lx\n", p.rip, p.rflags);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.rip, n.rflags);
    w << buf;
    w << "\n";
    sprintf(buf, "cs = 0x%04x                ds = 0x%04x\n", p.cs, p.ds);
    w << buf;
    sprintf(buf, " +-> 0x%04x                 +-> 0x%04x\n", n.cs, n.ds);
    w << buf;
    sprintf(buf, "es = 0x%04x                fs = 0x%04x\n", p.es, p.fs);
    w << buf;
    sprintf(buf, " +-> 0x%04x                 +-> 0x%04x\n", n.es, n.fs);
    w << buf;
    sprintf(buf, "gs = 0x%04x                ss = 0x%04x\n", p.gs, p.ss);
    w << buf;
    sprintf(buf, " +-> 0x%04x                 +-> 0x%04x\n", n.gs, n.ss);
    w << buf;
    w << "\n";
    sprintf(buf, "cr0 = 0x%016lx   cr2 = 0x%016lx\n", p.cr0, p.cr2);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.cr0, n.cr2);
    w << buf;
    sprintf(buf, "cr3 = 0x%016lx   cr4 = 0x%016lx\n", p.cr3, p.cr4);
    w << buf;
    sprintf(buf, " +--> 0x%016lx    +--> 0x%016lx\n", n.cr3, n.cr4);
    w << buf;
    sprintf(buf, "cr8 = 0x%016lx  efer = 0x%016lx\n", p.cr8, p.efer);
    w << buf;
    sprintf(buf, " +--> 0x%016lx   +---> 0x%016lx\n", n.cr8, n.efer);
    w << buf;
    w << "\n";
    sprintf(buf,
            "idt: base = 0x%016lx   limit = 0x%08x\n",
            p.idt.base,
            p.idt.limit);
    w << buf;
    sprintf(buf,
            " +-> base = 0x%016lx   limit = 0x%08x\n",
            n.idt.base,
            n.idt.limit);
    w << buf;
    sprintf(buf,
            "gdt: base = 0x%016lx   limit = 0x%08x\n",
            p.gdt.base,
            p.gdt.limit);
    w << buf;
    sprintf(buf,
            " +-> base = 0x%016lx   limit = 0x%08x\n",
            n.gdt.base,
            n.gdt.limit);
    w << buf;
}

// Pretty print a vector register of width W in elements of type T.
// @param vec: The vector to pretty print.
// @return: The string representation of `vec` in elements of T separated by
// spaces.
template<size_t W, typename T>
static std::string vecToString(vec<W> const& vec) {
    u32 const elemWidth(sizeof(T) * 8);
    assert(!(W % elemWidth));
    u32 const numElems(W / elemWidth);

    std::ostringstream oss;
    oss << std::hex;
    for (int i(numElems - 1); i >=0; --i) {
        oss << std::setfill('0') << std::setw(elemWidth / 4);
        oss << (u64)vec.template elem<T>(i);
        if (!!i) {
            oss << " ";
        }
    }
    return oss.str();
}

void Tui::doUpdateRegWinFpuMmx(Snapshot::Registers const& prevRegs,
                               Snapshot::Registers const& newRegs) {
    assert(m_currentMode == RegisterWindowMode::FpuMmx);

    Window& w(*regWin);
    Snapshot::Registers const& p(prevRegs);
    Snapshot::Registers const& n(newRegs);

    char const * const sep("  |  ");
    for (u8 i(0); i < 8; ++i) {
        w << "mm" << std::to_string(i).c_str() << " = ";
        w << vecToString<64, u64>(p.mmx[i]).c_str() << sep;
        w << vecToString<64, u32>(p.mmx[i]).c_str() << sep;
        w << vecToString<64, u16>(p.mmx[i]).c_str() << sep;
        w << vecToString<64, u8>(p.mmx[i]).c_str() << "\n";
        w << " +--> ";
        w << vecToString<64, u64>(n.mmx[i]).c_str() << sep;
        w << vecToString<64, u32>(n.mmx[i]).c_str() << sep;
        w << vecToString<64, u16>(n.mmx[i]).c_str() << sep;
        w << vecToString<64, u8>(n.mmx[i]).c_str() << "\n";

        if (i < 7) {
            w << "\n";
        }
    }
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
    m_borderWin(::newwin(height, width, defaultPosY, defaultPosX)),
    m_win(::newwin(height-innerPadY*2,width-innerPadX*2,innerPadY,innerPadX)),
    m_title(std::string(" ") + title + " "),
    m_height(height) {
    // Enable keypad on the (inner) window in order to capture function keys.
    ::keypad(m_win, true);
    // Prepare the border window and add the title to it.
    ::wborder(m_borderWin, '|', '|', '-', '-', '+', '+', '+', '+');
    ::mvwprintw(m_borderWin, 0, titleOffset, "%s", m_title.c_str());
    ::leaveok(m_win, true);
}

Tui::Window::~Window() {
    ::delwin(m_win);
    ::delwin(m_borderWin);
}

void Tui::Window::move(u32 const y, u32 const x) {
    ::mvwin(m_borderWin, y, x);
    ::mvwin(m_win, y + innerPadY, x + innerPadX);
    ::wrefresh(m_borderWin);
    ::wrefresh(m_win);
}

void Tui::Window::refresh() {
    // Only refresh the inner window here, this is because refreshing the
    // m_borderWin will overwrite the content of the inner window. The
    // m_borderWin is only meant to be refreshed when moved.
    ::wrefresh(m_win);
}

void Tui::Window::clearAndResetCursor() {
    ::werase(m_win);
    wmove(m_win, 0, 0);
}

Tui::Window& Tui::Window::operator<<(char const * const str) {
    ::wprintw(m_win, "%s", str);
    return *this;
}

int Tui::Window::getChar() {
    return ::wgetch(m_win);
}

void Tui::Window::enableScrolling(bool const enabled) {
    ::scrollok(m_win, enabled);
    if (enabled) {
        ::wsetscrreg(m_win, 0, m_height);
    }
}

void Tui::Window::setTitle(std::string const& newTitle) {
    std::string const withPad(std::string(" ") + newTitle + " ");

    // Only refresh the m_win and m_borderWin if the title actually changed to
    // avoid flickering as much as possible.
    if (m_title != withPad) {
        m_title = withPad;
        ::wborder(m_borderWin, '|', '|', '-', '-', '+', '+', '+', '+');
        ::mvwprintw(m_borderWin, 0, titleOffset, "%s", m_title.c_str());
        ::wrefresh(m_borderWin);
        // The refresh above destroyed the m_win area. We need to completely
        // re-draw it.
        ::redrawwin(m_win);
    }
}
}
