#include <x86lab/ui/ui.hpp>
#include <imgui/imgui.h>
#include "imgui_impl_sdl.h"
#include "imgui_impl_sdlrenderer.h"

#include <set>

namespace X86Lab::Ui {

// Ui::Backend implementation using Dear ImGui running with SDL renderer.
class Imgui : public Backend {
public:
    // Initialize SDL and Dear ImGui.
    Imgui();

private:
    // The title of the SDL window showing the application.
    static constexpr char const * sdlWindowTitle = "x86Lab";
    // Starting width and height of the SDL window. Note that the SDL window
    // starts maximized, therefore those size really indicates what the default
    // _un-maximized_ window size is.
    static constexpr u32 sdlWinDefaultSizeWidth = 1280;
    static constexpr u32 sdlWinDefaultSizeHeight = 720;

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
    // +---------+---------+---------+
    // |         |         |         |
    // |         |         |         |
    // |  CODE   |  STACK  |   REGS  |
    // |         |         |         |
    // |         |         |         |
    // +---------+---------+---------+
    // |           MEMORY            |
    // +-----------------------------+
    // Code window position and size are static.
    static constexpr ImVec2 codeWinPos = ImVec2(0.0f, 0.0f);
    static constexpr ImVec2 codeWinSize = ImVec2(0.25f, 0.70f);
    // Stack window position is known at compilation time (since code window
    // size and pos are known) but its size is not known: it will be computed to
    // fit it's content.
    static constexpr ImVec2 stackWinPos = ImVec2(codeWinPos.x + codeWinSize.x,
                                                 0.0f);
    // Will be computed during drawStackWin() call.
    ImVec2 m_stackWinSize;
    // Register window size and position can only be computed at runtime.

    static constexpr ImVec2 logsWinPos = ImVec2(0.0f,
                                                codeWinPos.y + codeWinSize.y);
    static constexpr ImVec2 logsWinSize = ImVec2(1.0f, 1.0f - codeWinSize.y);

    // FIXME: For now the log window is disabled and the memory window is taking
    // its place.
    static constexpr ImVec2 memWinPos = logsWinPos;
    static constexpr ImVec2 memWinSize = logsWinSize;

    // Colors.
    // The color of "old" register values in the history list.
    static constexpr ImVec4 regsOldValColor = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

    // (Re-)draw the GUI.
    void draw();

    // Background color of the current line / instruction in the code window.
    static constexpr ImVec4 codeWinCurrLineBgColor = ImVec4(0.18, 0.18, 0.2, 1);
    void drawCodeWin(ImGuiViewport const& viewport);
    void drawRegsWin(ImGuiViewport const& viewport);

    // Stack window: The stack windows shows the current state of the stack in a
    // table. Each line contains the absolute address and relative (to rsp)
    // address of an element of the stack as well as its value.
    // Stack frames are delimited by separators in the table.
    //
    // The color of the stack frame separators.
    static constexpr ImVec4 stackWinFrameSeparatorColor = regsOldValColor;
    // The thickness of the stack frame separators.
    static constexpr float stackWinFrameSeparatorThickness = 1.0f;
    // The maximum number of stack entries to be shown in the stack window. The
    // stack windows uses a list clipper so increasing this value does not
    // negatively impact performance.
    static constexpr u64 stackWinMaxHistory = 1000;
    // Keep track of the start offset of the last stackWinMaxHistory stack
    // frames. Re-computed everytime the state of the VM changes. This set is
    // meant to avoid recomputing the frame offsets everytime the screen is
    // refresh while the VM state stays un-changed.
    std::set<u64> m_stackWinStackFrameStartOffsets;
    // Re-compute the m_stackWinStackFrameStartOffsets. Called everytime the
    // VM's state changes.
    void updateStackWinStackFrameStartOffsets();
    // Draw the stack window, to be called by draw() only.
    void drawStackWin(ImGuiViewport const& viewport);

    void drawLogsWin(ImGuiViewport const& viewport);

    // The focused address in the memory dump in the memory window. The table
    // scrolls to this address. Can be changed using the input field.
    u64 m_memWinFocusedAddr;
    // Legend for the focus address input field.
    static constexpr char const * memWinInputFieldText = "Jump to (hex):";
    // The number of lines to show in the memory dump. FIXME: This really
    // shouldn't be hardcoded. Since we are using a clipper we can get away with
    // showing the entire physical memory.
    static constexpr u64 memWinDumpLines = 1000;
    void drawMemWin(ImGuiViewport const& viewport);

    // Set to true during the first call to draw() (and its helper functions)
    // for a given state of the VM. Then false until the state changes again.
    // This can be used by draw() and its helpers in order to differentiate
    // between a draw on a new state and a draw on the same state as before
    // (when drawing multiple frames while waiting for input for instance).
    bool m_isDrawingNewState;

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

        // Must be last.
        __MAX,
    };
    // The granularity in which the vector registers are currently displayed.
    VectorRegisterGranularity m_currentGranularity;

    // How many bytes is a single element in the given granularity. Used to
    // compute how many elements a vector register contains in a given
    // granularity.
    static std::map<VectorRegisterGranularity, u32> const granularityToBytes;

    // Draw the columns for each element of a vector register in the given
    // granularity. This function assumes that we are currently drawing a table
    // (e.g. between BeginTable and EndTable calls) and calls TableNextColumn
    // before printing out each element of the vector register.
    template<size_t W>
    void drawColsForVec(vec<W> const& vec,
                        VectorRegisterGranularity const granularity);

    // Last known, state of the VM, this is what is currently being displayed in
    // the GUI.
    // FIXME: We shouldn't have to copy this everytime, also this is rather poor
    // code mixing the states of the VM and the GUI.
    State m_state;

    // The logs to display in the Log window.
    std::vector<std::string> m_logs;
};
}
