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
        NumVectorRegisterGranularity,
    };

private:
    // The SDL window running the Dear ImGui application.
    SDL_Window *m_sdlWin;
    // Backend renderer for SDL/Dear ImGui.
    SDL_Renderer *m_sdlRenderer;
    // Did the user request quitting the application?
    bool m_quit;
    // The granularity in which the vector registers are currently displayed.
    VectorRegisterGranularity m_currentGranularity;
    // Last known, state of the VM, this is what is currently being displayed in
    // the GUI.
    // FIXME: We shouldn't have to copy this everytime, also this is rather poor
    // code mixing the states of the VM and the GUI.
    State m_state;

    // Helper function to change m_currentGranularity to the next granularity,
    // wrapping to `Byte` when m_currentGranularity == `Full`.
    void cycleGranularity();

    // The logs to display in the Log window.
    std::vector<std::string> m_logs;

    // (Re-)draw the GUI.
    void draw();

    // Implementation of the abstract methods, as required by Ui::Backend.
    virtual Action doWaitForNextAction();
    virtual void doUpdate(State const& newState);
    virtual void doLog(std::string const& msg);
};
}
