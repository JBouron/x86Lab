#pragma once
#include <ncurses.h>
#include <x86lab/util.hpp>
#include <x86lab/ui/ui.hpp>
#include <string_view>

namespace X86Lab::Ui {

// Backend implementation using Ncurse as the underlying UI.
class Tui : public Backend {
public:
    // Construct a Tui backend, initializing ncurse if need be.
    Tui();

    // Free up resources tied to this backend.
    ~Tui();
private:
    // Implementation of waitForNextAction.
    virtual Action doWaitForNextAction();

    // Cycle the tabs in the register window.
    // @param toRight: The direction of the cycling.
    void cycleTabs(bool const toRight);

    // Implementation of update.
    virtual void doUpdate(State const& newState);

    // Update the register windows with the new register values.
    // @param prevRegs: Previous register values.
    // @param newRegs: Latest register values.
    void doUpdateRegWin(Snapshot::Registers const& prevRegs,
                        Snapshot::Registers const& newRegs);

    // Helper function for doUpdateRegWin when the current mode is set to
    // GeneralPurpose.
    // @param prevRegs: Previous register values.
    // @param newRegs: Latest register values.
    void doUpdateRegWinGp(Snapshot::Registers const& prevRegs,
                          Snapshot::Registers const& newRegs);

    // Helper function for doUpdateRegWin when the current mode is set to
    // FpuMmx.
    // @param prevRegs: Previous register values.
    // @param newRegs: Latest register values.
    void doUpdateRegWinFpuMmx(Snapshot::Registers const& prevRegs,
                              Snapshot::Registers const& newRegs);
 
    // Update the code window.
    // @param fileName: The source code file path.
    // @param currLine: line number of the current instruction. The code window
    // will center on this line. If 0 then the code window starts at line 1.
    void doUpdateCodeWin(std::string const& fileName, u64 const currLine);

    // Implementation of log.
    virtual void doLog(std::string const& msg);

    // Refresh all windows.
    void refresh();

    // Wrapper around ncurses' window type. This nicely encapsulate state of the
    // window as well as its borders.
    class Window {
    public:
        // Create a ncurses window. This does not refresh the window and the new
        // window has its coordinates set to the default corrdinates
        // Window::defaultPosX and Window::defaultPosY. Note that the height and
        // width is the geometry of the border (of thickness 1); the actual
        // window area is therefore (height - 2) x (width - 2) assuming the
        // inner padding is 0 (see innerPadX and innerPadY).
        // @param height: The height of the window.
        // @param width: The width of the window.
        // @param title: The title of the window.
        Window(u32 const height, u32 const width, std::string const& title);

        // Free up resources associated with this window.
        ~Window();

        // Move the window. This function will refresh the window.
        // @param y: The requested y position.
        // @param x: The requested x position.
        void move(u32 const y, u32 const x);

        // Force refresh the window.
        void refresh();

        // Clear the content of a window, and reset the cursor position to be at
        // the top left corner.
        void clearAndResetCursor();

        // Print in the window area. This takes into account line splitting and
        // new lines.
        // @param str: The string to print.
        Window& operator<<(char const * const str);

        // Get the next user input on the window, this is a wrapper to a call of
        // getch.
        // @return: Input char.
        int getChar();

        // Enable automatic scrolling on a window.
        // @param enabled: If true, scrolling is enabled, else disabled.
        void enableScrolling(bool const enabled);

        // Set the window's title.
        // @param newTitle: The new title to use for this window.
        void setTitle(std::string const& newTitle);

    private:
        // Some constants regarding window geometry.
        // Default position when creating a window.
        static constexpr u32 defaultPosX = 0;
        static constexpr u32 defaultPosY = 0;
        // The offset of the title of a window relative to the top left corner
        // of the border. The title is always printed on the top border of a
        // window.
        static constexpr u32 titleOffset = 2;
        // The inner padding within the window. innerPadX indicate how many
        // space characters there are between the border and the window printing
        // area (eg. window content). Conversely innerPadY is the number of
        // empty lines between the top/down border and window content.
        static constexpr u32 innerPadX = 3;
        static constexpr u32 innerPadY = 2;

        // Ncurses' borders are peculiar as they are "inner" borders, meaning
        // that they are part of the window's content. This is undesirable as
        // printing content in the window will overwrite the border. Hence use a
        // second window as the "border".
        WINDOW* m_borderWin;
        WINDOW* m_win;

        // Title of the window.
        std::string m_title;
        // Height and width of the window in rows and cols respectively.
        u32 m_height;
    };

    // Ncurses only need to be initialized once. In the event that we have two
    // Tui instances or more, make sure it does not get initialized more than
    // once.
    static bool isNcurseInitialized;

    // The Tui interface contains three windows:
    // The window showing the code being run.
    static constexpr char const * codeWinTitle = "Code";
    Window* codeWin;
    // The window showing the current values of the registers.
    static constexpr char const * regWinTitle = "Registers";
    Window* regWin;
    // The window showing the logs generated by the runner.
    static constexpr char const * logWinTitle = "Logs";
    Window* logWin;

    // The previous and current state of the registers; currently displayed in
    // the registers window. FIXME: Use a cleaner approach other than copying
    // this stuff. This also significantly increases the size of the Tui class.
    Snapshot::Registers prevRegs;
    Snapshot::Registers currRegs;

    // The information currently displayed on the register window.
    enum class RegisterWindowMode {
        // General purpose, segments and control registers. Along with IDT /
        // GDT.
        GeneralPurpose,
        // Fpu registers and MMX.
        FpuMmx,

        // Must be the last value.
        NumRegisterWindowMode,
    };
    // Get the title for a RegisterWindowMode.
    // @param mode: The mode to get the title for.
    static std::string titleForRegisterWindowMode(
        RegisterWindowMode const& mode);

    RegisterWindowMode m_currentMode;

    // Check if the register window is currently showing vector registers.
    // @return: true if the current tab of the register window contains vector
    // registers, false otherwise.
    bool isRegWindowShowingVectorRegisters() const;

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
    // The current granularity of vector registers displayed in the register
    // window.
    VectorRegisterGranularity m_currentGranularity;

    // Helper function to get the string representation of a vector register in
    // a particular granularity.
    // @param vec: The vector register to represent as a string.
    // @param granularity: The VectorRegisterGranularity granularity to use in
    // the representation.
    // @return: A string representing the current value of the vector register
    // in the given granularity. Each element (e.g packed byte, word, ...) is
    // separated by a space.
    template<size_t W>
    static std::string vecRegToString(
        vec<W> const& vec,
        VectorRegisterGranularity const granularity);

    // Update the current granularity to the next one following the order in
    // VectorRegisterGranularity.
    void cycleGranularity();

    // Width and height of the window, as a percentage of the terminal's width
    // and height. The layout of the TUI backend is as follows (not to scale):
    // +---------+---------+
    // |         |         |
    // |         |         |
    // |  CODE   |  REGS   |
    // |         |         |
    // |         |         |
    // +---------+---------+
    // |       LOGS        |
    // +---------+---------+
    static constexpr float codeWinWidth = 0.5;
    static constexpr float codeWinHeight = 0.75;
    static constexpr float regWinWidth = 1.0 - codeWinWidth;
    static constexpr float regWinHeight = codeWinHeight;
    static constexpr float logWinWidth = 1.0;
    static constexpr float logWinHeight = 1.0 - codeWinHeight;

    // The size of the gap between the border of the terminal and the windows,
    // as well as between the windows themselves.
    static constexpr u32 gapSize = 1;
};

}
