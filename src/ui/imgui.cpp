#include <x86lab/ui/imgui.hpp>
#include <SDL.h>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace X86Lab::Ui {
std::map<Imgui::VectorRegisterGranularity, u32> const Imgui::granularityToBytes
    = {
    {Imgui::VectorRegisterGranularity::Byte,   1},
    {Imgui::VectorRegisterGranularity::Word,   2},
    {Imgui::VectorRegisterGranularity::Dword,  4},
    {Imgui::VectorRegisterGranularity::Qword,  8},
    {Imgui::VectorRegisterGranularity::Float,  4},
    {Imgui::VectorRegisterGranularity::Double, 8},
};

// SDL window and renderer left to nullptr until doInit() is called on this
// backend.
Imgui::Imgui() : m_sdlWindow(nullptr),
                 m_sdlRenderer(nullptr),
                 m_currentGranularity(VectorRegisterGranularity::Qword) {}

bool Imgui::doInit() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        // FIXME: Should probably return something a bit better than a raw bool
        // here, at least an error type indicating what went wrong.
        return false;
    }

    SDL_WindowFlags const flags((SDL_WindowFlags)(SDL_WINDOW_RESIZABLE |
                                                  SDL_WINDOW_ALLOW_HIGHDPI |
                                                  SDL_WINDOW_MAXIMIZED));

    m_sdlWindow = SDL_CreateWindow(Imgui::sdlWindowTitle,
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   Imgui::sdlWinDefaultSizeWidth,
                                   Imgui::sdlWinDefaultSizeHeight,
                                   flags);

    // Setup SDL_Renderer instance
    m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow,
                                       -1,
                                       SDL_RENDERER_PRESENTVSYNC |
                                       SDL_RENDERER_ACCELERATED);
    if (m_sdlRenderer == nullptr) {
        return false;
    }

    // Create a new context for ImGui, this must be the first ImGui::* function
    // called.
    ImGui::CreateContext();

    // Disable auto-saving of GUI preferences. As of now, nothing can be resized
    // and no options are meant to survive restarts.
    ImGui::GetIO().IniFilename = nullptr;

    // Enable dark theme, because it's more 1337.
    ImGui::StyleColorsDark();

    // Necessary init for using SDL and SDL_Renderer with ImGui.
    ImGui_ImplSDL2_InitForSDLRenderer(m_sdlWindow, m_sdlRenderer);
    ImGui_ImplSDLRenderer_Init(m_sdlRenderer);
    return true;
}

// Compute the next value of an enum, wrapping around to the first value of the
// enumeration if needed. This assume that the type of the enum (E) contains a
// value called "__MAX" which appears last in the enum's declaration.
// @param e: The value for which the next value should be computed.
// @return: The value appearing after `e` in E's declaration, if this is "__MAX"
// then this returns the first declared value in E.
template<typename E>
static E next(E const e) {
    int const curr(static_cast<int>(e));
    int const max(static_cast<int>(E::__MAX));
    return static_cast<E>((curr + 1) % max);
}

Action Imgui::doWaitForNextAction() {
    while (true) {
        // Main loop of the GUI refresh + input.
        // FIXME: Is this really the best place to put this? Maybe the GUI
        // should be refreshed in doUpdate()?
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(m_sdlWindow))) {
                return Action::Quit;
            }
        }

        draw();

        if (ImGui::IsKeyPressed(ImGuiKey_S, true)) {
            return Action::Step;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R, true)) {
            return Action::ReverseStep;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            // Toggle the next granularity.
            m_currentGranularity = next(m_currentGranularity);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            return Action::Quit;
        }
    }
    return Action::Quit;
}

void Imgui::doUpdate(State const& newState) {
    m_state = newState;
}

void Imgui::doLog(std::string const& msg) {
    m_logs.push_back(msg);
}

void Imgui::draw() {
    ImGui_ImplSDLRenderer_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport const& viewport(*ImGui::GetMainViewport());
    drawCodeWin(viewport);
    drawStackWin(viewport);
    drawRegsWin(viewport);
    // FIXME: For now the log window is disabled and the memory window is taking
    // its place.
    //drawLogsWin(viewport);
    drawMemWin(viewport);

    ImGui::Render();

    ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(m_sdlRenderer);
}

