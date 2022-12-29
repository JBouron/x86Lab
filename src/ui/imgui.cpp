#include <x86lab/ui/imgui.hpp>
#include <SDL.h>
#include <sstream>
#include <iomanip>

namespace X86Lab::Ui {
Imgui::Imgui() {
    // FIXME: We should have a more graceful way to deal with errors. Any error
    // occuring here cannot be detected by the Runner instance, which will try
    // to update and interact with the GUI regardless.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return;
    }

    SDL_WindowFlags const flags((SDL_WindowFlags)(SDL_WINDOW_RESIZABLE |
                                                  SDL_WINDOW_ALLOW_HIGHDPI));
    // FIXME: Fix hard-coding of window title and size.
    m_sdlWin = SDL_CreateWindow("x86Lab",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                1280,
                                720,
                                flags);

    // Setup SDL_Renderer instance
    m_sdlRenderer = SDL_CreateRenderer(m_sdlWin,
                                       -1,
                                       SDL_RENDERER_PRESENTVSYNC |
                                       SDL_RENDERER_ACCELERATED);
    if (m_sdlRenderer == NULL) {
        SDL_Log("Error creating SDL_Renderer!");
        return;
    }

    ImGui::CreateContext();
    // Disable auto-saving of GUI preferences.
    ImGui::GetIO().IniFilename = NULL;
    ImGui_ImplSDL2_InitForSDLRenderer(m_sdlWin, m_sdlRenderer);
    ImGui_ImplSDLRenderer_Init(m_sdlRenderer);

    m_quit = false;
    m_state = State();
    m_currentGranularity = VectorRegisterGranularity::Qword;
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
            if (event.type == SDL_QUIT) {
                m_quit = true;
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_CLOSE &&
                       event.window.windowID == SDL_GetWindowID(m_sdlWin)) {
                m_quit = true;
            }
        }

        draw();

        if (ImGui::IsKeyPressed(ImGuiKey_S, true)) {
            return Action::Step;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R, true)) {
            return Action::ReverseStep;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            cycleGranularity();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            return Action::Quit;
        }
    }
    return Action::Quit;
}

template<size_t W, typename T>
static std::string vecToStr(vec<W> const& vec, std::string const& separator) {
    u32 const elemWidth(sizeof(T) * 8);
    assert(!(W % elemWidth));
    u32 const numElems(W / elemWidth);

    std::ostringstream oss;
    if constexpr (!std::is_same_v<T, float> && !std::is_same_v<T, double>) {
        oss << std::hex;
    }
    for (int i(numElems - 1); i >=0; --i) {
        if constexpr (!std::is_same_v<T, float> && !std::is_same_v<T, double>) {
            oss << std::setfill('0');
            oss << std::setw(elemWidth / 4);
            // Need the cast to u64 in the case of packed bytes representation
            // to avoid printing characters.
            oss << (u64)vec.template elem<T>(i);
        } else {
            // In float or double, use the max precision possible.
            static_assert(std::is_same_v<T, float>||std::is_same_v<T, double>);
            int const digits(std::numeric_limits<T>::digits10);
            oss.precision(digits);
            oss << vec.template elem<T>(i);
        }
        if (!!i) {
            oss << separator;
        }
    }
    return oss.str();
}

template<size_t W>
std::string vecRegToString(
    vec<W> const& vec,
    Imgui::VectorRegisterGranularity const granularity) {
    switch (granularity) {
        case Imgui::VectorRegisterGranularity::Byte:
            return vecToStr<W, u8>(vec, " ");
        case Imgui::VectorRegisterGranularity::Word:
            return vecToStr<W, u16>(vec, " ");
        case Imgui::VectorRegisterGranularity::Dword:
            return vecToStr<W, u32>(vec, " ");
        case Imgui::VectorRegisterGranularity::Qword:
            return vecToStr<W, u64>(vec, " ");
        case Imgui::VectorRegisterGranularity::Float:
            return vecToStr<W, float>(vec, " ");
        case Imgui::VectorRegisterGranularity::Double:
            return vecToStr<W, double>(vec, " ");
        case Imgui::VectorRegisterGranularity::Full:
            // Re-use packed bytes representation with no space in between
            // elements.
            return vecToStr<W, u8>(vec, "");
        default:
            throw Error("Invalid VectorRegisterGranularity", 0);
    }
}

