#include <x86lab/ui/imgui.hpp>
#include <SDL.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <span>
#include <functional>

#include <capstone/capstone.h>

namespace X86Lab::Ui {

// SDL window and renderer left to nullptr until doInit() is called on this
// backend.
Imgui::Imgui() : m_sdlWindow(nullptr), m_sdlRenderer(nullptr) {}

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

    // Initialize windows.
    m_configBar = std::make_unique<ConfigBar>();
    m_codeWindow = std::make_unique<CodeWindow>();
    m_stackWindow = std::make_unique<StackWindow>();
    m_cpuStateWindow = std::make_unique<CpuStateWindow>();
    m_memoryWindow = std::make_unique<MemoryWindow>();
    return true;
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

        Action const configAction(m_configBar->clickedAction());
        if (configAction != Action::None) {
            return configAction;
        } else if (ImGui::IsKeyPressed(ImGuiKey_S, true)) {
            return Action::Step;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R, true)) {
            return Action::ReverseStep;
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
    // We want to draw the following layout (not to scale):
    // +-----------------------------+
    // |           CONFIG            |
    // +---------+---------+---------+
    // |         |         |         |
    // |         |         |         |
    // |  CODE   |  STACK  |   REGS  |
    // |         |         |         |
    // |         |         |         |
    // +---------+---------+---------+
    // |           MEMORY            |
    // +-----------------------------+
    // FIXME: As of now there is no log window/pane. This is because it has been
    // replaced by the memory window. There is not much use for a log window but
    // in the future a dialog or pane will be added to log errors.
    ImGui_ImplSDLRenderer_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport const& viewport(*ImGui::GetMainViewport());
    ImVec2 const vpSize(viewport.WorkSize);
    ImVec2 const codeWinSize(ImVec2(0.25f, 0.70f));

    // Config bar is at (0,0) and spans the entire width of the window. The
    // height is computed to fit the content which is all in one line.
    ImVec2 const configBarPos(0.0f, 0.0f);
    ImVec2 const configBarSetupSize(vpSize.x, 0.0f);
    ImVec2 const configBarSize(m_configBar->draw(configBarPos,
        configBarSetupSize, m_state));

    // Code window
    ImVec2 const codeWindowPos(0.0f, configBarPos.y + configBarSize.y);
    ImVec2 const codeWindowSetupSize(0.0f,
        codeWinSize.y * (vpSize.y - configBarSize.y));
    ImVec2 const codeWindowSize(m_codeWindow->draw(codeWindowPos,
        codeWindowSetupSize, m_state));

    // Stack window. Must be drawn before the registers window since the
    // register window needs to know the size of the stack window to compute its
    // position.
    ImVec2 const stackWindowPos(codeWindowPos.x + codeWindowSize.x,
                                codeWindowPos.y);
    // Auto-size the window horizontally to fit content.
    ImVec2 const stackWindowSetupSize(0.0f, codeWindowSize.y);
    ImVec2 const stackWindowSize(m_stackWindow->draw(stackWindowPos,
        stackWindowSetupSize, m_state));

    // Cpu state window.
    ImVec2 const cpuStateWindowPos(stackWindowPos.x + stackWindowSize.x,
        stackWindowPos.y);
    ImVec2 const cpuStateWindowSetupSize(
        vpSize.x - codeWindowSize.x - stackWindowSize.x, codeWindowSize.y);
    ImVec2 const cpuStateWindowSize(m_cpuStateWindow->draw(cpuStateWindowPos,
        cpuStateWindowSetupSize, m_state));

    // Memory window.
    ImVec2 const memoryWindowPos(0.0f,
        cpuStateWindowPos.y + cpuStateWindowSize.y);
    ImVec2 const memoryWindowSetupSize(vpSize.x,
        vpSize.y - codeWindowSize.y - configBarSize.y);
    m_memoryWindow->draw(memoryWindowPos, memoryWindowSetupSize, m_state);

    // FIXME: For now the log window is disabled and the memory window is taking
    // its place.

    ImGui::Render();
    ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(m_sdlRenderer);
}

Imgui::Window::Window(std::string const& title, ImGuiWindowFlags const flags) :
    m_title(title), m_flags(flags) {}

ImVec2 Imgui::Window::draw(ImVec2 const& position,
                           ImVec2 const& size,
                           State const& state) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(m_title.c_str(), NULL, m_flags);
    doDraw(state);
    ImVec2 const actualSize(ImGui::GetWindowSize());
    ImGui::End();
    return actualSize;
}

template<typename T>
Imgui::Dropdown<T>::Dropdown(std::string const& label,
                             std::map<T, std::string> const& options) :
    m_label(label), m_options(options), m_childFrameWidth(0) {
    assert(m_options.size() > 0);
    m_selection = m_options.cbegin()->first;
}