void Imgui::drawCodeWin(ImGuiViewport const& viewport) {
    ImVec2 const pos(codeWinPos.x * viewport.WorkSize.x,
                     codeWinPos.y * viewport.WorkSize.y);
    ImVec2 const size(codeWinSize.x * viewport.WorkSize.x,
                      codeWinSize.y * viewport.WorkSize.y);
    ImGui::SetNextWindowPos(codeWinPos);
    ImGui::SetNextWindowSize(size);

    // The code window is always centered on the current instruction, disable
    // scrolling.
    ImGuiWindowFlags const flags(defaultWindowFlags |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Begin("Code", NULL, flags);

    std::string fileName(m_state.sourceFileName());
    // A the beginning of program execution, the state could be default and
    // therefore no source file set yet.
    if (!!fileName.size()) {
        std::ifstream file(fileName, std::ios::in);
        std::string line;
        u64 lineNum(0);
        u64 const currLine(m_state.currentLine());
        while (std::getline(file, line)) {
            lineNum ++;
            std::string const prefix((lineNum == currLine) ? ">>> " : "    ");
            std::string const l(prefix + line);
            ImGui::Text("%s", l.c_str());
            if (lineNum == currLine) {
                // Do the centering on current instruction.
                ImGui::SetScrollHereY(0.5);
            }
        }
    }

    ImGui::End();
}

template<size_t W>
void Imgui::drawColsForVec(vec<W> const& vec,
                           VectorRegisterGranularity const granularity) {
    u32 const numElems(vec.bytes / granularityToBytes.at(granularity));
    for (int i(numElems - 1); i >= 0; --i) {
        ImGui::TableNextColumn();
        switch (granularity) {
            case VectorRegisterGranularity::Byte:
                ImGui::Text("%02hhx", vec.template elem<u8>(i));
                break;
            case VectorRegisterGranularity::Word:
                ImGui::Text("%04hx", vec.template elem<u16>(i));
                break;
            case VectorRegisterGranularity::Dword:
                ImGui::Text("%08x", vec.template elem<u32>(i));
                break;
            case VectorRegisterGranularity::Qword:
                ImGui::Text("%016lx", vec.template elem<u64>(i));
                break;
            case VectorRegisterGranularity::Float:
                ImGui::Text("%f", vec.template elem<float>(i));
                break;
            case VectorRegisterGranularity::Double:
                ImGui::Text("%f", vec.template elem<double>(i));
                break;
            default:
                throw std::runtime_error("Invalid granularity");
        }
    }
}

void Imgui::drawRegsWin(ImGuiViewport const& viewport) {
    // drawRegsWin must be called after drawStackWin since the size of the stack
    // window defines the pos and size of the register window.
    assert(!!m_stackWinSize.x && !!m_stackWinSize.y);
    ImVec2 const pos(stackWinPos.x * viewport.WorkSize.x + m_stackWinSize.x,
                     stackWinPos.y);
    ImVec2 const size(viewport.WorkSize.x - pos.x, m_stackWinSize.y);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);

    ImGui::Begin("Registers", NULL, defaultWindowFlags);
    ImGui::BeginTabBar("##tabs", 0);

    // General purpose registers.
    if (ImGui::BeginTabItem("General Purpose", NULL, 0)) {
        ImGui::Text("  -- General Purpose --");
        // Print general purpose registers rax, rbx, ..., r14, r15.
        auto const printGp([&]() {
            char const * const currFmt("%s = 0x%016lx    %s = 0x%016lx    "
                "%s = 0x%016lx    %s = 0x%016lx");
            char const * const histFmt("      0x%016lx          0x%016lx     "
                "     0x%016lx          0x%016lx");

            ImGui::Text(currFmt,
                        "rax", m_state.registers().rax,
                        "rbx", m_state.registers().rbx,
                        "rcx", m_state.registers().rcx,
                        "rdx", m_state.registers().rdx);
            ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
            ImGui::Text(histFmt,
                        m_state.prevRegisters().rax,
                        m_state.prevRegisters().rbx,
                        m_state.prevRegisters().rcx,
                        m_state.prevRegisters().rdx);
            ImGui::PopStyleColor();
            ImGui::Text("");

            ImGui::Text(currFmt,
                        "rsi", m_state.registers().rsi,
                        "rdi", m_state.registers().rdi,
                        "rsp", m_state.registers().rsp,
                        "rbp", m_state.registers().rbp);
            ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
            ImGui::Text(histFmt,
                        m_state.prevRegisters().rsi,
                        m_state.prevRegisters().rdi,
                        m_state.prevRegisters().rsp,
                        m_state.prevRegisters().rbp);
            ImGui::PopStyleColor();
            ImGui::Text("");

            ImGui::Text(currFmt,
                        "r8 ", m_state.registers().r8,
                        "r9 ", m_state.registers().r9,
                        "r10", m_state.registers().r10,
                        "r11", m_state.registers().r11);
            ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
            ImGui::Text(histFmt,
                        m_state.prevRegisters().r8,
                        m_state.prevRegisters().r9,
                        m_state.prevRegisters().r10,
                        m_state.prevRegisters().r11);
            ImGui::PopStyleColor();
            ImGui::Text("");

            ImGui::Text(currFmt,
                        "r12", m_state.registers().r12,
                        "r13", m_state.registers().r13,
                        "r14", m_state.registers().r14,
                        "r15", m_state.registers().r15);
            ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
            ImGui::Text(histFmt,
                        m_state.prevRegisters().r12,
                        m_state.prevRegisters().r13,
                        m_state.prevRegisters().r14,
                        m_state.prevRegisters().r15);
            ImGui::PopStyleColor();
            ImGui::Text("");
        });

        // Compute the string representation of rflags in the form:
        //  "IOPL=x [A B C ...]"
        // where A, B, C, ... are mnemonics for the different bits of
        // RFLAGS.
        // @param rflags: The value of RFLAGS to stringify.
        // @return: The string representation of rflags.
        auto const rflagsToString([](u64 const rflags) {
            // Map 2 ** i to its associated mnemonic.
            static std::map<u32, std::string> const mnemonics({
                {(1 << 21), "ID"},
                {(1 << 20), "VIP"},
                {(1 << 19), "VIF"},
                {(1 << 18), "AC"},
                {(1 << 17), "VM"},
                {(1 << 16), "RF"},
                {(1 << 14), "NT"},
                {(1 << 11), "OF"},
                {(1 << 10), "DF"},
                {(1 << 9), "IF"},
                {(1 << 8), "TF"},
                {(1 << 7), "SF"},
                {(1 << 6), "ZF"},
                {(1 << 4), "AF"},
                {(1 << 2), "PF"},
                {(1 << 0), "CF"},
            });
            u64 const iopl((rflags >> 12) & 0x3);
            std::string res("IOPL=" + std::to_string(iopl) + " [");
            bool hasFlag(false);
            // The resulting string should be reverse-sorted on the mnemonics
            // values, e.g. IF should appear before ZF and CF should always be
            // the right-most (if set). The map keeps the mnemonics sorted on
            // their value hence reverse-iterate here.
            for (auto it(mnemonics.crbegin()); it != mnemonics.crend(); ++it) {
                if ((rflags & it->first) == it->first) {
                    if (hasFlag) {
                        res += " ";
                    } else {
                        hasFlag = true;
                    }
                    res += it->second;
                }
            }
            res += "]";
            return res;
        });
        printGp();

        ImGui::Text("rip = 0x%016lx    rfl = 0x%016lx %s",
                    m_state.registers().rip,
                    m_state.registers().rflags,
                    rflagsToString(m_state.registers().rflags).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
        ImGui::Text("      0x%016lx          0x%016lx %s",
                    m_state.prevRegisters().rip,
                    m_state.prevRegisters().rflags,
                    rflagsToString(m_state.prevRegisters().rflags).c_str());
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Text("  -- Segments --");
        ImGui::Text("cs = 0x%04x  ds = 0x%04x  es = 0x%04x  fs = 0x%04x  "
                    "gs = 0x%04x  ss = 0x%04x",
                    m_state.registers().cs,
                    m_state.registers().ds,
                    m_state.registers().es,
                    m_state.registers().fs,
                    m_state.registers().gs,
                    m_state.registers().ss);
        ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
        ImGui::Text("     0x%04x       0x%04x       0x%04x       0x%04x       "
                    "0x%04x       0x%04x",
                    m_state.prevRegisters().cs,
                    m_state.prevRegisters().ds,
                    m_state.prevRegisters().es,
                    m_state.prevRegisters().fs,
                    m_state.prevRegisters().gs,
                    m_state.prevRegisters().ss);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Text("  -- Tables --");
        ImGui::Text("idt: base = 0x%016lx  limit = 0x%08x    "
                    "gdt: base = 0x%016lx  limit = 0x%08x",
                    m_state.registers().idt.base,
                    m_state.registers().idt.limit,
                    m_state.registers().gdt.base,
                    m_state.registers().gdt.limit);
        ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
        ImGui::Text("            0x%016lx          0x%08x                "
                    "0x%016lx          0x%08x",
                    m_state.prevRegisters().idt.base,
                    m_state.prevRegisters().idt.limit,
                    m_state.prevRegisters().gdt.base,
                    m_state.prevRegisters().gdt.limit);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Text("  -- Control --");
        ImGui::Text("cr0 = 0x%016lx    cr2 = 0x%016lx    cr3 = 0x%016lx",
                    m_state.registers().cr0,
                    m_state.registers().cr2,
                    m_state.registers().cr3);
        ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
        ImGui::Text("      0x%016lx          0x%016lx          0x%016lx",
                    m_state.prevRegisters().cr0,
                    m_state.prevRegisters().cr2,
                    m_state.prevRegisters().cr3);
        ImGui::PopStyleColor();
        ImGui::Text("");
        ImGui::Text("cr4 = 0x%016lx    cr8 = 0x%016lx   efer = 0x%016lx",
                    m_state.registers().cr4,
                    m_state.registers().cr8,
                    m_state.registers().efer);
        ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
        ImGui::Text("      0x%016lx          0x%016lx          0x%016lx",
                    m_state.prevRegisters().cr4,
                    m_state.prevRegisters().cr8,
                    m_state.prevRegisters().efer);
        ImGui::PopStyleColor();

        // End general purpose registers.
        ImGui::EndTabItem();
    }

    // Vector registers.
    // Vector registers are printed out in tables, the following flags control
    // the drawing of those tables.
    ImGuiTableFlags const vecRegTableFlags(ImGuiTableFlags_ScrollX |
                                           ImGuiTableFlags_BordersV);
    ImGuiTableColumnFlags const vecRegColFlags(
        ImGuiTableColumnFlags_WidthFixed);

    // FPU / MMX.
    if (ImGui::BeginTabItem("FPU & MMX", NULL, 0)) {
        // MMX registers only hold packed integers, hence override the current
        // granularity if it is set to float or double.
        VectorRegisterGranularity const granularity(
            (m_currentGranularity == VectorRegisterGranularity::Float ||
             m_currentGranularity == VectorRegisterGranularity::Double) ?
                VectorRegisterGranularity::Qword :
                m_currentGranularity);

        u32 const numElemForGran(vec64::bytes /
            granularityToBytes.at(granularity));
        // One column for the register name, one for each element in the current
        // granularity.
        u32 const numCols(1 + numElemForGran);

        if (ImGui::BeginTable("MMX", numCols, vecRegTableFlags)) {
            // Name column.
            ImGui::TableSetupColumn("Reg.", vecRegColFlags);
            // Element column, named after the index of each element in the
            // vector.
            for (u32 i(0); i < numCols - 1; ++i) {
                ImGui::TableSetupColumn(std::to_string((numCols-2) - i).c_str(),
                                        vecRegColFlags);
            }
            ImGui::TableHeadersRow();

            for (u8 i(0); i < X86Lab::Vm::State::Registers::NumMmxRegs; ++i) {
                ImGui::TableNextColumn();
                ImGui::Text("%s", ("mmx" + std::to_string(i)).c_str());
                drawColsForVec(m_state.registers().mmx[i], granularity);
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
                drawColsForVec(m_state.prevRegisters().mmx[i], granularity);
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }

        ImGui::EndTabItem();
    }

    // SSE / AVX.
    if (ImGui::BeginTabItem("SSE & AVX", NULL, 0)) {
        // If AVX-512 is available, print the zmm registers, otherwise only
        // print ymms registers.
        u32 const bytePerVec(Util::Extension::hasAvx512() ?
                             vec512::bytes : vec256::bytes);
        VectorRegisterGranularity const granularity(m_currentGranularity);
        u32 const numElemForGran(bytePerVec /
            granularityToBytes.at(granularity));
        u32 const numCols(1 + numElemForGran);

        if (ImGui::BeginTable("SSE/AVX", numCols, vecRegTableFlags)) {
            // Set up column names: regs 8 7 6 ... 0
            ImGui::TableSetupColumn("Reg.", vecRegColFlags);
            for (u32 i(0); i < numCols - 1; ++i) {
                ImGui::TableSetupColumn(std::to_string((numCols-2) - i).c_str(),
                                        vecRegColFlags);
            }
            ImGui::TableHeadersRow();

            u32 const numRegs(Util::Extension::hasAvx512() ?
                              X86Lab::Vm::State::Registers::NumZmmRegs :
                              X86Lab::Vm::State::Registers::NumYmmRegs);
            char const * const name(Util::Extension::hasAvx512()?"zmm":"ymm");
            for (u8 i(0); i < numRegs; ++i) {
                // Register name.
                ImGui::TableNextColumn();
                ImGui::Text("%s", (name + std::to_string(i)).c_str());
                if (Util::Extension::hasAvx512()) {
                    drawColsForVec(m_state.registers().zmm[i], granularity);
                } else {
                    drawColsForVec(m_state.registers().ymm[i], granularity);
                }
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
                if (Util::Extension::hasAvx512()) {
                    drawColsForVec(m_state.prevRegisters().zmm[i], granularity);
                } else {
                    drawColsForVec(m_state.prevRegisters().ymm[i], granularity);
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::End();
}

void Imgui::drawStackWin(ImGuiViewport const& viewport) {
    ImVec2 const pos(stackWinPos.x * viewport.WorkSize.x,
                     stackWinPos.y * viewport.WorkSize.y);
    ImGui::SetNextWindowPos(pos);

    // Force auto-fit for the window's size.
    ImGui::SetNextWindowSize(ImVec2(0, 0));

    // Set the constraints on the stack window. We want the window to be at
    // least as tall as the code window. The width is un-constrained, we let the
    // window be automatically sized by its content.
    ImVec2 const minSize(0.0f, codeWinSize.y * viewport.WorkSize.y);
    ImVec2 const maxSize(viewport.WorkSize.x,
                         codeWinSize.y * viewport.WorkSize.y);
    ImGui::SetNextWindowSizeConstraints(minSize, maxSize);

    ImGuiWindowFlags const winFlags(defaultWindowFlags |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Begin("Stack", NULL, winFlags);

    // The max number of lines that can fit in the stack window. We use this to
    // know how far we should read from the stack.
    // FIXME: For now we do not have a way to detect the total amount of memory
    // in the guest. Reading guest's physical memory is always valid, even when
    // out of bounds, hence always read enough bytes from the stack to fill in
    // the full window, even if this means outside the stack.
    float const winHeight(ImGui::GetWindowSize().y);
    float const padding(ImGui::GetStyle().WindowPadding.y);
    float const freeHeight(winHeight - 2 * padding);
    // After a lot of experiments, this seems to give a somewhat accurate answer
    // as to how many lines can be printed in the window. However this seems to
    // "break" when the window get's very tall, in which case it seems that we
    // can fit one more line but it isn't drawn.
    u32 const maxLines(1 + (freeHeight - ImGui::GetTextLineHeight()) /
        ImGui::GetTextLineHeightWithSpacing());

    // Print the stack's content.
    for (u32 i(0); i < maxLines; ++i) {
        u64 const disp(8 * (maxLines - 1 - i));
        u64 const offset(m_state.registers().rsp + disp);
        std::unique_ptr<u8> const raw(
            m_state.snapshot()->readPhysicalMemory(offset, 8));
        u64 const val(*reinterpret_cast<u64*>(raw.get()));
        if (!!disp) {
            ImGui::Text("0x%016lx (+0x%03lx): 0x%016lx", offset, disp, val);
        } else {
            ImGui::Text("0x%016lx (rsp ->): 0x%016lx", offset, val);
        }
    }

    // Save the stack window's size to compute the position and size of the
    // register window.
    m_stackWinSize = ImGui::GetWindowSize();
    ImGui::End();
}

void Imgui::drawLogsWin(ImGuiViewport const& viewport) {
    ImVec2 const pos(logsWinPos.x * viewport.WorkSize.x,
                     logsWinPos.y * viewport.WorkSize.y);
    ImVec2 const size(logsWinSize.x * viewport.WorkSize.x,
                      logsWinSize.y * viewport.WorkSize.y);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);

    ImGui::Begin("Logs", NULL, defaultWindowFlags);
    for (std::string& log : m_logs) {
        ImGui::Text("%s", log.c_str());
    }
    // Keep the scroll at the bottom of the box.
    ImGui::SetScrollHereY(1.0f);
    ImGui::End();
}

void Imgui::drawMemWin(ImGuiViewport const& viewport) {
    ImVec2 const pos(memWinPos.x * viewport.WorkSize.x,
                     memWinPos.y * viewport.WorkSize.y);
    ImVec2 const size(memWinSize.x * viewport.WorkSize.x,
                      memWinSize.y * viewport.WorkSize.y);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);

    ImGuiWindowFlags const winFlags(defaultWindowFlags |
                                    ImGuiWindowFlags_HorizontalScrollbar |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::Begin("Memory", NULL, winFlags);

    // FIXME: All of the code below is really hard-coded for showing 8 QWORDs
    // per row. There should be a way to dynamically change this from the IU,
    // e.g. show WORDs, DWORDs, floats, ... instead.

    // Fow now, the layout of the memory dump is hard-coded to 8 QWORDs per line
    // (e.g. a cache line). There is no plan to make this dynamic (e.g. adding a
    // shortcut to change the granularity).
    u64 const elemSize(8);
    u64 const bytesPerLine(64);
    u64 const numElems(bytesPerLine / elemSize);

    ImGuiTableFlags const tableFlags(ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_SizingFixedFit);
    ImGuiTableColumnFlags const colFlags(ImGuiTableColumnFlags_WidthFixed);

    // This rowHeight computation is a bit voodoo, but comes from the imgui
    // demo so I guess we can trust it.
    float const rowHeight(
        ImGui::GetFontSize() + ImGui::GetStyle().CellPadding.y * 2.0f);
    // The scroll position on the table is always set to a multiple of rowHeight
    // so that no row is ever clipped. However we need to be careful with the
    // last row: if the scroll position is always a multiple of rowHeight but
    // the table's height is not, then the last row will always be clipped no
    // matter what. Hence we need to choose a table height that is a multiple of
    // rowHeight as well.
    float const tableHeight(
        std::floor(ImGui::GetContentRegionAvail().y / rowHeight) * rowHeight);
    // Leave the width of the table to 0 so that it stretches over the entire
    // window's width.
    ImVec2 const outerSize(0.0f, tableHeight);

    if (ImGui::BeginTable("MemoryDump", 10, tableFlags, outerSize)) {
        // The headers of the colums are always shown.
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address", colFlags);
        ImGui::TableSetupColumn("+0x00", colFlags);
        ImGui::TableSetupColumn("+0x08", colFlags);
        ImGui::TableSetupColumn("+0x10", colFlags);
        ImGui::TableSetupColumn("+0x18", colFlags);
        ImGui::TableSetupColumn("+0x20", colFlags);
        ImGui::TableSetupColumn("+0x28", colFlags);
        ImGui::TableSetupColumn("+0x30", colFlags);
        ImGui::TableSetupColumn("+0x38", colFlags);
        ImGui::TableSetupColumn("ASCII", colFlags);
        ImGui::TableHeadersRow();

        // Make sure the scroll position is at a multiple of rowHeight.
        float const scrollY(ImGui::GetScrollY());
        ImGui::SetScrollY(std::floor(scrollY / rowHeight) * rowHeight);

        // Print a row in the table.
        // @param rowIdx: The index of the row. The resulting row will be for
        // offset = rowIdx * bytesPerLine.
        auto const printRow([&](u32 const rowIdx) {
            u64 const offset(rowIdx * bytesPerLine);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Address.
            ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
            ImGui::Text("0x%016lx", offset);
            ImGui::PopStyleColor();


            std::shared_ptr<X86Lab::Snapshot const> const s(m_state.snapshot());
            vec512 const line(s->readPhysicalMemory(offset,
                                                    bytesPerLine).get());

            // Elements at that offset.
            for (u32 i(0); i < numElems; ++i) {
                ImGui::TableNextColumn();
                ImGui::Text("%016lx", line.elem<u64>(i));
            }

            // ASCII repr.
            ImGui::TableNextColumn();
            for (u32 i(0); i < bytesPerLine; ++i) {
                // Replace non-printable char by a darkened "." char.
                char const ch(line.elem<u8>(i));
                if (std::isprint(ch)) {
                    ImGui::Text("%c", ch);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, regsOldValColor);
                    ImGui::Text(".");
                    ImGui::PopStyleColor();
                }
                // Cancel-out the newline from the previous ImGui::Text(), next
                // char is immediately following the previous one.
                ImGui::SameLine(0, 0);
            }
        });

        ImGuiListClipper clipper;
        // FIXME: What should be the limit here?
        clipper.Begin(100);
        while (clipper.Step()) {
            for (int i(clipper.DisplayStart); i < clipper.DisplayEnd; ++i) {
                printRow(i);
            }
        }


        ImGui::EndTable();

        // Draw separators between the addresses and the content and between
        // the content and the ASCII repr.
        // Note: Dear ImGui does not support only drawing borders on _some_
        // columns hence we are forced to use the DrawList API here.
        ImDrawList* const drawList(ImGui::GetWindowDrawList());
        float const paddingX(ImGui::GetStyle().CellPadding.x);
        float const paddingY(ImGui::GetStyle().CellPadding.y);
        float const addrColWidth(ImGui::CalcTextSize("0x0000000000000000").x +
            paddingX);

        ImVec2 const winPos(ImGui::GetWindowPos());
        ImVec2 const contentPos(ImGui::GetWindowContentRegionMin());
        // Table position in screen space.
        ImVec2 const tablePos(winPos.x + contentPos.x, winPos.y + contentPos.y);

        // The separator spans all displayed rows of the table, excepted the
        // first header row. Use some padding to make it pretty.
        float const sepLen(tableHeight - rowHeight - 2 * paddingY);
        // FIXME: Make this a static const in the Imgui class.
        ImVec4 const sepColor(regsOldValColor);

        // First separator.
        {
        ImVec2 const sepStart(tablePos.x + addrColWidth,
                              tablePos.y + rowHeight + paddingY);
        ImVec2 const sepEnd(sepStart.x, sepStart.y + sepLen);
        drawList->AddLine(sepStart, sepEnd, ImGui::GetColorU32(sepColor));
        }

        // Second separator.
        {
        float const valColWidth(ImGui::CalcTextSize("0000000000000000").x +
            2 * paddingX);
        ImVec2 const sepStart(tablePos.x + addrColWidth + numElems*valColWidth,
                              tablePos.y + rowHeight + paddingY);
        ImVec2 const sepEnd(sepStart.x, sepStart.y + sepLen);
        drawList->AddLine(sepStart, sepEnd, ImGui::GetColorU32(sepColor));
        }
    }

    ImGui::End();
}
}
