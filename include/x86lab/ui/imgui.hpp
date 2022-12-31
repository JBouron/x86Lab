#include <x86lab/ui/ui.hpp>
#include <imgui/imgui.h>
#include "imgui_impl_sdl.h"
#include "imgui_impl_sdlrenderer.h"

namespace X86Lab::Ui {

// Ui::Backend implementation using Dear ImGui running with SDL renderer.
class Imgui : public Backend {
public:
    // Initialize SDL and Dear ImGui.
    Imgui();

private:
    // The title of the SDL window showing the application.
    static constexpr char const * sdlWindowTitle = "x86Lab";
    // Starting width and height of the SDL window.
    static constexpr u32 sdlWinStartSizeWidth = 1280;
    static constexpr u32 sdlWinStartSizeHeight = 720;

    // GUI Layout and drawing.

    // Set of window flags shared by code, register and logs windows. Some
    // windows are using more flags on top of those, see their associated
    // drawXWin() function.
    static constexpr ImGuiWindowFlags defaultWindowFlags =
        // Keep the layout static, no resizing of moving windows allowed.
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoNav |
        // This would allow having "floating" windows in the future and make
        // sure that those are always on the foreground.
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings;

    // Position and size of all windows. Width and height of the windows are
    // expressed as a percentage of the root window's width and height. The
    // layout is as follows (not to scale):
    // +---------+---------+
    // |         |         |
    // |         |         |
    // |  CODE   |  REGS   |
    // |         |         |
    // |         |         |
    // +---------+---------+
    // |       LOGS        |
    // +---------+---------+
    static constexpr ImVec2 codeWinPos = ImVec2(0.0f, 0.0f);
    static constexpr ImVec2 codeWinSize = ImVec2(0.3f, 0.85f);
    static constexpr ImVec2 regsWinPos = ImVec2(codeWinPos.x + codeWinSize.x,
                                                0.0f);
    static constexpr ImVec2 regsWinSize = ImVec2(1.0f - codeWinSize.x,
                                                 codeWinSize.y);
    static constexpr ImVec2 logsWinPos = ImVec2(0.0f,
                                                codeWinPos.y + codeWinSize.y);
    static constexpr ImVec2 logsWinSize = ImVec2(1.0f, 1.0f - codeWinSize.y);

    // Colors.
    // The color of "old" register values in the history list.
    static constexpr ImVec4 regsOldValColor = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

    // (Re-)draw the GUI.
    void draw();

    // Helper functions for drawing code, register and logs windows. To be
    // called by draw() only.
    void drawCodeWin(ImGuiViewport const& viewport);
    void drawRegsWin(ImGuiViewport const& viewport);
    void drawLogsWin(ImGuiViewport const& viewport);

    // Implementation of the abstract methods, as required by Ui::Backend.
    virtual bool doInit();
    virtual Action doWaitForNextAction();
    virtual void doUpdate(State const& newState);
    virtual void doLog(std::string const& msg);

    // The SDL window running the Dear ImGui application.
    SDL_Window *m_sdlWindow;
    // Backend renderer for SDL/Dear ImGui.
    SDL_Renderer *m_sdlRenderer;

    // Vector registers can usually be interpreted in different ways: Packed
    // bytes, words, dwords, ... or packed single / double precision fp.
    // To save screen-space only a single form / granularity is printed in the
    // register window at a time. The user can then toggle the granularity as
    // they please.
    enum class VectorRegisterGranularity {
        // Packed integers.
        Byte,
        Word,
        Dword,
        Qword,
        // Packed floating point values.
        Float,
        Double,
        // Value of the entire vector register interpreted as one big unsigned
        // integer.
        Full,

        // Must be last.
        __MAX,
    };
    // The granularity in which the vector registers are currently displayed.
    VectorRegisterGranularity m_currentGranularity;

    // Convert a vector register value to its string representation in a given
    // granularity.
    // @param vec: The vector register value to convert.
    // @param gran: The granularity to use in the string representation.
    template<size_t W>
    static std::string vecRegToString(vec<W> const& vec,
                                      VectorRegisterGranularity const gran);

    // Last known, state of the VM, this is what is currently being displayed in
    // the GUI.
    // FIXME: We shouldn't have to copy this everytime, also this is rather poor
    // code mixing the states of the VM and the GUI.
    State m_state;

    // The logs to display in the Log window.
    std::vector<std::string> m_logs;
};
}
