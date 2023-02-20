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
        // Compute the width to be used by the child frame when drawing the
        // combo widget.
        float childFrameWidth();
        // Used to cache the result of childFrameWidth(). The width of the child
        // frame only needs to be computed once since the label and options
        // never change overtime. The reason we cannot compute this value at
        // construction time is because Imgui might not be fully initialized.
        float m_childFrameWidth;
    };

    // The config "window" is a small bar to the top of the main window that
    // directly manipulates the VM's configuration. This includes the cpu mode
    // the VM starts, resetting the VM, ...
    class ConfigBar : public Window {
    public:
        ConfigBar();

        // Get the last action requested by the used by clicking on the buttons.
        // @return: The last requested action, None if the user did not
        // clic/request any button/action.
        Action clickedAction() const;
    private:
        // Don't draw the title on the config bar as this is not a window.
        static constexpr ImGuiWindowFlags defaultFlags =
            Imgui::defaultWindowFlags | ImGuiWindowFlags_NoTitleBar;

        // The last action requested from the user by manually clicking on the
        // buttons. None if no action was requested.
        Action m_lastAction;

        // The currently selected value for the starting CPU mode. Changing this
        // value is done through the three radio buttons in the config bar.
        // Changing the starting cpu mode resets the VM.
        // By default the VM starts in 64-bit long mode.
        // FIXME: This default value must be the same as the default starting
        // cpu mode in main.cpp. Currently there is nothing enforcing this!
        Vm::CpuMode m_startCpuMode;

        // Override.
        virtual void doDraw(State const& state);
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

        // The disassembled code, indexed by the address of each instruction.
        // Each pair contains the bytes and mnemonic of the instruction.
        std::map<u64, std::pair<std::string, std::string>> m_disassembledCode;

        // Clears m_disassembledCode and re-disassemble the code from scratch,
        // starting at RIP.
        void disassembleCode(State const& state);

        // The mode the cpu was in during the last instruction/state.
        Vm::CpuMode m_previousCpuMode;

        // The RIP during the last call to draw. Use to detect when the current
        // instruction changed and when to focus/scroll on it.
        u64 m_previousRip;

        // The format available for the code window.
        enum class Format {
            // Show the source file along with line numbers. This assume that
            // the code is loaded at address 0. This will not work with
            // self-modifying code or if the CPU configured a different linear
            // addres to point to physical address 0.
            Source,
            // Disassemble the code at the current RIP. This will work in any
            // situation however some information is lost (e.g. label names,
            // ...).
            Disassembly,
        };
        // Drop-down selecting the format.
        std::unique_ptr<Dropdown<Format>> m_formatDropdown;

        // Override.
        virtual void doDraw(State const& state);

        // The two modes of the Code window.
        // doDraw when the current format is Source.
        void doDrawSourceFile(State const& state);
        // doDraw when the current format is Disassebly.
        void doDrawDisassembly(State const& state);
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
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        // The thickness of the stack frame separators.
        static constexpr float frameSeparatorThickness = 1.0f;

        // The maximum number of stack entries to be shown in the stack window.
        // The stack windows uses a list clipper so increasing this value does
        // not negatively impact performance.
        static constexpr u64 maxHistory = 1000;

        // Color of the addresses in the table.
        static constexpr ImVec4 addrColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

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

    // Different possible formats for displaying values.
    enum class DisplayFormat {
        Hexadecimal,
        SignedDecimal,
        UnsignedDecimal,
        FloatingPoint,
    };

    // Different possible formats for displaying vector registers or memory
    // dump.
    enum class Granularity {
        // Packed integers.
        Byte,
        Word,
        Dword,
        Qword,
        // Packed floating point values.
        Float,
        Double,
    };

    // How many bytes is a single element in the given granularity. Used to
    // compute how many elements a vector register contains in a given
    // granularity.
    static std::map<Granularity, u32> const granularityToBytes;

    // Maps all values of the DisplayFormat enum to a string representation. Can
    // be used for dropdown construction.
    static const std::map<DisplayFormat, std::string> formatToString;

    // Maps a DisplayFormat and number of bits to the format string needed to
    // print this value. For instance this maps (Hexadecimal, 64) to "0x%016lx".
    static const std::map<std::pair<DisplayFormat, u8>, char const*>
        displayFormatAndBitsToFormatString;

    // Show the current state of the VM's cpu (registers, page tables, ...).
    class CpuStateWindow : public Window {
    public:
        CpuStateWindow();

        // Toggle the next display granularity for the vector registers.
        void nextGranularity();
    private:
        static constexpr char const * defaultTitle = "Cpu state";

        // The color of "old" register values in the history list.
        static constexpr ImVec4 oldValColor = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

        // The color of unmapped page table entries in the page table tab.
        static constexpr ImVec4 unmappedColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

        // Draw the columns for each element of a vector register in the given
        // granularity. This function assumes that we are currently drawing a
        // table (e.g. between BeginTable and EndTable calls) and calls
        // TableNextColumn before printing out each element of the vector
        // register.
        template<size_t W>
        void drawColsForVec(vec<W> const& vec,
                            Granularity const granularity,
                            DisplayFormat const displayFormat);

        // Override.
        virtual void doDraw(State const& state);

        // Helper function to draw each tab of the window. Called by doDraw.
        // Draw the tab showing general purpose registers.
        void doDrawGeneralPurpose(State const& state);
        // Draw the tab showing the FPU and MMX registers.
        void doDrawFpuMmx(State const& state);
        // Draw the tab showing SSE & AVX registers.
        void doDrawSseAvx(State const& state);

        // Draw the tab showing the state of the page tables.
        void doDrawPageTables(State const& state);

        // Draw the tab showing the state of the GDT entries.
        void doDrawGdt(State const& state);

        // The dropdown used to select the display format of general purpose
        // registers.
        std::unique_ptr<Dropdown<DisplayFormat>> m_gpFormatDropdown;

        // MMX dropdowns for granularity and display format.
        std::unique_ptr<Dropdown<Granularity>> m_mmxGranularityDropdown;
        std::unique_ptr<Dropdown<DisplayFormat>> m_mmxFormatDropdown;

        // SSE/AVX dropdown for granularity and display format.
        std::unique_ptr<Dropdown<Granularity>> m_sseAvxGranularityDropdown;
        std::unique_ptr<Dropdown<DisplayFormat>> m_sseAvxFormatDropdown;
    };

    // Display the content of the VM's physical memory.
    class MemoryWindow : public Window {
    public:
        MemoryWindow();
    private:
        static constexpr char const * defaultTitle = "Memory";

        static constexpr ImGuiWindowFlags windowFlags = 
            Imgui::defaultWindowFlags |
            ImGuiWindowFlags_HorizontalScrollbar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        // The focused address in the memory dump in the memory window. The
        // table scrolls to this address. Can be changed using the input field.
        u64 m_focusedAddr;
        // The dropdown used to select the granularity of the memory dump, e.g.
        // bytes, words, ...
        std::unique_ptr<Dropdown<Granularity>> m_granDropdown;
        // The dropdown used to select the display format of the values of
        // memory.
        std::unique_ptr<Dropdown<DisplayFormat>> m_dispFormatDropdown;

        enum class AddressSpace {
            Physical,
            Linear,
        };
        // The dropdown used to select which address space is shown in the
        // memory window.
        std::unique_ptr<Dropdown<AddressSpace>> m_addressSpaceDropdown;
        // The current address space shown in the window.
        AddressSpace m_addressSpace;
        // Legend for the focus address input field.
        static constexpr char const * inputFieldText = "Jump to 0x";
        // The number of lines to show in the memory dump. FIXME: This really
        // shouldn't be hardcoded. Since we are using a clipper we can get away
        // with showing the entire physical memory.
        static constexpr u64 dumpNumLines = 5000;

        // Color of the addresses in the table.
        static constexpr ImVec4 addrColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        // Color of the non-printable chars in the ASCII format.
        static constexpr ImVec4 nonPrintColor = addrColor;
        // Color of the separator between the memory content and the ASCII
        // format.
        static constexpr ImVec4 separatorColor = addrColor;

        // Override.
        virtual void doDraw(State const& state);
    };

    // The set of windows making up the interface of x86Lab.
    std::unique_ptr<ConfigBar> m_configBar;
    std::unique_ptr<CodeWindow> m_codeWindow;
    std::unique_ptr<StackWindow> m_stackWindow;
    std::unique_ptr<CpuStateWindow> m_cpuStateWindow;
    std::unique_ptr<MemoryWindow> m_memoryWindow;
};
}