void Imgui::cycleGranularity() {
    int const curr(static_cast<int>(m_currentGranularity));
    int const max(static_cast<int>(
        VectorRegisterGranularity::NumVectorRegisterGranularity));
    m_currentGranularity =
        static_cast<VectorRegisterGranularity>((curr + 1) % max);
}

void Imgui::draw() {
    ImGui_ImplSDLRenderer_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGuiWindowFlags const flags(ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoSavedSettings);
    ImGuiViewport const* viewport(ImGui::GetMainViewport());

    float const registerWinWidth(0.70);
    float const logWinHeight(0.15);

    float const codeWinWidth(1.0 - registerWinWidth);
    ImVec2 const codeWinPos(0.0, 0.0);
    ImVec2 const codeWinSize(codeWinWidth * viewport->WorkSize.x,
                             (1.0 - logWinHeight) * viewport->WorkSize.y);

    ImVec2 const registerWinPos(codeWinPos.x + codeWinSize.x, 0.0);
    ImVec2 const registerWinSize(registerWinWidth * viewport->WorkSize.x,
                                 (1.0 - logWinHeight) *viewport->WorkSize.y);

    ImVec2 const logWinPos(0.0, codeWinPos.y + codeWinSize.y);
    ImVec2 const logWinSize(1.0 * viewport->WorkSize.x,
                            logWinHeight * viewport->WorkSize.y);

    // Draw code window.
    ImGui::SetNextWindowPos(codeWinPos);
    ImGui::SetNextWindowSize(codeWinSize);

    ImGui::Begin("Code", NULL, flags | ImGuiWindowFlags_NoScrollbar);

    std::string fileName(m_state.sourceFileName());
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
                ImGui::SetScrollHereY(0.5);
            }
        }
    }

    ImGui::End();

    // Draw regs window.
    ImGui::SetNextWindowPos(registerWinPos);
    ImGui::SetNextWindowSize(registerWinSize);

    ImVec4 const colorOld(0.35f, 0.35f, 0.35f, 1.0f);

    ImGui::Begin("Registers", NULL, flags);
    ImGui::BeginTabBar("##tabs", 0);

    // General purpose registers.
    if (ImGui::BeginTabItem("General Purpose", NULL, 0)) {
        ImGui::Text("  -- General Purpose --");
        ImGui::Text("rax = 0x%016lx    rbx = 0x%016lx    rcx = 0x%016lx    rdx = 0x%016lx",
                    m_state.registers().rax,
                    m_state.registers().rbx,
                    m_state.registers().rcx,
                    m_state.registers().rdx);
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("      0x%016lx          0x%016lx          0x%016lx          0x%016lx",
                    m_state.prevRegisters().rax,
                    m_state.prevRegisters().rbx,
                    m_state.prevRegisters().rcx,
                    m_state.prevRegisters().rdx);
        ImGui::PopStyleColor();
        ImGui::Text("");
        ImGui::Text("rsi = 0x%016lx    rdi = 0x%016lx    rsp = 0x%016lx    rbp = 0x%016lx",
                    m_state.registers().rsi,
                    m_state.registers().rdi,
                    m_state.registers().rsp,
                    m_state.registers().rbp);
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("      0x%016lx          0x%016lx          0x%016lx          0x%016lx",
                    m_state.prevRegisters().rsi,
                    m_state.prevRegisters().rdi,
                    m_state.prevRegisters().rsp,
                    m_state.prevRegisters().rbp);
        ImGui::PopStyleColor();
        ImGui::Text("");
        ImGui::Text("r8  = 0x%016lx    r9  = 0x%016lx    r10 = 0x%016lx    r11 = 0x%016lx",
                    m_state.registers().r8,
                    m_state.registers().r9,
                    m_state.registers().r10,
                    m_state.registers().r11);
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("      0x%016lx          0x%016lx          0x%016lx          0x%016lx",
                    m_state.prevRegisters().r8,
                    m_state.prevRegisters().r9,
                    m_state.prevRegisters().r10,
                    m_state.prevRegisters().r11);
        ImGui::PopStyleColor();
        ImGui::Text("");
        ImGui::Text("r12 = 0x%016lx    r13 = 0x%016lx    r14 = 0x%016lx    r15 = 0x%016lx",
                    m_state.registers().r12,
                    m_state.registers().r13,
                    m_state.registers().r14,
                    m_state.registers().r15);
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("      0x%016lx          0x%016lx          0x%016lx          0x%016lx",
                    m_state.prevRegisters().r12,
                    m_state.prevRegisters().r13,
                    m_state.prevRegisters().r14,
                    m_state.prevRegisters().r15);
        ImGui::PopStyleColor();
        ImGui::Text("");
        ImGui::Text("rip = 0x%016lx    rfl = 0x%016lx",
                    m_state.registers().rip,
                    m_state.registers().rflags);
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("      0x%016lx          0x%016lx",
                    m_state.prevRegisters().rip,
                    m_state.prevRegisters().rflags);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Text("  -- Segments --");
        ImGui::Text("cs = 0x%04x  ds = 0x%04x  es = 0x%04x  fs = 0x%04x  gs = 0x%04x  ss = 0x%04x",
                    m_state.registers().cs,
                    m_state.registers().ds,
                    m_state.registers().es,
                    m_state.registers().fs,
                    m_state.registers().gs,
                    m_state.registers().ss);
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("     0x%04x       0x%04x       0x%04x       0x%04x       0x%04x       0x%04x",
                    m_state.prevRegisters().cs,
                    m_state.prevRegisters().ds,
                    m_state.prevRegisters().es,
                    m_state.prevRegisters().fs,
                    m_state.prevRegisters().gs,
                    m_state.prevRegisters().ss);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Text("  -- Tables --");
        ImGui::Text("idx: base = 0x%016lx  limit = 0x%08x    gdt: base = 0x%016lx  limit = 0x%08x",
                    m_state.registers().idt.base,
                    m_state.registers().idt.limit,
                    m_state.registers().gdt.base,
                    m_state.registers().gdt.limit);
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("            0x%016lx          0x%08x                0x%016lx          0x%08x",
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
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
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
        ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
        ImGui::Text("      0x%016lx          0x%016lx          0x%016lx",
                    m_state.prevRegisters().cr4,
                    m_state.prevRegisters().cr8,
                    m_state.prevRegisters().efer);
        ImGui::PopStyleColor();

        // End general purpose registers.
        ImGui::EndTabItem();
    }

    // FPU / MMX.
    if (ImGui::BeginTabItem("FPU & MMX", NULL, 0)) {
        VectorRegisterGranularity granularity;
        if (m_currentGranularity == VectorRegisterGranularity::Float ||
            m_currentGranularity == VectorRegisterGranularity::Double ||
            m_currentGranularity == VectorRegisterGranularity::Full) {
            // MMX registers only hold packed integers. Full is equivalent to Qword
            // hence use Qword granularity in those cases.
            granularity = VectorRegisterGranularity::Qword;
        } else {
            granularity = m_currentGranularity;
        }

        for (u8 i(0); i < X86Lab::Vm::State::Registers::NumMmxRegs; ++i) {
            std::string line("mmx" + std::to_string(i) + " = ");
            line += vecRegToString(m_state.registers().mmx[i], granularity);
            ImGui::Text("%s", line.c_str());
            line = "       " + vecRegToString(m_state.prevRegisters().mmx[i], granularity);
            ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
            ImGui::Text("%s", line.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTabItem();
    }

    // SSE / AVX.
    if (ImGui::BeginTabItem("SSE & AVX", NULL, 0)) {
        for (u8 i(0); i < X86Lab::Vm::State::Registers::NumYmmRegs; ++i) {
            std::string line("ymm" + std::to_string(i) + (i < 10 ? " " : "") + " = ");
            line += vecRegToString(m_state.registers().ymm[i], m_currentGranularity);
            ImGui::Text("%s", line.c_str());
            line = "        " + vecRegToString(m_state.prevRegisters().ymm[i], m_currentGranularity);
            ImGui::PushStyleColor(ImGuiCol_Text, colorOld);
            ImGui::Text("%s", line.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();

    // Draw logs window.
    ImGui::SetNextWindowPos(logWinPos);
    ImGui::SetNextWindowSize(logWinSize);

    ImGui::Begin("Logs", NULL, flags | ImGuiWindowFlags_NoScrollbar);
    for (std::string& log : m_logs) {
        ImGui::Text("%s", log.c_str());
    }
    // Keep the scroll at the bottom of the box.
    ImGui::SetScrollHereY(1.0f);
    ImGui::End();

    ImGui::End();

    ImGui::Render();

    ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(m_sdlRenderer);
}

void Imgui::doUpdate(State const& newState) {
    m_state = newState;
}

void Imgui::doLog(std::string const& msg) {
    m_logs.push_back(msg);
}
}
