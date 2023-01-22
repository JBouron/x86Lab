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
    // Will be computed during drawStackWin() call. This is needed to compute
    // the register window's position and size.
    ImVec2 m_stackWinSize;
    // Register window size and position can only be computed at runtime.

    // FIXME: For now the log window is disabled and the memory window is taking
    // its place.
    static constexpr ImVec2 logsWinPos = ImVec2(0.0f,
                                                codeWinPos.y + codeWinSize.y);
    static constexpr ImVec2 logsWinSize = ImVec2(1.0f, 1.0f - codeWinSize.y);
    // Memory window position and size.
    static constexpr ImVec2 memWinPos = logsWinPos;
    static constexpr ImVec2 memWinSize = logsWinSize;

    // (Re-)draw the GUI.
    void draw();

    // Implementation of the abstract methods, as required by Ui::Backend.
    virtual bool doInit();
    virtual Action doWaitForNextAction();
    virtual void doUpdate(State const& newState);
    virtual void doLog(std::string const& msg);

    // The SDL window running the Dear ImGui application.
    SDL_Window *m_sdlWindow;
    // Backend renderer for SDL/Dear ImGui.
    SDL_Renderer *m_sdlRenderer;

    // Last known, state of the VM, this is what is currently being displayed in
    // the GUI.
    // FIXME: We shouldn't have to copy this everytime, also this is rather poor
    // code mixing the states of the VM and the GUI.
    State m_state;

    // The logs to display in the Log window.
    std::vector<std::string> m_logs;

    // A child window of the ImGui GUI. Note that while this class fully
    // describes how a window is drawn, it does not control its size nor its
    // position, this is set by the Imgui class when drawing windows.
    class Window {
    public:
        // Create a window.
        // @param title: The title of the window.
        // @param flags: The flags to be used by the window.
        Window(std::string const& title, ImGuiWindowFlags const flags);

        virtual ~Window() = default;

        // Draw the window with the given position and size. This method takes
        // care of calling ImGui::Begin and ImGui::End. This calls doDraw()
        // between the Begin and End.
        // @param position: Where to place the window.
        // @param size: The size of the window to be drawn.
        // @param state: The state of the VM to be drawn.
        // @return: The actual size of the window, this is different than `size`
        // if size.x == 0 or size.y == 0 since in that case the window's size
        // depends on its content.
        ImVec2 draw(ImVec2 const& position,
                    ImVec2 const& size,
                    State const& state);

    protected:
        // Title of the window.
        std::string m_title;
        // Flags of the window.
        ImGuiWindowFlags m_flags;

        // Where the actual drawing of the window happens, this is expected to
        // be overriden by the sub-class.
        virtual void doDraw(State const& state) = 0;
    };

    // Drop down widget. Allow to easily create a dropdown item containing
    // options of a given type T.
    template<typename T>
    class Dropdown {
    public:
        // Create a dropdown widget with the given options.
        // @param options: The options to display in the dropdown. Options are
        // specified in a map where each key is an option, the value associated
        // with a key/option is the string representation of that option, to be
        // used when drawing the dropdown / options.
        // Options appear in the dropdown in ascending order (e.g. ascending
        // order of the keys of the map).
        // The default selection is the first option.
        Dropdown(std::string const& label,
                 std::map<T, std::string> const& options);

        // Draw the widget.
        void draw();

        // Force the selection to a particular option.
        // @param option: The option to force the selection to.
        void setSelection(T const& option);

        // Get the value associated with the currently selected item in the
        // dropdown.
        T const& selection() const;

    private:
        // The flags to use for the combo object. Use default.
        static constexpr ImGuiComboFlags comboFlags = 0;
        // The label, printed on the right of the dropdown.
        std::string m_label;
        // The available options.
        std::map<T, std::string> m_options;
        // The currently selected option.
        T m_selection;
    };

    // Show the code being run in the VM. Simple layout printing each line of
    // the source file and highlighting the current instruction.
    class CodeWindow : public Window {
    public:
        // Default title for a CodeWindow.
        static constexpr char const * defaultTitle = "Code";

        // Create a code window, with default title and flags.
        CodeWindow();

    private:
        // Background color of the current line / instruction.
        static constexpr ImVec4 currLineBgColor = ImVec4(0.18, 0.18, 0.2, 1);

        // The RIP during the last call to draw. Use to detect when the current
        // instruction changed and when to focus/scroll on it.
        u64 m_previousRip;

        // Override.
        virtual void doDraw(State const& state);
    };

    // Stack window: The stack windows shows the current state of the stack in a
    // table. Each line contains the absolute address and relative (to rsp)
    // address of an element of the stack as well as its value.
    // Stack frames are delimited by separators in the table.
    class StackWindow : public Window {
    public:
        StackWindow();
    private:
        static constexpr char const * defaultTitle = "Stack";

        static constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_AlwaysAutoResize |
            Imgui::defaultWindowFlags;

        // The stack window prints separators to identify stack frames.
        // The color of the stack frame separators.
        static constexpr ImVec4 frameSeparatorColor =
            ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        // The thickness of the stack frame separators.
        static constexpr float frameSeparatorThickness = 1.0f;

        // The maximum number of stack entries to be shown in the stack window.
        // The stack windows uses a list clipper so increasing this value does
        // not negatively impact performance.
        static constexpr u64 maxHistory = 1000;

        // Color of the addresses in the table.
        static constexpr ImVec4 addrColor = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

        // Keep track of the start offset of the last maxHistory stack frames.
        // Re-computed everytime the state of the VM changes. This set is meant
        // to avoid recomputing the frame offsets everytime the screen is
        // refresh while the VM state stays un-changed.
        std::set<u64> m_stackFrameStartOffsets;
        // Re-compute the m_stackFrameStartOffsets. Called everytime the
        // VM's state changes.
        void updateStackFrameStartOffsets(State const& state);
        // The value of RBP and RSP in the previous call to doDraw(). This is
        // used to detect when the set of stack frames is changing which
        // usually coincides with RBP or RSP changing.
        u64 m_previousRbp;
        u64 m_previousRsp;

        // Override.
        virtual void doDraw(State const& state);
    };

    // Different possible format for displaying values.
    enum class DisplayFormat {
        Hexadecimal,
        SignedDecimal,
        UnsignedDecimal,
        FloatingPoint,
    };

    // Maps all values of the DisplayFormat enum to a string representation. Can
    // be used for dropdown construction.
    static const std::map<DisplayFormat, std::string> formatToString;

    // Maps a DisplayFormat and number of bits to the format string needed to
    // print this value. For instance this maps (Hexadecimal, 64) to "0x%016lx".
    static const std::map<std::pair<DisplayFormat, u8>, char const*>
        displayFormatAndBitsToFormatString;

    // Show the current state of the VM's registers. Organized in multiple tabs
    // to separate general-purpose registers and vector registers.
    class RegisterWindow : public Window {
    public:
        RegisterWindow();

        // Toggle the next display granularity for the vector registers.
        void nextGranularity();
    private:
        static constexpr char const * defaultTitle = "Registers";

        // The color of "old" register values in the history list.
        static constexpr ImVec4 oldValColor = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

        // Vector registers can usually be interpreted in different ways: Packed
        // bytes, words, dwords, ... or packed single / double precision fp.  To
        // save screen-space only a single form / granularity is printed in the
        // register window at a time. The user can then toggle the granularity
        // as they please.
        enum class Granularity {
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
        // The granularity in which the vector registers are currently
        // displayed.
        Granularity m_currentGranularity;

        // How many bytes is a single element in the given granularity. Used to
        // compute how many elements a vector register contains in a given
        // granularity.
        static std::map<Granularity, u32> const granularityToBytes;

        // Draw the columns for each element of a vector register in the given
        // granularity. This function assumes that we are currently drawing a
        // table (e.g. between BeginTable and EndTable calls) and calls
        // TableNextColumn before printing out each element of the vector
        // register.
        template<size_t W>
        void drawColsForVec(vec<W> const& vec, Granularity const granularity);

        // Override.
        virtual void doDraw(State const& state);

        // Helper function to draw each tab of the window. Called by doDraw.
        // Draw the tab showing general purpose registers.
        void doDrawGeneralPurpose(State const& state);
        // Draw the tab showing the FPU and MMX registers.
        void doDrawFpuMmx(State const& state);
        // Draw the tab showing SSE & AVX registers.
        void doDrawSseAvx(State const& state);

        // The dropdown used to select the display format of general purpose
        // registers.
        std::unique_ptr<Dropdown<DisplayFormat>> m_gpFormatDropdown;
    };

    // Display the content of the VM's physical memory.
    class MemoryWindow : public Window {
    public:
        MemoryWindow();
    private:
        static constexpr char const * defaultTitle = "Physical Memory";

        static constexpr ImGuiWindowFlags windowFlags = 
            Imgui::defaultWindowFlags |
            ImGuiWindowFlags_HorizontalScrollbar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        // The focused address in the memory dump in the memory window. The
        // table scrolls to this address. Can be changed using the input field.
        u64 m_focusedAddr;
        // Legend for the focus address input field.
        static constexpr char const * inputFieldText = "Jump to 0x";
        // The number of lines to show in the memory dump. FIXME: This really
        // shouldn't be hardcoded. Since we are using a clipper we can get away
        // with showing the entire physical memory.
        static constexpr u64 dumpNumLines = 1000;

        // Color of the addresses in the table.
        static constexpr ImVec4 addrColor = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        // Color of the non-printable chars in the ASCII format.
        static constexpr ImVec4 nonPrintColor = addrColor;
        // Color of the separator between the memory content and the ASCII
        // format.
        static constexpr ImVec4 separatorColor = addrColor;

        // Override.
        virtual void doDraw(State const& state);
    };

    // The set of windows making up the interface of x86Lab.
    std::unique_ptr<CodeWindow> m_codeWindow;
    std::unique_ptr<StackWindow> m_stackWindow;
    std::unique_ptr<RegisterWindow> m_registerWindow;
    std::unique_ptr<MemoryWindow> m_memoryWindow;
};
}