template<typename T>
void Imgui::Dropdown<T>::draw() {
    // All combos are drawn in their own child frame. We do this so that we can
    // clamp the padding that is printed after the label (the lable
    // automatically added by ImGui that is).

    // Use zero padding on the child frame to keep alignment correct with
    // parent.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

    // The size of the child frame, just enought to draw the combo and clamp
    // right after the arrow button. The height is strange, I am not sure why it
    // needs to be mult by 3 and not 2, however this seems to work so I leaving
    // it at that.
    ImVec2 const padding(ImGui::GetStyle().CellPadding);
    ImVec2 const childSize(childFrameWidth(),
                           ImGui::GetFontSize() + padding.y * 3.0f);

    ImGuiWindowFlags const childFlags(ImGuiWindowFlags_NoDecoration |
                                      ImGuiWindowFlags_NoBackground);

    // The ID of the child frame is computed from the label, so technically we
    // cannot have two dropdowns with the same label in the parent. This is fine
    // though as this is a very peculiar case which does not occur in this
    // program anyway.
    ImGuiID const id(ImGui::GetID(m_label.c_str()));

    ImGui::BeginChildFrame(id, childSize, childFlags);
    ImGui::PopStyleVar();

    // Draw the label ourselves. For some reason ImGui is drawing labels on the
    // right of the combo boxes, which looks strange.
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", m_label.c_str());
    ImGui::SameLine();

    // In order to draw without the label we need to push the item width to -1.
    // See https://github.com/ocornut/imgui/issues/623.
    ImGui::PushItemWidth(-1);
    char const * const preview(m_options.at(m_selection).c_str());
    if (ImGui::BeginCombo("##dummyLabel", preview, comboFlags)) {
        // Draw all the options.
        for (auto&& it : m_options) {
            bool const isSelected(m_selection == it.first);
            if (ImGui::Selectable(it.second.c_str(), isSelected)) {
                m_selection = it.first;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
}

template<typename T>
void Imgui::Dropdown<T>::setSelection(T const& option) {
    assert(m_options.contains(option));
    m_selection = option;
}

template<typename T>
T const& Imgui::Dropdown<T>::selection() const {
    return m_selection;
}

template<typename T>
float Imgui::Dropdown<T>::childFrameWidth() {
    if (!m_childFrameWidth) {
        // The m_childFrameWidth was not yet computed.
        // Essentially the combo box is nothing more than a framed text (the
        // currently selected option) with a square button of side len = frame
        // height. We also need to take into account the size of the label.
        ImGuiStyle const& style(ImGui::GetStyle());
        float const framePaddingX(style.FramePadding.x);

        // Find the option with the longest name. We want the combo box to be
        // sized just enough to fit the longest option.
        std::vector<size_t> len;
        std::transform(m_options.cbegin(), m_options.cend(),
            std::back_inserter(len),
            [](auto& it) {return ImGui::CalcTextSize(it.second.c_str()).x;});
        // The width of the framed text for the longest option.
        float const optionMaxWidth(*std::max_element(len.cbegin(), len.cend()) +
            2 * framePaddingX);
        // The width of the label, printed manually in draw().
        float const labelWidth(ImGui::CalcTextSize(m_label.c_str()).x +
            2 * framePaddingX);
        // The width of the button. Really this should just be
        // GetFrameHeightWithSpacing, but apparently that's not it, adding the
        // * 2 works. Gotta love UI programming ...
        float const buttonWidth(ImGui::GetFrameHeightWithSpacing() * 2);
        m_childFrameWidth = labelWidth + optionMaxWidth + buttonWidth;
    }
    return m_childFrameWidth;
}

Imgui::ConfigBar::ConfigBar() :
    Window("Dummy", defaultFlags),
    m_lastAction(Action::None),
    m_startCpuMode(Vm::CpuMode::LongMode) {}

Action Imgui::ConfigBar::clickedAction() const {
    return m_lastAction;
}

void Imgui::ConfigBar::doDraw(State const& __attribute__((unused)) state) {
    // Stepping buttons + Reset.
    m_lastAction = Action::None;
    if (ImGui::Button("[s] Step")) {
        m_lastAction = Action::Step;
    }
    ImGui::SameLine();
    if (ImGui::Button("[r] Reverse step")) {
        m_lastAction = Action::ReverseStep;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset VM")) {
        m_lastAction = Action::Reset;
    }

    // Starting CPU mode radio button. Only one mode can be selected at a time.
    // Changing this mode resets the VM.
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Start CPU mode:");
    ImGui::SameLine();
    Vm::CpuMode const prevSelectedMode(m_startCpuMode);
    if (ImGui::RadioButton("16-bit real mode",
                           m_startCpuMode == Vm::CpuMode::RealMode)) {
        m_startCpuMode = Vm::CpuMode::RealMode;
        if (m_startCpuMode != prevSelectedMode) {
            m_lastAction = Action::Reset16;
        }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("32-bit protected mode",
                           m_startCpuMode == Vm::CpuMode::ProtectedMode)) {
        m_startCpuMode = Vm::CpuMode::ProtectedMode;
        if (m_startCpuMode != prevSelectedMode) {
            m_lastAction = Action::Reset32;
        }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("64-bit long mode",
                           m_startCpuMode == Vm::CpuMode::LongMode)) {
        m_startCpuMode = Vm::CpuMode::LongMode;
        if (m_startCpuMode != prevSelectedMode) {
            m_lastAction = Action::Reset64;
        }
    }
}

Imgui::CodeWindow::CodeWindow() :
    Window(defaultTitle, Imgui::defaultWindowFlags),
    m_previousRip(~((u64)0)) {
    static std::map<Format, std::string> const formatToString = {
        {Format::Source, "Source"},
        {Format::Disassembly, "Disassembly"},
    };
    m_formatDropdown = std::make_unique<Dropdown<Format>>("Code format:",
        formatToString);
}

void Imgui::CodeWindow::disassembleCode(State const& state) {
    std::shared_ptr<Snapshot const> const snap(state.snapshot());
    // First check if we need to disassemble. Most of the time we can reuse the
    // m_disassembledCode cache excepted in the following cases:
    //  - The code was never disassembled, e.g. m_disassembledCode is empty.
    //  - The cpu changed its current mode, e.g. transitioned between two of the
    //  16, 32 and 64 bit modes.
    //  - The disassembled code in m_disassembledCode does not contain the
    //  address at which RIP is currently pointing to. This would typically
    //  happen if RIP jumps in a middle of an instruction or if there is data
    //  between two instruction that is mis-interpreted by the disassembler.
    //  - Code was modified. FIXME: This is not yet suppored.
    Vm::CpuMode const cpuMode(snap->cpuMode());
    bool const cpuModeChanged(cpuMode != m_previousCpuMode);
    m_previousCpuMode = cpuMode;
    bool const needsDisassembly(cpuModeChanged || m_disassembledCode.empty()
        || !m_disassembledCode.contains(state.registers().rip));
    if (!needsDisassembly) {
        // No need to disassemble, the current contents of m_disassembledCode
        // can be used as is. Skip.
        return;
    }

    // (Re-)disassemble the code. When disassembling we start at the current
    // RIP. The code window then resets the Y scroll to show the instruction
    // under RIP at the top.

    m_disassembledCode.clear();

    csh capstoneHandle;
    cs_insn *instructions;

    cs_mode disassemblyMode;
    if (cpuMode == Vm::CpuMode::RealMode) {
        // Real-mode.
        disassemblyMode = CS_MODE_16;
    } else if (cpuMode == Vm::CpuMode::ProtectedMode) {
        // 32-bit
        disassemblyMode = CS_MODE_32;
    } else {
        disassemblyMode = CS_MODE_64;
    }

    if (cs_open(CS_ARCH_X86, disassemblyMode, &capstoneHandle) != CS_ERR_OK) {
        ImGui::Text("Failed to initialize disassembler");
        return;
    }

    if (cs_option(capstoneHandle, CS_OPT_SKIPDATA, CS_OPT_ON) != CS_ERR_OK) {
        ImGui::Text("Failed to initialize disassembler");
        return;
    }

    u64 const codeAddrStart(state.registers().rip);
    // FIXME: This might end-up disassembling a bit more than needed.
    u64 const codeSize(state.codeSize());
    std::vector<u8> const code(snap->readLinearMemory(codeAddrStart, codeSize));
    // Disassemble the instructions starting at codeAddrStart. The `0` means
    // that the disassembler stops until there is no more code to read or until
    // it encounters a broken instruction.
    size_t const instrCount(cs_disasm(capstoneHandle, code.data(), code.size(),
        codeAddrStart, 0, &instructions));

    for (size_t i(0); i < instrCount; ++i) {
        cs_insn const& ins(instructions[i]);
        u64 const insAddr(ins.address);
        std::ostringstream insBytes;
        for (int j(0); j < ins.size; ++j) {
            insBytes << std::hex << std::setw(2) << std::setfill('0');
            insBytes << u64(ins.bytes[j]) << " ";
        }
        std::ostringstream insMnemonic;
        insMnemonic << ins.mnemonic << " " << ins.op_str;
        m_disassembledCode[insAddr] =
            std::make_pair(insBytes.str(), insMnemonic.str());
    }

    cs_free(instructions, instrCount);
    cs_close(&capstoneHandle);
}

void Imgui::CodeWindow::doDraw(State const& state) {
    m_formatDropdown->draw();
    switch (m_formatDropdown->selection()) {
        case Format::Source:
            doDrawSourceFile(state);
            break;
        case Format::Disassembly:
            doDrawDisassembly(state);
            break;
        default:
            throw std::runtime_error("Invalid code window format");
    }
}

void Imgui::CodeWindow::doDrawDisassembly(State const& state) {
    // Disassemble the code and update m_disassembledCode if needed.
    disassembleCode(state);
    
    // Setup table, 3 columns: linear address, bytes, instruction.
    ImGuiTableFlags const tableFlags(ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_BordersInnerV |
                                     ImGuiTableFlags_BordersOuter |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY);
    if (!ImGui::BeginTable("CodeTable", 3, tableFlags)) {
        return;
    }

    ImGui::TableSetupColumn("Linear address");
    ImGui::TableSetupColumn("Machine code");
    ImGui::TableSetupColumn("Instruction");
    ImGui::TableHeadersRow();

    bool const isDrawingNewState(m_previousRip != state.registers().rip);
    m_previousRip = state.registers().rip;

    ImVec2 const padding(ImGui::GetStyle().CellPadding);
    float const rowHeight(ImGui::GetFontSize() + padding.y * 2.0f);
    ImDrawList* const drawList(ImGui::GetWindowDrawList());

    for (auto& elem : m_disassembledCode) {
        u64 const insAddr(elem.first);
        std::string const& insBytes(elem.second.first);
        std::string const& insOp(elem.second.second);

        ImGui::TableNextColumn();
        if (insAddr == state.registers().rip) {
            ImVec2 const cursorPos(ImGui::GetCursorScreenPos());
            ImVec2 const rectMin(cursorPos.x - padding.x,
                                 cursorPos.y - padding.y);
            float const rectWidth(ImGui::GetWindowContentRegionMax().x);
            float const rectHeight(rowHeight);
            ImVec2 const rectMax(rectMin.x + rectWidth,
                                 rectMin.y + rectHeight);
            u32 const color(ImGui::GetColorU32(currLineBgColor));
            drawList->AddRectFilled(rectMin, rectMax, color);
            if (isDrawingNewState) {
                ImGui::SetScrollHereY(0.5);
            }
        }
        ImGui::Text("0x%016lx", insAddr);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(insBytes.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(insOp.c_str());
    }

    ImGui::EndTable();
}

void Imgui::CodeWindow::doDrawSourceFile(State const& state) {
    std::string const fileName(state.sourceFileName());
    if (!fileName.size()) {
        // A the beginning of program execution, the state could be default and
        // therefore no source file set yet. In this case there is nothing to
        // draw.
        return;
    }

    ImGuiTableFlags const tableFlags(ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_BordersInnerV |
                                     ImGuiTableFlags_ScrollY);
    if (!ImGui::BeginTable("CodeTable", 2, tableFlags)) {
        return;
    }

    bool const isDrawingNewState(m_previousRip != state.registers().rip);
    m_previousRip = state.registers().rip;

    std::ifstream file(fileName, std::ios::in);
    u64 const currLine(state.currentLine());
    ImVec2 const padding(ImGui::GetStyle().CellPadding);
    float const rowHeight(ImGui::GetFontSize() + padding.y * 2.0f);
    ImDrawList* const drawList(ImGui::GetWindowDrawList());
    u64 lineNum(0);
    for (std::string line; std::getline(file, line);) {
        // Line column.
        lineNum ++;

        ImGui::TableNextColumn();
        if (lineNum == currLine) {
            if (isDrawingNewState) {
                // Center on current instruction when stepping
                // through.
                ImGui::SetScrollHereY(0.5);
            }

            // Change the background color for this line only. Unfortunately
            // there is no easy way to set a background color on a single row of
            // a table, hence we are constrained to draw a rectangle over the
            // entire row ourselves.
            ImVec2 const cursorPos(ImGui::GetCursorScreenPos());
            ImVec2 const rectMin(cursorPos.x - padding.x,
                                 cursorPos.y - padding.y);
            float const rectWidth(ImGui::GetWindowContentRegionMax().x);
            float const rectHeight(rowHeight);
            ImVec2 const rectMax(rectMin.x + rectWidth,
                                 rectMin.y + rectHeight);
            u32 const color(ImGui::GetColorU32(currLineBgColor));
            drawList->AddRectFilled(rectMin, rectMax, color);
        }

        ImGui::Text("%ld ", lineNum);

        // Instruction colum.
        ImGui::TableNextColumn();
        ImGui::Text(" %s", line.c_str());
    }
    ImGui::EndTable();
}

Imgui::StackWindow::StackWindow() : Window(defaultTitle, windowFlags),
                                    m_previousRbp(~((u64)0)),
                                    m_previousRsp(~((u64)0)) {}

void Imgui::StackWindow::updateStackFrameStartOffsets(State const& state) {
    m_stackFrameStartOffsets.clear();
    // Find all offsets corresponding to the start of stack frames by reading
    // the stack and all saved RBPs. In the worst case scenario we would need to
    // find the last maxHistory stack frames, any older frame would not show up
    // in the stack window anyway, hence skipping. This puts a bound on the
    // number of lookups however we still have the problem that we cannot
    // reliably determine what is a stack frame and what isn't: maybe the
    // application/function is not creating stack frames in a "standard" fashion
    // (e.g. good old `push  rbp ; mov rbp, rsp`)? So we might read some garbage
    // data. A rule of thumb to avoid reading too much garbage is to stop when
    // we see that a saved rbp is < rsp, such a stack frame would be invalid,
    // except in some very peculiar situation (for instance if there is a wrap
    // around when pushing on the stack); this should be sufficient for 99.99%
    // of the cases.
    u64 const rbp(state.registers().rbp);
    u64 const rsp(state.registers().rsp);
    u64 currFrameStartOffset(rbp);
    while (rsp <= currFrameStartOffset &&
           m_stackFrameStartOffsets.size() <= maxHistory) {
        m_stackFrameStartOffsets.insert(currFrameStartOffset);

        // Move to next stack frame, follow the saved RBP "linked
        // list".
        std::vector<u8> const rawRbp(state.snapshot()->readLinearMemory(
            currFrameStartOffset, sizeof(currFrameStartOffset)));
        if (rawRbp.size() != sizeof(currFrameStartOffset)) {
            // The read returned a partial buffer (or even empty). This means
            // that the linear address currFrameStartOffset is not mapped to
            // physical memory, we cannot go up the stack frames anymore.
            return;
        }
        currFrameStartOffset = *reinterpret_cast<u64 const*>(rawRbp.data());
    }
}


void Imgui::StackWindow::doDraw(State const& state) {
    if (m_previousRsp != state.registers().rsp ||
        m_previousRbp != state.registers().rbp) {
        // Re-compute the cached stack frame start offsets now that the state
        // of the stack. The condition is a bit sloppy here, we might end up
        // recomputing more than necessary, but as long as we are not missing a
        // recomputation when one is needed we are fine.
        // We _could_ keep track of the frame creation / deletion but this would
        // be error prone especially when dealing with reverse execution.
        updateStackFrameStartOffsets(state);
    }

    // There seems to be a bug in Dear ImGui where not setting ScrollX when
    // setting ScrollY on a table leads to the scroll bar overlapping with the
    // content of the table, but only when the scroll bar is towards the bottom
    // of the table.
    ImGuiTableFlags const tableFlags(ImGuiTableFlags_BordersOuter |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_BordersInnerV);
    // Specifying ScrollX/Y requires specifying the outerSize. We can leave the
    // x to 0 so that the table is auto-sized on the horizontal axis.
    // The height is set to the entire window.
    ImVec2 const outerSize(0.0f, ImGui::GetContentRegionAvail().y);
    if (!ImGui::BeginTable("StackTable", 3, tableFlags, outerSize)) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGuiTableColumnFlags const colFlags(ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Address", colFlags);
    ImGui::TableSetupColumn("Rel.", colFlags);
    ImGui::TableSetupColumn("Value", colFlags);
    ImGui::TableHeadersRow();

    u64 const rsp(state.registers().rsp);

    // Draw a separator at the top of the current row. This is done everytime
    // the start of a stack frame is detected.
    auto const showSeparatorOnCurrentRow([&]() {
        ImVec2 const cursorPos(ImGui::GetCursorScreenPos());
        ImVec2 const padding(ImGui::GetStyle().CellPadding);
        // Separator spans the entire width of the row, honoring padding.
        float const sepLen(ImGui::GetWindowContentRegionMax().x);
        ImVec2 const start(cursorPos.x, cursorPos.y - padding.y);
        ImVec2 const end(start.x + sepLen, start.y);
        ImDrawList* const drawList(ImGui::GetWindowDrawList());
        u32 const color(ImGui::GetColorU32(frameSeparatorColor));
        drawList->AddLine(start, end, color, frameSeparatorThickness);
    });

    // Compute the memory offset associated with a particular row given its id.
    // Row with id 0 shows the oldest entry in the stack (e.g. the furthest away
    // from current rsp).
    // The row with max id (this id depends on the size) shows the latest entry
    // in the stack, e.g. value pointed by rsp.
    // @param rowId: The index of the row.
    // @return: 64-bit offset associated with this row.
    auto const rowIdToOffset([&](u32 const rowId) {
        return rsp + (maxHistory - rowId - 1) * 8;
    });

    // Check if the row with id `rowId` has an offset which coincides with the
    // start of a stack frame. More specifically this checks of
    // rowIdToOffset(rowId) is the beginning of any stack frame in the stack.
    // @param rowId: The id of the row to test.
    // @return: true if the row is the start of a frame, false otherwise.
    auto const isRowStartOfStackFrame([&](u32 const rowId) {
        u64 const rowOffset(rowIdToOffset(rowId));
        return m_stackFrameStartOffsets.contains(rowOffset);
    });

    // Print a row of the table showing the stack's content.
    // @param rowId: The index of the row in the table.
    auto const printRow([&](u32 const rowId) {
        u64 const currOffset(rowIdToOffset(rowId));

        std::vector<u8> const raw(
            state.snapshot()->readPhysicalMemory(currOffset, 8));
        u64 const val(*reinterpret_cast<u64 const*>(raw.data()));

        // Start new row.
        ImGui::TableNextColumn();
        if (isRowStartOfStackFrame(rowId)) {
            // Add a separator at the start of each stack frame.
            showSeparatorOnCurrentRow();
        }

        // Address column.
        ImGui::PushStyleColor(ImGuiCol_Text, addrColor);
        ImGui::Text("0x%016lx", currOffset);
        ImGui::PopStyleColor();

        // Relative offset from rsp column.
        ImGui::TableNextColumn();
        if (currOffset == rsp) {
            ImGui::Text("     rsp ->");
        } else {
            u64 const relativeOffset(currOffset - rsp);
            ImGui::Text("rsp + 0x%04lx", relativeOffset);
        }

        // Value column.
        ImGui::TableNextColumn();
        ImGui::Text("0x%016lx", val);
    });

    ImGuiListClipper clipper;
    clipper.Begin(maxHistory);
    while (clipper.Step()) {
        for (int i(clipper.DisplayStart); i < clipper.DisplayEnd; ++i) {
            printRow(i);
        }
    }

    // Reset the scroll to show the current RSP after each push / pop.
    if (m_previousRsp != state.registers().rsp) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
    }

    ImGui::EndTable();

    m_previousRsp = state.registers().rsp;
    m_previousRbp = state.registers().rbp;
}

std::map<Imgui::Granularity, u32> const Imgui::granularityToBytes = {
    {Imgui::Granularity::Byte,   1},
    {Imgui::Granularity::Word,   2},
    {Imgui::Granularity::Dword,  4},
    {Imgui::Granularity::Qword,  8},
    {Imgui::Granularity::Float,  4},
    {Imgui::Granularity::Double, 8},
};

std::map<Imgui::DisplayFormat, std::string> const Imgui::formatToString = {
    {Imgui::DisplayFormat::Hexadecimal, "Hexadecimal"},
    {Imgui::DisplayFormat::SignedDecimal, "Signed decimal"},
    {Imgui::DisplayFormat::UnsignedDecimal, "Unsigned decimal"},
    {Imgui::DisplayFormat::FloatingPoint, "Floating point"},
};

// This is essentially as cross product of DisplayFormat and {8, 16, 32, 64}
// except that floating point values are only supported for 32 and 64 bit
// widths.
std::map<std::pair<Imgui::DisplayFormat, u8>, char const*> const
Imgui::displayFormatAndBitsToFormatString = {
    {std::make_pair(Imgui::DisplayFormat::Hexadecimal,  8), "0x%02x"},
    {std::make_pair(Imgui::DisplayFormat::Hexadecimal, 16), "0x%04x"},
    {std::make_pair(Imgui::DisplayFormat::Hexadecimal, 32), "0x%08x"},
    {std::make_pair(Imgui::DisplayFormat::Hexadecimal, 64), "0x%016lx"},

    {std::make_pair(Imgui::DisplayFormat::SignedDecimal,  8), "%hhd"},
    {std::make_pair(Imgui::DisplayFormat::SignedDecimal, 16), "%hd"},
    {std::make_pair(Imgui::DisplayFormat::SignedDecimal, 32), "%d"},
    {std::make_pair(Imgui::DisplayFormat::SignedDecimal, 64), "%ld"},

    {std::make_pair(Imgui::DisplayFormat::UnsignedDecimal,  8), "%hhu"},
    {std::make_pair(Imgui::DisplayFormat::UnsignedDecimal, 16), "%hu"},
    {std::make_pair(Imgui::DisplayFormat::UnsignedDecimal, 32), "%u"},
    {std::make_pair(Imgui::DisplayFormat::UnsignedDecimal, 64), "%lu"},

    {std::make_pair(Imgui::DisplayFormat::FloatingPoint, 32), "%f"},
    {std::make_pair(Imgui::DisplayFormat::FloatingPoint, 64), "%f"},
};

Imgui::CpuStateWindow::CpuStateWindow() :
    Window(defaultTitle, Imgui::defaultWindowFlags) {
    m_gpFormatDropdown = std::make_unique<Dropdown<DisplayFormat>>(
        "Value format:", formatToString);

    // MMX registers can only hold integer values, hence no Float or Double
    // options. Additionally, an MMX register cannot hold a packed qword.
    static std::map<Granularity, std::string> const mmxGranularityOpt({
        {Granularity::Byte  , "Packed bytes"},
        {Granularity::Word  , "Packed words"},
        {Granularity::Dword , "Packed double-words"},
    });
    m_mmxGranularityDropdown = std::make_unique<Dropdown<Granularity>>(
        "Vector format:", mmxGranularityOpt);

    static std::map<DisplayFormat, std::string> const mmxDisplayFormatOpt({
        {Imgui::DisplayFormat::Hexadecimal, "Hexadecimal"},
        {Imgui::DisplayFormat::SignedDecimal, "Signed decimal"},
        {Imgui::DisplayFormat::UnsignedDecimal, "Unsigned decimal"},
    });
    m_mmxFormatDropdown = std::make_unique<Dropdown<DisplayFormat>>(
        "Value format:", mmxDisplayFormatOpt);

    // SSE and AVX registers can hold any type.
    static std::map<Granularity, std::string> const sseGranularityOpt({
        {Granularity::Byte,   "Packed bytes"},
        {Granularity::Word,   "Packed words"},
        {Granularity::Dword,  "Packed double-words"},
        {Granularity::Qword,  "Packed quad-words"},
        {Granularity::Float,  "Packed floats"},
        {Granularity::Double, "Packed doubles"},
    });
    m_sseAvxGranularityDropdown = std::make_unique<Dropdown<Granularity>>(
        "Vector format:", sseGranularityOpt);

    // We only use the display format dropdown when dealing with packed
    // (unsigned) integers. When packed floats or packed doubles is selected,
    // the display format is forced to DisplayFormat::FloatingPoint.
    // Therefore we can share the map with mmx registers.
    static std::map<DisplayFormat, std::string> const& sseDisplayFormatOpt(
        mmxDisplayFormatOpt);
    m_sseAvxFormatDropdown = std::make_unique<Dropdown<DisplayFormat>>(
        "Value format:", sseDisplayFormatOpt);
}

template<size_t W>
void Imgui::CpuStateWindow::drawColsForVec(vec<W> const& vec,
                                           Granularity const granularity,
                                           DisplayFormat const displayFormat) {
    u32 const numElems(vec.bytes / granularityToBytes.at(granularity));
    char const * const fmt(displayFormatAndBitsToFormatString.at(
        std::make_pair(displayFormat, granularityToBytes.at(granularity) * 8)));
    for (int i(numElems - 1); i >= 0; --i) {
        ImGui::TableNextColumn();
        switch (granularity) {
            case Granularity::Byte:
                ImGui::Text(fmt, vec.template elem<u8>(i));
                break;
            case Granularity::Word:
                ImGui::Text(fmt, vec.template elem<u16>(i));
                break;
            case Granularity::Dword:
                ImGui::Text(fmt, vec.template elem<u32>(i));
                break;
            case Granularity::Qword:
                ImGui::Text(fmt, vec.template elem<u64>(i));
                break;
            case Granularity::Float:
                ImGui::Text(fmt, vec.template elem<float>(i));
                break;
            case Granularity::Double:
                ImGui::Text(fmt, vec.template elem<double>(i));
                break;
            default:
                throw std::runtime_error("Invalid granularity");
        }
    }
}

void Imgui::CpuStateWindow::doDraw(State const& state) {
    ImGui::BeginTabBar("##tabs", 0);

    if (ImGui::BeginTabItem("General purpose regs.", NULL, 0)) {
        doDrawGeneralPurpose(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("FPU & MMX regs.", NULL, 0)) {
        doDrawFpuMmx(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("SSE & AVX regs.", NULL, 0)) {
        doDrawSseAvx(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Page tables", NULL, 0)) {
        doDrawPageTables(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("GDT", NULL, 0)) {
        doDrawGdt(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("IDT", NULL, 0)) {
        doDrawIdt(state);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

void Imgui::CpuStateWindow::doDrawGeneralPurpose(State const& state) {
    Snapshot::Registers const& regs(state.registers());
    Snapshot::Registers const& prevRegs(state.prevRegisters());

    // All tables are using the same flags.
    ImGuiTableFlags const tableFlags(ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_SizingFixedFit);
    // This rowHeight computation is a bit voodoo, but comes from the imgui
    // demo so I guess we can trust it.
    ImGuiStyle const& style(ImGui::GetStyle());
    // Row height for all the tables in this tab.
    float const rowHeight(ImGui::GetFontSize() + style.CellPadding.y * 2.0f);
    // The column containing 64-bit register values have a fixed width that can
    // contain the largest 64-bit (un-)signed value. We do this so that switch
    // between hex and decimal keeps a consistent alignment and avoids very
    // narow colums with small values.
    float const valueColWidth(ImGui::CalcTextSize("+18446744073709551615").x);

    // First table: General purpose registers rax, rbx, ..., r14, r15.
    // Use layout 4 x 4 hence 4 rows and 3 spacing / empty rows. Fix the table
    // height so it does not cover the entire window, we need the space for the
    // next tables! Rows containing values also contain the previous values
    // hence having double the height as empty rows.
    // Each row of the table has the following layout:
    //   |rax|=|<value>|rbx|=|<value>|rcx|=|<value>|rdx|=|<value>|
    //   |   | |<value>|   | |<value>|   | |<value>|   | |<value>|
    // therefore need 12 columns.
    float const table1Height((4 * 2 + 3) * rowHeight);
    ImGui::Text("  -- General Purpose --");
    // Draw the display format drop down.
    m_gpFormatDropdown->draw();
    if (ImGui::BeginTable("##GP1", 12, tableFlags, ImVec2(0, table1Height))) {

        // Changing the format only affect registers rax through r15 including
        // rsp and rbp. Rip and rflags are not affected and always printed in
        // hex format.
        DisplayFormat const format(m_gpFormatDropdown->selection());
        // The format string to use when printing rax ... r15.
        char const * const fmtStr64(displayFormatAndBitsToFormatString.at(
            std::make_pair(format, 64)));

        // Setup the colums width so that colums containing register values have
        // a fixed size that is large enough to contain the biggest 64 bit
        // value.
        ImGuiTableColumnFlags const colFlags(0);
        for (u32 i(0); i < 4; ++i) {
            ImGui::TableSetupColumn("#", colFlags, 0);
            ImGui::TableSetupColumn("#", colFlags, 0);
            ImGui::TableSetupColumn("#", colFlags, valueColWidth);
        }

        // Print columns for a given registers containing current value and the
        // previous one.
        // Notice the std::bit_cast for the FloatingPoint format, this cast is
        // required because printing "%f" while passing a u64 cannot work: the
        // u64 would be passed through RSI instead of XMM0 as a double would do,
        // hence the Text() would read garbage from XMM0! Don't you just love
        // ABI's ?
#define PRINT_REG(regName)                                                     \
        do {                                                                   \
            ImGui::TableNextColumn();                                          \
            ImGui::Text(#regName);                                             \
            ImGui::TableNextColumn();                                          \
            ImGui::Text("=");                                                  \
            ImGui::TableNextColumn();                                          \
            if (format == DisplayFormat::FloatingPoint) {                      \
                ImGui::Text(fmtStr64, std::bit_cast<double>(regs.regName));    \
            } else {                                                           \
                ImGui::Text(fmtStr64, regs.regName);                           \
            }                                                                  \
            ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);                 \
            if (format == DisplayFormat::FloatingPoint) {                      \
                ImGui::Text(fmtStr64, std::bit_cast<double>(prevRegs.regName));\
            } else {                                                           \
                ImGui::Text(fmtStr64, prevRegs.regName);                       \
            }                                                                  \
            ImGui::PopStyleColor();                                            \
        } while (0)

        PRINT_REG(rax);
        PRINT_REG(rbx);
        PRINT_REG(rcx);
        PRINT_REG(rdx);

        // Spacing.
        ImGui::TableNextColumn();
        ImGui::Text(" ");
        ImGui::TableNextRow();

        PRINT_REG(rsi);
        PRINT_REG(rdi);
        PRINT_REG(rsp);
        PRINT_REG(rbp);

        // Spacing.
        ImGui::TableNextColumn();
        ImGui::Text(" ");
        ImGui::TableNextRow();

        PRINT_REG(r8);
        PRINT_REG(r9);
        PRINT_REG(r10);
        PRINT_REG(r11);

        // Spacing.
        ImGui::TableNextColumn();
        ImGui::Text(" ");
        ImGui::TableNextRow();

        PRINT_REG(r12);
        PRINT_REG(r13);
        PRINT_REG(r14);
        PRINT_REG(r15);

        ImGui::EndTable();
    }

    // Compute the string representation of rflags in the form:
    //  "IOPL=x [A B C ...]"
    // where A, B, C, ... are mnemonics for the different bits of
    // RFLAGS.
    // @param rflags: The value of RFLAGS to stringify.
    // @return: The string representation of rflags.
    auto const rflagsToString([](u64 const rflags) {
        // Map 2 ** i to its associated mnemonic.
        static std::map<u32, std::string> const mnemonics({
            {(1 << 21), "ID"}, {(1 << 20), "VIP"}, {(1 << 19), "VIF"},
            {(1 << 18), "AC"}, {(1 << 17), "VM"},  {(1 << 16), "RF"},
            {(1 << 14), "NT"}, {(1 << 11), "OF"},  {(1 << 10), "DF"},
            {(1 << 9), "IF"},  {(1 << 8), "TF"},   {(1 << 7), "SF"},
            {(1 << 6), "ZF"},  {(1 << 4), "AF"},   {(1 << 2), "PF"},
            {(1 << 0), "CF"},
        });
        u64 const iopl((rflags >> 12) & 0x3);
        std::string res("IOPL=" + std::to_string(iopl) + " [");
        bool hasFlag(false);
        // The resulting string should be reverse-sorted on the mnemonics
        // values, e.g. IF should appear before ZF and CF should always be the
        // right-most (if set). The map keeps the mnemonics sorted on their
        // value hence reverse-iterate here.
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

    // Remaining GP registers are RIP and RFLAGS. RFLAGS is pretty printed (we
    // show individual flags) therefore its string representation is large,
    // larger than 64-bit values. We print RFLAGS (and RIP) in a separate table
    // so that RFLAGS does not mess-up the aligment.
    // Only a single row for this table, but this row contains both the current
    // and previous values of RIP and RFLAGS hence the double height.
    // Table has the following layout.
    //   |rip|=|<value>|rfl|=|<value> <str>|
    //   |   | |<value>|   | |<value> <str>|
    // hence 6 columns.
    float const table2Height(2 * rowHeight);
    if (ImGui::BeginTable("##GP2", 6, tableFlags, ImVec2(0, table2Height))) {
        // Setup the column width for the one containing the rip value, we want
        // to use the same width as the registers of the previous table so we
        // keep everything correctly aligned on the "=". Note: We need to call
        // TableSetupColumn on the columns before the one of interest.
        ImGuiTableColumnFlags const colFlags(0);
        ImGui::TableSetupColumn("#", colFlags, 0);
        ImGui::TableSetupColumn("#", colFlags, 0);
        ImGui::TableSetupColumn("#", colFlags, valueColWidth);

        ImGui::TableNextColumn();
        ImGui::Text("rip");
        ImGui::TableNextColumn();
        ImGui::Text("=");
        ImGui::TableNextColumn();
        ImGui::Text("0x%016lx", regs.rip);
        ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);
        ImGui::Text("0x%016lx", prevRegs.rip);
        ImGui::PopStyleColor();

        ImGui::TableNextColumn();
        ImGui::Text("rfl");
        ImGui::TableNextColumn();
        ImGui::Text("=");
        ImGui::TableNextColumn();
        u64 const rflg(state.registers().rflags);
        u64 const prevRflg(state.prevRegisters().rflags);
        ImGui::Text("0x%016lx %s", rflg, rflagsToString(rflg).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);
        ImGui::Text("0x%016lx %s", prevRflg, rflagsToString(prevRflg).c_str());
        ImGui::PopStyleColor();

        ImGui::EndTable();
    }

    // Segment registers, simple layout on a single row.
    ImGui::Separator();
    ImGui::Text("  -- Segments --");
    // Use a 1 x 6 layout, single double-height row.
    float const table3Height(table2Height);
    if (ImGui::BeginTable("##GP3", 18, tableFlags, ImVec2(0, table3Height))) {
#undef PRINT_REG
#define PRINT_REG(regName)                                      \
        do {                                                    \
            ImGui::TableNextColumn();                           \
            ImGui::Text(#regName);                              \
            ImGui::TableNextColumn();                           \
            ImGui::Text("=");                                   \
            ImGui::TableNextColumn();                           \
            ImGui::Text("0x%04x", regs.regName);                \
            ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);  \
            ImGui::Text("0x%04x", prevRegs.regName);            \
            ImGui::PopStyleColor();                             \
        } while (0)

        PRINT_REG(cs);
        PRINT_REG(ds);
        PRINT_REG(es);
        PRINT_REG(fs);
        PRINT_REG(gs);
        PRINT_REG(ss);

        ImGui::EndTable();
    }

    // IDT and GDT. As before simple layout with a single row.
    ImGui::Separator();
    ImGui::Text("  -- Tables --");
    // Use a 1 x 4 layout (base and limit for both IDT and GDT), single
    // double-height row.
    float const table4Height(table3Height);
    if (ImGui::BeginTable("##GP4", 12, tableFlags, ImVec2(0, table4Height))) {
#undef PRINT_REG
#define PRINT_REG(regName)                                      \
        do {                                                    \
            ImGui::TableNextColumn();                           \
            ImGui::Text(#regName ": base");                     \
            ImGui::TableNextColumn();                           \
            ImGui::Text("=");                                   \
            ImGui::TableNextColumn();                           \
            ImGui::Text("0x%016lx", regs.regName.base);         \
            ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);  \
            ImGui::Text("0x%016lx", prevRegs.regName.base);     \
            ImGui::PopStyleColor();                             \
                                                                \
            ImGui::TableNextColumn();                           \
            ImGui::Text("limit");                               \
            ImGui::TableNextColumn();                           \
            ImGui::Text("=");                                   \
            ImGui::TableNextColumn();                           \
            ImGui::Text("0x%08x", regs.regName.limit);          \
            ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);  \
            ImGui::Text("0x%08x", prevRegs.regName.limit);      \
            ImGui::PopStyleColor();                             \
        } while (0)

        PRINT_REG(idt);
        PRINT_REG(gdt);
        ImGui::EndTable();
    }

    // Control registers.
    ImGui::Separator();
    ImGui::Text("  -- Control --");
    // Use 3 cols and 2 double-height rows.
    float const table5Height((2 * 2) * rowHeight);
    if (ImGui::BeginTable("##GP5", 9, tableFlags, ImVec2(0, table5Height))) {
#undef PRINT_REG
#define PRINT_REG(regName)                                      \
        do {                                                    \
            ImGui::TableNextColumn();                           \
            ImGui::Text(#regName);                              \
            ImGui::TableNextColumn();                           \
            ImGui::Text("=");                                   \
            ImGui::TableNextColumn();                           \
            ImGui::Text("0x%016lx", regs.regName);              \
            ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);  \
            ImGui::Text("0x%016lx", prevRegs.regName);          \
            ImGui::PopStyleColor();                             \
        } while (0)

        PRINT_REG(cr0);
        PRINT_REG(cr2);
        PRINT_REG(cr3);
        PRINT_REG(cr4);
        PRINT_REG(cr8);
        PRINT_REG(efer);

        ImGui::EndTable();
    }
}

void Imgui::CpuStateWindow::doDrawFpuMmx(State const& state) {
    m_mmxGranularityDropdown->draw();
    ImGui::SameLine();
    m_mmxFormatDropdown->draw();
    Granularity const granularity(m_mmxGranularityDropdown->selection());
    DisplayFormat const dispFmt(m_mmxFormatDropdown->selection());

    u32 const numElemForGran(vec64::bytes / granularityToBytes.at(granularity));
    // One column for the register name, one for each element in the current
    // granularity.
    u32 const numCols(1 + numElemForGran);

    ImGuiTableFlags const tableFlags(ImGuiTableFlags_BordersOuter |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_BordersInnerV);
    ImGuiTableColumnFlags const colFlags(ImGuiTableColumnFlags_WidthFixed);

    ImGuiStyle const& style(ImGui::GetStyle());
    float const rowHeight(ImGui::GetFontSize() + style.CellPadding.y * 2.0f);
    ImVec2 const outerSize(0, (2*Snapshot::Registers::NumMmxRegs+1)*rowHeight);
    if (!ImGui::BeginTable("MMX", numCols, tableFlags, outerSize)) {
        return;
    }

    // Name column.
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Reg.", colFlags);
    // Element column, named after the index of each element in the vector.
    for (u32 i(0); i < numCols - 1; ++i) {
        ImGui::TableSetupColumn(std::to_string((numCols-2) - i).c_str(),
                                colFlags);
    }
    ImGui::TableHeadersRow();

    for (u8 i(0); i < X86Lab::Vm::State::Registers::NumMmxRegs; ++i) {
        ImGui::TableNextColumn();
        ImGui::Text("%s", ("mm" + std::to_string(i)).c_str());
        drawColsForVec(state.registers().mmx[i], granularity, dispFmt);
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);
        drawColsForVec(state.prevRegisters().mmx[i], granularity, dispFmt);
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

void Imgui::CpuStateWindow::doDrawSseAvx(State const& state) {
    m_sseAvxGranularityDropdown->draw();
    Granularity const gran(m_sseAvxGranularityDropdown->selection());
    DisplayFormat dispFmt;
    if (gran != Granularity::Float && gran != Granularity::Double) {
        // When the granularity is set on packed integers, propose to change the
        // display format.
        ImGui::SameLine();
        m_sseAvxFormatDropdown->draw();
        dispFmt = m_sseAvxFormatDropdown->selection();
    } else {
        // For packed floats and doubles the format is set to FloatingPoint.
        dispFmt = DisplayFormat::FloatingPoint;
    }

    // Table and column flags for mask and vector registers.
    ImGuiTableFlags const tableFlags(ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_BordersInnerV);
    ImGuiTableColumnFlags const colFlags(ImGuiTableColumnFlags_WidthFixed);
    // This rowHeight computation is a bit voodoo, but comes from the imgui
    // demo so I guess we can trust it.
    ImGuiStyle const& style(ImGui::GetStyle());
    float const rowHeight(ImGui::GetFontSize() + style.CellPadding.y * 2.0f);

    Snapshot::Registers const& regs(state.registers());

    // MXCSR and mask registers (only if AVX-512 is supported).
    bool const hasAvx512(Util::Extension::hasAvx512());
    ImGui::Text("mxcsr = 0x%08x", regs.mxcsr);
    if (hasAvx512) {
        // For the mask registers we use a 2 rows x 4 cols layout like so:
        //   |k0|=|<value>|k1|=|<value>|k2|=|<value>|k3|=|<value>|
        //   |k4|=|<value>|k5|=|<value>|k6|=|<value>|k7|=|<value>|
        // In order to save space we don't print the previous values of mask
        // registers.
        // Need to set the table height so that it does not span the entire
        // window height.
        ImVec2 const outerSize(0.0f, 2 * rowHeight);
        ImGuiTableFlags const flags(tableFlags ^ ImGuiTableFlags_BordersInnerV);
        if (ImGui::BeginTable("Mask regs", 12, flags, outerSize)) {
            for (u32 i(0); i < Snapshot::Registers::NumKRegs; ++i) {
                std::string const name("k" + std::to_string(i));
                ImGui::TableNextColumn();
                ImGui::Text("%s", name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("=");
                ImGui::TableNextColumn();
                ImGui::Text("0x%016lx", regs.k[i]);
            }

            ImGui::EndTable();
        }
    }

    // If AVX-512 is available, print the zmm registers, otherwise only
    // print ymms registers.
    u32 const bytePerVec(hasAvx512 ? vec512::bytes : vec256::bytes);
    u32 const numElemForGran(bytePerVec / granularityToBytes.at(gran));
    u32 const numCols(1 + numElemForGran);

    u32 const numRegs(Util::Extension::hasAvx512() ?
                      X86Lab::Vm::State::Registers::NumZmmRegs :
                      X86Lab::Vm::State::Registers::NumYmmRegs);

    if (!ImGui::BeginTable("SSE/AVX", numCols, tableFlags)) {
        return;
    }

    // Set up column names: regs 8 7 6 ... 0
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Reg.", colFlags);
    for (u32 i(0); i < numCols - 1; ++i) {
        ImGui::TableSetupColumn(std::to_string((numCols-2) - i).c_str(),
                                colFlags);
    }
    ImGui::TableHeadersRow();

    char const * const name(Util::Extension::hasAvx512()?"zmm":"ymm");
    for (u8 i(0); i < numRegs; ++i) {
        // Register name.
        ImGui::TableNextColumn();
        ImGui::Text("%s", (name + std::to_string(i)).c_str());
        if (Util::Extension::hasAvx512()) {
            drawColsForVec(regs.zmm[i], gran, dispFmt);
        } else {
            drawColsForVec(regs.ymm[i], gran, dispFmt);
        }
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, oldValColor);
        if (Util::Extension::hasAvx512()) {
            drawColsForVec(state.prevRegisters().zmm[i], gran, dispFmt);
        } else {
            drawColsForVec(state.prevRegisters().ymm[i], gran, dispFmt);
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

struct Entry {
    union {
        struct {
            bool present : 1;
            bool writable : 1;
            bool userpage : 1;
            bool writeThrough : 1;
            bool cacheDisable : 1;
            bool accessed : 1;
            bool dirty : 1;
            bool pat : 1;
            bool global : 1;
            u16 : 3;
            // Technically the offset is not 51 bits but ~48 bits. That's fine,
            // the unused bits are 0 anyway (I think!).
            u64 next : 51;
            bool executeDisable : 1;
        } __attribute__((packed));
        // The raw value of the entry.
        uint64_t raw;
    } __attribute__((packed));

    u64 nextTableOffset() const {
        return next << 12;
    }
} __attribute__((packed));
static_assert(sizeof(Entry) == sizeof(uint64_t));

void Imgui::CpuStateWindow::doDrawPageTables(State const& state) {
    ImGuiTableFlags const tableFlags(ImGuiTableFlags_BordersOuter |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_BordersInnerV);
    // Entry desc | L addr start | L addr end | P addr start | <attrs ...>
    // The attributes are the various bits of the mapping, which add the
    // following cols: XD | G | PAT | D | A | PCD | PWT | U/S | R/W | Entry raw
    // The present bit is implicitly implied.
    if (!ImGui::BeginTable("##pagetable", 14, tableFlags)) {
        return;
    }

    ImGuiTableColumnFlags const colFlags(ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Entry", colFlags);
    ImGui::TableSetupColumn("Start linear addr.", colFlags);
    ImGui::TableSetupColumn("End linear addr.", colFlags);
    ImGui::TableSetupColumn("Mapped phy. addr.", colFlags);
    ImGui::TableSetupColumn("ExD", colFlags);
    ImGui::TableSetupColumn(" G ", colFlags);
    ImGui::TableSetupColumn("PAT", colFlags);
    ImGui::TableSetupColumn(" D ", colFlags);
    ImGui::TableSetupColumn(" A ", colFlags);
    ImGui::TableSetupColumn("PCD", colFlags);
    ImGui::TableSetupColumn("PWT", colFlags);
    ImGui::TableSetupColumn("U/S", colFlags);
    ImGui::TableSetupColumn("R/W", colFlags);
    ImGui::TableSetupColumn("Entry raw value", colFlags);
    ImGui::TableHeadersRow();

    // Compute the canonical form of an address.
    // @param addr: The address to canonicalize.
    // @return: The canonical form of `addr` where bits 48 ... 63 are set to the
    // same value of bit 47.
    // FIXME: We assume that the guest is using 48-bit virtual addresses here.
    // We should compute that instead.
    auto const canonicalize([](u64 const addr) {
        u64 const mask(~0ULL << 48);
        if (addr & (1ULL << 47)) {
            return addr | mask;
        } else {
            return addr & ~mask;
        }
    });

    // Get the name of particular level of page table.
    auto const nameForLevel([](u64 const L) {
        assert(0 <= L && L < 4);
        switch (L) {
            case 0:
                return std::string("Page Frame");
            case 1:
                return std::string("Page Table");
            case 2:
                return std::string("Page Dir");
            case 3:
                return std::string("Page Dir Ptr");
            default:
                throw std::runtime_error("Invalid level");
        }
    });

    // Get the name of an entry of a particular page table level.
    auto const entryNameForLevel([](u64 const L) {
        assert(0 < L && L <= 4);
        switch (L) {
            case 1:
                return std::string("PTE");
            case 2:
                return std::string("PDE");
            case 3:
                return std::string("PDPTE");
            case 4:
                return std::string("PML4E");
            default:
                throw std::runtime_error("Invalid level");
        }
    });

    std::shared_ptr<X86Lab::Snapshot const> const s(state.snapshot());

    // Print the page tables recursively.
    // @param L: The level of the page table to be printed, 4 corresponds to a
    // PML4, 3 to a PDP, ...
    // @param tablePhyAddr: The physical address of the page table to be
    // printed. This is where the entries are read from.
    // @param tableStartLinAddr: The smallest linear address covered by this
    // table.
    std::function<void(u64, u64, u64)> printTables;
    printTables = [&](u64 const L,
                      u64 const tablePhyAddr,
                      u64 const tableStartLinAddr) {
        // FIXME: This is assuming 64-bit paging.
        static constexpr u64 entriesPerTable(512);
        static constexpr u64 tableSize(PAGE_SIZE);

        // Read the page table into a std::span of Entry.
        std::vector<u8> const bytes(s->readPhysicalMemory(tablePhyAddr,
                                                          tableSize));
        Entry const * const raw(reinterpret_cast<Entry const*>(bytes.data()));
        std::span<Entry const> const entries(raw, entriesPerTable);

        // Print the entries of this table, one per row. If this is anything
        // higher than a page-table (e.g. L > 1) then each entry can be opened
        // to reveal nested tables.
        for (u64 i(0); i < entriesPerTable; ++i) {
            Entry const& entry(entries[i]);

            // When not present the entire row gets darkened.
            if (!entry.present) {
                ImGui::PushStyleColor(ImGuiCol_Text, unmappedColor);
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Column 1: Entry name + next table physical address. For entries
            // of a page-table we don't print the physical address as it will be
            // printed in its own column.
            std::ostringstream oss;
            oss << entryNameForLevel(L) << " " << i << ": ";
            oss << nameForLevel(L - 1);
            if (L > 1) {
                oss << " @ 0x" << std::hex << entry.nextTableOffset();
            }
            // Page frames and entries that are not present are considered leaf
            // nodes.
            ImGuiTreeNodeFlags const flags(ImGuiTreeNodeFlags_SpanFullWidth |
                ((L == 1 || !entry.present) ? ImGuiTreeNodeFlags_Leaf : 0));
            bool const opened(ImGui::TreeNodeEx(oss.str().c_str(), flags));

            // Column 2: Start linear address.
            // The amount of memory this entry is spanning over. This is
            // essentially PAGE_SIZE * (pow(512, L-1)). To avoid using floating
            // point we convert the pow to shifthing: pow(512, L-1) ==
            // pow(1<<9,L-1) == 1 << (9*L-1).
            u64 const entrySpan(PAGE_SIZE * (1 << (9 * (L - 1))));
            u64 const entryStartLinAddr(
                canonicalize(tableStartLinAddr + i * entrySpan));
            ImGui::TableNextColumn();
            ImGui::Text("0x%016lx", entryStartLinAddr);

            // Column 4: End linear address.
            u64 const entryEndLinAddr(entryStartLinAddr + entrySpan - 1);
            ImGui::TableNextColumn();
            ImGui::Text("0x%016lx", entryEndLinAddr);

            // Column 5: Mapped physical address, only for the lower level, e.g.
            // page table level.
            if (L == 1 && entry.present) {
                u64 const mappedPhyAddr(entry.nextTableOffset());
                ImGui::TableNextColumn();
                ImGui::Text("0x%016lx", mappedPhyAddr);
            } else {
                // Entries are are not present and entries corresponding to
                // anything other than a page frames do not have a mapped
                // physical address.
                ImGui::TableNextColumn();
                if (L > 1) {
                    ImGui::PushStyleColor(ImGuiCol_Text, unmappedColor);
                    ImGui::Text("--");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::Text("--");
                }
            }

            // Remaining columns: attributes.
            // G | PAT | D | A | PCD | PWT | U/S | R/W
            // Helper macro to print attribute values.
            #define PRINT_ATTRIBUTE(attrName) \
                do { \
                    ImGui::TableNextColumn(); \
                    if (entry.attrName) { \
                        ImGui::TextUnformatted(" 1 "); \
                    } else { \
                        ImGui::PushStyleColor(ImGuiCol_Text, unmappedColor); \
                        ImGui::TextUnformatted(" 0 "); \
                        ImGui::PopStyleColor(); \
                    } \
                } while(0)
            PRINT_ATTRIBUTE(executeDisable);
            PRINT_ATTRIBUTE(global);
            PRINT_ATTRIBUTE(pat);
            PRINT_ATTRIBUTE(dirty);
            PRINT_ATTRIBUTE(accessed);
            PRINT_ATTRIBUTE(cacheDisable);
            PRINT_ATTRIBUTE(writeThrough);
            #undef PRINT_ATTRIBUTE

            // For U/S and R/W a bit would be too confusing, use chars such as:
            //  U: Userspace and S: Supervisor
            //  W: Writable  and R: Read-only
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.userpage ? " U " : " S ");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.writable ? " W " : " R ");

            // The raw 64-bit value of the entry.
            ImGui::TableNextColumn();
            ImGui::Text("0x%016lx", entry.raw);

            // Recurse on opened nodes that are higher level than a page frame.
            if (opened) {
                if (L > 1 && entry.present) {
                    printTables(L - 1, entry.nextTableOffset(),
                                entryStartLinAddr);
                }
                ImGui::TreePop();
            }

            if (!entry.present) {
                ImGui::PopStyleColor();
            }
        }
    };

    u64 const pml4PhyOffset(state.registers().cr3 & ~((1 << 12) - 1));
    printTables(4, pml4PhyOffset, 0);

    ImGui::EndTable();
}

// x86 segment descriptor layout, this is used to easily parse each field from a
// raw 64-bit value representing a descriptor in the GDT.
struct SegmentDescriptor {
    uint16_t limitLow;
    uint16_t baseLow;
    uint8_t baseMid;
    uint8_t type : 4;
    uint8_t S : 1;
    uint8_t DPL : 2;
    uint8_t P : 1;
    uint8_t limitHigh : 4;
    uint8_t AVL : 1;
    uint8_t L : 1;
    uint8_t DB : 1;
    uint8_t G : 1;
    uint8_t baseHigh;
} __attribute__((packed));

void Imgui::CpuStateWindow::doDrawGdt(State const& state) {
    // Draw info about the GDTR.
    Vm::State::Registers::Table const gdt(state.registers().gdt);
    // The limit is always of the form 8*N - 1 because base + limit must point
    // to the last byte of the table. Each segment descriptor is 8 bytes in
    // length. Hence the number of segments is (limit + 1) / 8.
    // Note: Promote the limit to 64 bit so that we gracefully handle the case
    // where limit == 0xffff.
    u64 const segDescSize(8);
    u64 const numSegments(((u64)gdt.limit + 1) / segDescSize);

    ImGui::Text("GDT base linear address: 0x%016lx", gdt.base);
    ImGui::Text("GDT limit: 0x%04hx", gdt.limit);

    ImGuiTableFlags const tableFlags(ImGuiTableFlags_BordersOuter |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_BordersInnerV);

    // Draw a table containing all the segments descriptors in the GDT, with the
    // following columns:
    // Base | Limit | Type | S | DPL | P | AVL | L | D/B | G | Raw value
    if (!ImGui::BeginTable("#GDT", 12, tableFlags)) {
        return;
    }

    ImGui::TableSetupScrollFreeze(1, 0);

    ImGui::TableSetupColumn("");
    ImGui::TableSetupColumn("Base");
    ImGui::TableSetupColumn("Limit");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn(" S ");
    ImGui::TableSetupColumn("DPL");
    ImGui::TableSetupColumn(" P ");
    ImGui::TableSetupColumn("AVL");
    ImGui::TableSetupColumn(" L ");
    ImGui::TableSetupColumn("D/B");
    ImGui::TableSetupColumn(" G ");
    ImGui::TableSetupColumn("Raw value");
    ImGui::TableHeadersRow();

    std::shared_ptr<X86Lab::Snapshot const> const snap(state.snapshot());
    for (u64 i(0); i < numSegments; ++i) {
        // Read the next segment descriptor from the table.
        u64 const descLinAddr(gdt.base + i * segDescSize);
        std::vector<u8> const raw(snap->readLinearMemory(descLinAddr,
            segDescSize));
        SegmentDescriptor const desc(
            *reinterpret_cast<SegmentDescriptor const*>(raw.data()));

        u64 const base((desc.baseHigh << 24) |
                       (desc.baseMid << 16) |
                       desc.baseLow);
        u64 const limit((desc.limitHigh << 16) | desc.limitLow);

        if (!desc.P) {
            // Darken the non present descriptors.
            ImGui::PushStyleColor(ImGuiCol_Text, unmappedColor);
        }

        ImGui::TableNextColumn();
        ImGui::Text("%ld", i);
        ImGui::TableNextColumn();
        ImGui::Text("0x%016lx", base);
        ImGui::TableNextColumn();
        ImGui::Text("0x%016lx", limit);
        ImGui::TableNextColumn();
        ImGui::Text("0x%01x", desc.type);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(desc.S ? " 1 " : " 0 ");
        ImGui::TableNextColumn();
        ImGui::Text("%d", desc.DPL);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(desc.P ? " 1 " : " 0 ");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(desc.AVL ? " 1 " : " 0 ");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(desc.L ? " 1 " : " 0 ");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(desc.DB ? " 1 " : " 0 ");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(desc.G ? " 1 " : " 0 ");
        ImGui::TableNextColumn();
        ImGui::Text("0x%016lx", *reinterpret_cast<u64 const*>(raw.data()));


        if (!desc.P) {
            // Darken the non present descriptors.
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndTable();
}

// Type to use in doDrawIdtHelper in order to print a real-mode IDT.
struct IdtEntry16Bits {
    static constexpr u32 numCols = 3;
    static constexpr char const * colNames[numCols] = {
        "Segment selector",
        "Offset",
        "Linear address",
    };

    void draw() const {
        ImGui::TableNextColumn();
        ImGui::Text("0x%04hx", segmentSelector);
        ImGui::TableNextColumn();
        ImGui::Text("0x%04hx", offset);

        u32 const linearAddr((segmentSelector << 4) + offset);
        ImGui::TableNextColumn();
        ImGui::Text("0x%08x", linearAddr);
    }

    u16 offset;
    u16 segmentSelector;
} __attribute__((packed));
static_assert(sizeof(IdtEntry16Bits) == 4);

void Imgui::CpuStateWindow::doDrawIdt(State const& state) {
    Vm::State::Registers::Table const idt(state.registers().idt);
    ImGui::Text("IDT base linear address: 0x%016lx", idt.base);
    ImGui::Text("IDT limit: 0x%04hx", idt.limit);

    std::shared_ptr<X86Lab::Snapshot const> const snap(state.snapshot());
    Vm::CpuMode const cpuMode(snap->cpuMode());
    switch (cpuMode) {
        case Vm::CpuMode::RealMode:
            doDrawIdtHelper<IdtEntry16Bits>(state);
            break;
        default:
            ImGui::Text("Not supported in current CPU mode");
            break;
    }
}

template<typename EntryType>
void Imgui::CpuStateWindow::doDrawIdtHelper(State const& state) {
    ImGuiTableFlags const tableFlags(ImGuiTableFlags_BordersOuter |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_BordersInnerV);

    // +1 because of the first column printing the index of each entry.
    u32 const numCols(EntryType::numCols + 1);
    if (!ImGui::BeginTable("#IDT", numCols, tableFlags)) {
        return;
    }

    // Freeze the header row.
    ImGui::TableSetupScrollFreeze(1, 0);

    // Setup the column names + header row.
    ImGui::TableSetupColumn("");
    for (u32 i(0); i < EntryType::numCols; ++i) {
        ImGui::TableSetupColumn(EntryType::colNames[i]);
    }
    ImGui::TableHeadersRow();

    // Print each entry of the IDT.
    std::shared_ptr<X86Lab::Snapshot const> const snap(state.snapshot());
    u64 const idtLinAddr(state.registers().idt.base);
    u64 const entrySize(sizeof(EntryType));
    u64 const numEntries((state.registers().idt.limit + 1) / entrySize);
    for (u32 i(0); i < numEntries; ++i) {
        u64 const entryOff(idtLinAddr + i * entrySize);
        std::vector<u8> const raw(snap->readLinearMemory(entryOff, entrySize));
        EntryType const entry(*reinterpret_cast<EntryType const*>(raw.data()));

        ImGui::TableNextColumn();
        ImGui::Text("%d", i);

        entry.draw();
    }

    ImGui::EndTable();
}

Imgui::MemoryWindow::MemoryWindow() :
    Window(defaultTitle, windowFlags),
    m_focusedAddr(0) {
    static std::map<Granularity, std::string> const dropdownOpt({
        {Granularity::Byte,   "Bytes + ASCII"},
        {Granularity::Word,   "Words"},
        {Granularity::Dword,  "Doublewords"},
        {Granularity::Qword,  "Quadwords"},
        {Granularity::Float,  "Floats"},
        {Granularity::Double, "Doubles"},
    });
    m_granDropdown = std::make_unique<Dropdown<Granularity>>("Dump format:",
                                                             dropdownOpt);
    static std::map<DisplayFormat, std::string> const displayFormatOpt({
        {Imgui::DisplayFormat::Hexadecimal, "Hexadecimal"},
        {Imgui::DisplayFormat::SignedDecimal, "Signed decimal"},
        {Imgui::DisplayFormat::UnsignedDecimal, "Unsigned decimal"},
    });
    m_dispFormatDropdown = std::make_unique<Dropdown<DisplayFormat>>(
        "Value format:", displayFormatOpt);

    static std::map<AddressSpace, std::string> const addrSpaceOpt({
        {AddressSpace::Physical, "Physical"},
        {AddressSpace::Linear, "Linear"},
    });
    m_addressSpaceDropdown = std::make_unique<Dropdown<AddressSpace>>(
        "Address space:", addrSpaceOpt);
}

void Imgui::MemoryWindow::doDraw(State const& state) {
    Granularity const gran(m_granDropdown->selection());
    u64 const elemSize(granularityToBytes.at(gran));
    bool const showingAscii(gran == Granularity::Byte);
    // When displaying bytes we also need space to print the ASCII
    // representation. Follow what virtually every hexdump does and print 16
    // bytes per line in that case. Otherwise we keep 64-bytes per line as this
    // seems a comfortable length.
    u64 const bytesPerLine(showingAscii ? 16 : 64);
    u64 const numElems(bytesPerLine / elemSize);
    // Code below assume that bytesPerLine is divisible by elemSize.
    assert(bytesPerLine % elemSize == 0);

    ImGuiStyle const& style(ImGui::GetStyle());

    // First widget: InputText field to select the currently focused address.
    // Print legend, AlignTextToFramePadding makes sure the text will be
    // centered with the input text.
    ImGui::AlignTextToFramePadding();
    ImGui::Text(inputFieldText);
    ImGui::SameLine();

    // Where the input address will be stored, need enough space to input a
    // 64-bit address in hex format (without the "0x" prefix) + NUL char.
    char inputBuf[17] = {0};
    // Keep the input widget only as wide as it needs to be, e.g. just enough to
    // contain a 64-bit address in hex format.
    ImGui::SetNextItemWidth(sizeof(inputBuf) * ImGui::CalcTextSize("0").x +
                            style.FramePadding.x * 2.0f);
    // Input widget, returns true if contains some input.
    ImGuiInputTextFlags const inputFlags(ImGuiInputTextFlags_AutoSelectAll |
                                         ImGuiInputTextFlags_CharsHexadecimal);
    bool const focusedAddrChanged(
        ImGui::InputText("##in", inputBuf, sizeof(inputBuf), inputFlags));
    if (focusedAddrChanged) {
        if (!!::strlen(inputBuf)) {
            std::istringstream iss(inputBuf);

            // If the buffer is empty at this point the input is set to 0.
            u64 input;
            iss >> std::hex >> input;

            // Keep the memory dump aligned on bytesPerLine.
            m_focusedAddr = bytesPerLine * (input / bytesPerLine);
        } else {
            // Buffer might be empty after erasing the content of the input
            // field, in this case reset to focusing on 0.
            m_focusedAddr = 0;
        }
    }

    // Second widget: The table showing the memory dump.
    bool const isPrintingFloats(gran == Granularity::Float ||
                                gran == Granularity::Double);
    // First show the dropdowns.
    // Dropdown for address space selection.
    ImGui::SameLine();
    m_addressSpaceDropdown->draw();
    m_addressSpace = m_addressSpaceDropdown->selection();
    // The value format dropdown is only shown if the
    // granularity is set to something other than Double and Float, because in
    // those two cases the format is implicitely set to FloatingPoint.
    ImGui::SameLine();
    m_granDropdown->draw();
    if (!isPrintingFloats) {
        ImGui::SameLine();
        m_dispFormatDropdown->draw();
    }

    ImGuiTableFlags const tableFlags(ImGuiTableFlags_BordersOuter |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_SizingFixedFit);
    ImGuiTableColumnFlags const colFlags(ImGuiTableColumnFlags_WidthFixed);

    // This rowHeight computation is a bit voodoo, but comes from the imgui
    // demo so I guess we can trust it.
    float const rowHeight(ImGui::GetFontSize() + style.CellPadding.y * 2.0f);
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
    ImVec2 const tablePos(ImGui::GetCursorScreenPos());

    // One column per element + the address. In case we are displaying bytes we
    // also need a column for the ASCII repr.
    u32 const numCols(showingAscii ? numElems + 2 : numElems + 1);
    if (!ImGui::BeginTable("MemoryDump", numCols, tableFlags, outerSize)) {
        return;
    }

    // The headers of the colums and the address are always shown.
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Address", colFlags);
    for (u32 i(0); i < numElems; ++i) {
        std::ostringstream colName;
        colName << "+0x" << std::hex << (i * elemSize);
        ImGui::TableSetupColumn(colName.str().c_str(), colFlags);
    }
    if (showingAscii) {
        ImGui::TableSetupColumn("ASCII", colFlags);
    }
    ImGui::TableHeadersRow();

    if (focusedAddrChanged) {
        // Immediately honor the requested focus address and change the scroll
        // position so that that address is the first line in the dump. We need
        // to do that _before_ drawing using the clipper so the clipper is
        // computing the correct range (e.g. starting at focusedRowIdx).
        u64 const focusedRowIdx(m_focusedAddr / bytesPerLine);
        ImGui::SetScrollY(focusedRowIdx * rowHeight);
    } else {
        // Make sure the scroll position is at a multiple of rowHeight. Note
        // that the if block above guarantees this when focusing on a particular
        // address, hence not needed in that case.
        float const scrollY(ImGui::GetScrollY());
        ImGui::SetScrollY(std::floor(scrollY / rowHeight) * rowHeight);
    }

    DisplayFormat const dispFmt(isPrintingFloats ? DisplayFormat::FloatingPoint:
                                m_dispFormatDropdown->selection());
    // The format string to use in the call to ImGui::Text printing values.
    char const * const fmt(displayFormatAndBitsToFormatString.at(
        std::make_pair(dispFmt, elemSize * 8)));
    std::shared_ptr<X86Lab::Snapshot const> const s(state.snapshot());
    // The x position of the ascii column in screen space. This will be used
    // below to draw the separator between the memory content and the ASCII
    // repr.
    float asciiColPosX;
    // Print a row in the table.
    // @param rowIdx: The index of the row. The resulting row will be for
    // offset = rowIdx * bytesPerLine.
    auto const printRow([&](u32 const rowIdx) {
        u64 const offset(rowIdx * bytesPerLine);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        // Address.
        ImGui::PushStyleColor(ImGuiCol_Text, addrColor);
        ImGui::Text("0x%016lx", offset);
        ImGui::PopStyleColor();

        // Read a line of memory (64 bytes) in the current address space
        // m_addressSpace.
        // @param offset: The offset to read from.
        // @return: A vector<u8> containing the content of the memory at offset
        // `offset`.
        auto const readMemLine([&](u64 const offset) {
            switch (m_addressSpace) {
                case AddressSpace::Physical:
                    return s->readPhysicalMemory(offset, 64);
                case AddressSpace::Linear: {
                    return s->readLinearMemory(offset, 64);
                }
            }
        });

        std::vector<u8> const lineData(readMemLine(offset));
        if (!!lineData.size()) {
            assert(lineData.size() >= bytesPerLine);
            vec512 const line(lineData.data());
            for (u32 i(0); i < numElems; ++i) {
                ImGui::TableNextColumn();
                switch (gran) {
                    case Granularity::Byte:
                        ImGui::Text(fmt, line.template elem<u8>(i));
                        break;
                    case Granularity::Word:
                        ImGui::Text(fmt, line.template elem<u16>(i));
                        break;
                    case Granularity::Dword:
                        ImGui::Text(fmt, line.template elem<u32>(i));
                        break;
                    case Granularity::Qword:
                        ImGui::Text(fmt, line.template elem<u64>(i));
                        break;
                    case Granularity::Float:
                        ImGui::Text(fmt, line.template elem<float>(i));
                        break;
                    case Granularity::Double:
                        ImGui::Text(fmt, line.template elem<double>(i));
                        break;
                    default:
                        throw std::runtime_error("Invalid granularity");
                }
            }

            // ASCII repr.
            if (showingAscii) {
                ImGui::TableNextColumn();
                asciiColPosX = ImGui::GetCursorScreenPos().x;
                for (u32 i(0); i < bytesPerLine; ++i) {
                    // Replace non-printable char by a darkened "." char.
                    char const ch(line.elem<u8>(i));
                    if (std::isprint(ch)) {
                        ImGui::Text("%c ", ch);
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, nonPrintColor);
                        ImGui::Text(". ");
                        ImGui::PopStyleColor();
                    }
                    // Cancel-out the newline from the previous ImGui::Text(), next
                    // char is immediately following the previous one.
                    ImGui::SameLine(0, 0);
                }
            }
        } else {
            // We only end up in this situation if we are in Linear address mode
            // and the memory is not mapped. In that case we display X's in all
            // colums.
            for (u32 i(0); i < numCols - 1; ++i) {
                ImGui::TableNextColumn();
                ImGui::Text("X");
            }
        }
    });

    ImGuiListClipper clipper;
    clipper.Begin(dumpNumLines);
    while (clipper.Step()) {
        for (int i(clipper.DisplayStart); i < clipper.DisplayEnd; ++i) {
            printRow(i);
        }
    }

    ImGui::EndTable();

    // Draw separators between the addresses and the content.
    // Note: Dear ImGui does not support only drawing borders on _some_ columns
    // hence we are forced to use the DrawList API here.
    ImDrawList* const drawList(ImGui::GetWindowDrawList());
    float const paddingX(style.CellPadding.x);
    float const paddingY(style.CellPadding.y);
    float const addrColWidth(ImGui::CalcTextSize("0x0000000000000000").x +
        paddingX * 2);

    // The separator spans all displayed rows of the table, excepted the
    // first header row. Use some padding to make it pretty.
    float const sepLen(tableHeight - rowHeight - 2 * paddingY);

    // First separator.
    ImVec2 const sepStart(tablePos.x + addrColWidth,
                          tablePos.y + rowHeight + paddingY);
    ImVec2 const sepEnd(sepStart.x, sepStart.y + sepLen);
    drawList->AddLine(sepStart, sepEnd, ImGui::GetColorU32(separatorColor));

    // When showing ASCII dump, also add a separator between the memory content
    // and the ASCII column.
    if (showingAscii) {
        // The rowHeight is for the header row.
        ImVec2 const sepStart(asciiColPosX - paddingX,
                              tablePos.y + rowHeight + paddingY);
        ImVec2 const sepEnd(sepStart.x, sepStart.y + sepLen);
        drawList->AddLine(sepStart, sepEnd, ImGui::GetColorU32(separatorColor));
    }
}
}
