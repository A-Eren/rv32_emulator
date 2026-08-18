// The application: mode state machine, layout, and key dispatch.
//
// App knows nothing about terminals. It draws into a Canvas and consumes Keys,
// so the whole interface can be driven headlessly -- which is what --render-once
// and --replay use, and which matters because debugging a curses application is
// painful precisely when stdout belongs to the UI.
//
// run_tui() is the thin part that owns a real Terminal and pumps the loop.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cmd/command.hpp"
#include "dbg/session.hpp"
#include "tui/layout.hpp"
#include "tui/views/encoding_view.hpp"
#include "tui/views/panels.hpp"
#include "tui/views/regs_view.hpp"
#include "tui/editor/editor_view.hpp"
#include "tui/views/source_view.hpp"

namespace rv::tui {

enum class AppMode : u8 {
    Edit,    // writing assembly
    Debug,   // stepping and inspecting
    Prompt,  // typing a ':' command
    Help,    // the key overlay
};

struct AppOptions {
    bool numeric_registers = false;
    /// Start in the editor rather than the debugger.
    bool start_in_editor = false;
    /// Where ^S writes. Empty means the file cannot be saved.
    std::string file_path;
    /// Draw with ASCII only, for terminals that mangle box-drawing glyphs.
    bool ascii = false;
};

class App {
public:
    App(dbg::DebugSession& session, AppOptions options = {});

    /// Draw one complete frame.
    void draw(Canvas& canvas);

    /// Returns false when the application should exit.
    bool handle_key(const Key& key);

    /// Advance a running program by one bounded slice. Called between key
    /// polls, which is how a long run stays interruptible without a thread.
    void tick();

    bool wants_quit() const { return quit_; }
    bool is_running() const { return run_mode_; }
    AppMode mode() const { return mode_; }

    void set_message(std::string text, bool is_error = false);
    const std::string& message() const { return message_; }

    /// Run a debugger command as if it had been typed at the ':' prompt. The
    /// TUI and the CLI share one implementation of every command.
    void run_command(const std::string& line);

    /// Assemble what is in the editor and load it. On failure the editor keeps
    /// the text, shows the diagnostics inline and jumps to the first error --
    /// which is the entire point of the write-F5-debug loop.
    bool assemble_and_load();
    bool save();

    EditorView& editor() { return editor_; }

private:
    struct DockTab {
        Widget* widget = nullptr;
        char key = '1';
    };

    void draw_status(Pane pane);
    void draw_editor(Pane pane);
    bool handle_edit_key(const Key& key);
    void draw_hints(Pane pane);
    void draw_message(Pane pane);
    void draw_prompt(Pane pane);
    void draw_tabs(Pane pane);
    void draw_help(Pane pane);
    void draw_too_small(Canvas& canvas);

    bool handle_prompt_key(const Key& key);
    bool handle_debug_key(const Key& key);

    void after_execution(const dbg::StopEvent& event);
    Widget& active_dock();

    dbg::DebugSession* session_;
    AppOptions options_;

    EditorView editor_;
    SourceView source_;
    RegsView registers_;
    EncodingView encoding_;
    MemView memory_;
    ConsoleView console_;
    MmioView mmio_;
    SymbolsView symbols_;
    std::vector<DockTab> dock_;
    std::size_t dock_index_ = 0;

    AppMode mode_ = AppMode::Debug;
    /// Where Escape returns to when leaving the prompt or the help overlay.
    AppMode base_mode_ = AppMode::Debug;
    bool quit_ = false;
    bool run_mode_ = false;
    /// Instructions executed since the current run began. run_chunk() has no
    /// budget of its own, so without this the tui would spin on an infinite
    /// loop forever while the cli's `run` stopped after the watchdog.
    u64 run_executed_ = 0;
    bool zoom_dock_ = false;
    /// False when the code pane has focus, true when the dock does.
    bool dock_focused_ = false;

    std::string message_;
    bool message_is_error_ = false;
    std::string prompt_input_;
    std::vector<std::string> prompt_history_;
    std::size_t history_index_ = 0;

    /// Buffer revision at the last successful assemble, so the status bar can
    /// say whether what is on screen has been built.
    u64 assembled_revision_ = 0;
    std::size_t last_error_count_ = 0;
};

struct TuiOptions {
    AppOptions app;
    /// Draw a single frame to stdout and exit. Debugging escape hatch.
    bool render_once = false;
    /// Feed these keys in before reading the keyboard, for scripted checks.
    std::vector<Key> replay;
};

/// Own a Terminal and pump the event loop. Returns a process exit code.
int run_tui(dbg::DebugSession& session, const TuiOptions& options = {});

/// Render one frame at the given size and return it as text. Used by
/// --render-once and by the golden-frame tests.
std::string render_frame(App& app, int width, int height);

}  // namespace rv::tui
