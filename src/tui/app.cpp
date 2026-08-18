#include "tui/app.hpp"

#include <algorithm>
#include <cstdio>

#include <fstream>

#include "asm/diag_render.hpp"
#include "cmd/format.hpp"
#include "isa/disasm.hpp"
#include "tui/string_canvas.hpp"
#include "tui/term.hpp"

namespace rv::tui {
namespace {

/// Instructions per slice while running. Bounds worst-case input latency to a
/// few milliseconds while keeping the loop single-threaded -- a worker thread
/// would buy nothing here (speed is not a goal) and cost a class of
/// nondeterministic bugs that are agony to debug inside a curses app.
constexpr u64 kRunChunk = 20000;

std::string truncate_to(const std::string& text, int width) {
    if (width <= 0) return {};
    if (static_cast<int>(text.size()) <= width) return text;
    return text.substr(0, static_cast<std::size_t>(width) - 1) + "…";
}

}  // namespace

App::App(dbg::DebugSession& session, AppOptions options)
    : session_(&session),
      options_(options),
      editor_(session),
      source_(session),
      registers_(session),
      encoding_(session),
      memory_(session),
      console_(session),
      mmio_(session),
      symbols_(session) {
    dock_ = {
        {&registers_, '1'}, {&memory_, '2'}, {&encoding_, '3'},
        {&console_, '4'},   {&mmio_, '5'},   {&symbols_, '6'},
    };
    source_.follow_pc();
    editor_.set_text(session.source().text());
    // The session was loaded from this very text, so the buffer starts in the
    // built state rather than claiming it has never been assembled.
    assembled_revision_ = editor_.buffer().revision();
    if (options_.start_in_editor) {
        mode_ = AppMode::Edit;
        base_mode_ = AppMode::Edit;
    }
}

Widget& App::active_dock() { return *dock_[dock_index_].widget; }

void App::set_message(std::string text, bool is_error) {
    message_ = std::move(text);
    message_is_error_ = is_error;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void App::draw_status(Pane pane) {
    pane.fill_all(StyleId::StatusBar);
    int column = 1;

    if (base_mode_ == AppMode::Edit) {
        pane.put_at(column, 0, " EDIT ", StyleId::StatusAccent);
        ++column;

        char where[128];
        std::snprintf(where, sizeof where, "%s%s   Ln %u, Col %u",
                      options_.file_path.empty() ? "<unsaved>" : options_.file_path.c_str(),
                      editor_.modified() ? "*" : "", editor_.cursor().line,
                      editor_.cursor().col + 1);
        pane.put_at(column, 0, where, StyleId::StatusBar);
        column += 2;

        const std::size_t errors = editor_.buffer().revision() == assembled_revision_
                                       ? last_error_count_
                                       : 0;
        if (editor_.buffer().revision() != assembled_revision_) {
            pane.put_at(column, 0, "not assembled", StyleId::StatusBar);
        } else if (errors > 0) {
            char text[64];
            std::snprintf(text, sizeof text, "%zu error%s", errors, errors == 1 ? "" : "s");
            pane.put_at(column, 0, text, StyleId::StatusAccent);
        } else {
            pane.put_at(column, 0, "assembled", StyleId::StatusBar);
        }
        return;
    }

    pane.put_at(column, 0, run_mode_ ? " RUNNING " : " DEBUG ", StyleId::StatusAccent);
    ++column;

    char state[96];
    std::snprintf(state, sizeof state, "pc %08x  cyc %llu", session_->cpu().pc,
                  static_cast<unsigned long long>(session_->cpu().cycle));
    pane.put_at(column, 0, state, StyleId::StatusBar);
    column += 2;

    if (session_->halted()) {
        pane.put_at(column, 0, "HALTED", StyleId::StatusAccent);
        column += 2;
    }

    // The current instruction, plus which word of its source line it is. That
    // [1/2] is how a two-instruction `li` stops being a mystery.
    const Word word = session_->read_imem_word(session_->cpu().pc);
    std::string text = isa::disassemble_word(word, session_->cpu().pc);
    if (const auto slot = session_->current_slot(); slot && slot->second > 1) {
        char suffix[32];
        std::snprintf(suffix, sizeof suffix, "  [%u/%u]", slot->first + 1, slot->second);
        text += suffix;
    }
    pane.put(column, 0, truncate_to(text, pane.width() - column - 1), StyleId::StatusBar);
}

void App::draw_hints(Pane pane) {
    const char* text = nullptr;
    if (mode_ == AppMode::Prompt) {
        text = "Enter run   Esc cancel   ↑↓ history";
    } else if (base_mode_ == AppMode::Edit) {
        text = "F5 assemble+debug  ^S save  ^Z undo  F9 bp  F4 next error  ? help  ^Q quit";
    } else {
        text = "F8 step  F7 si  F10 next  F5 run  F6 back  F9 bp  F2 edit  : cmd  ? help  q quit";
    }
    pane.put(0, 0, truncate_to(text, pane.width()), StyleId::Hint);
}

void App::draw_message(Pane pane) {
    if (message_.empty()) return;
    pane.put(0, 0, truncate_to(message_, pane.width()),
             message_is_error_ ? StyleId::Error : StyleId::Normal);
}

void App::draw_prompt(Pane pane) {
    if (mode_ != AppMode::Prompt) return;
    pane.put(0, 0, ":", StyleId::StatusAccent);
    pane.put(1, 0, truncate_to(prompt_input_, pane.width() - 2));
    pane.set_cursor(1 + std::min<int>(static_cast<int>(prompt_input_.size()), pane.width() - 2), 0);
}

void App::draw_tabs(Pane pane) {
    int column = 0;
    for (std::size_t index = 0; index < dock_.size(); ++index) {
        const bool active = index == dock_index_;
        const std::string label =
            std::string(active ? "[" : " ") + dock_[index].key + " " +
            dock_[index].widget->title() + (active ? "]" : " ");
        pane.put_at(column, 0, label, active ? StyleId::Title : StyleId::Dim);
    }
    // Fill the rest of the bar so the tabs read as a strip rather than as
    // floating words.
    if (column < pane.width()) {
        pane.put(column, 0, std::string(static_cast<std::size_t>(pane.width() - column), ' '),
                 StyleId::Frame);
    }
    if (zoom_dock_) {
        const std::string note = "F11 unzoom";
        pane.put(pane.width() - static_cast<int>(note.size()) - 1, 0, note, StyleId::Dim);
    }
}

void App::draw_help(Pane pane) {
    pane.fill_all(StyleId::Normal);
    static const char* const kLines[] = {
        "  rv32 -- keys",
        "",
        "  execution",
        "    F8 / s     step one source line",
        "    F7 / i     step one machine instruction (shows pseudo expansions)",
        "    F10 / n    step over a call",
        "    F5 / c     run until something stops it",
        "    F6 / b     step backwards one line",
        "    F4         reset the machine",
        "",
        "  navigation",
        "    ↑ ↓        move the selection",
        "    g          jump back to where execution is",
        "    F9         toggle a breakpoint on the selected line",
        "    Tab        move focus between the code pane and the dock",
        "    1..6       choose a dock panel",
        "    F11        zoom the dock to full screen",
        "",
        "  other",
        "    :          type a debugger command (same set as the CLI)",
        "    ? / F1     this help",
        "    q          quit",
        "",
        "  Press any key to continue.",
    };
    for (std::size_t index = 0; index < sizeof kLines / sizeof kLines[0]; ++index) {
        const int row = static_cast<int>(index);
        if (row >= pane.height()) break;
        const std::string_view text = kLines[index];
        const bool heading = !text.empty() && text[2] != ' ' && text.find("F") != 2;
        pane.put(0, row, text, heading ? StyleId::Title : StyleId::Normal);
    }
}

void App::draw_too_small(Canvas& canvas) {
    Pane pane(canvas, Rect{0, 0, canvas.size().width, canvas.size().height});
    pane.fill_all(StyleId::Normal);
    char text[96];
    std::snprintf(text, sizeof text, "terminal too small: need %dx%d, have %dx%d", kMinWidth,
                  kMinHeight, canvas.size().width, canvas.size().height);
    pane.put(0, 0, text, StyleId::Error);
    if (pane.height() > 1) pane.put(0, 1, "resize, or use 'rv32 dbg' instead", StyleId::Dim);
}

void App::draw_editor(Pane pane) {
    editor_.focused = true;
    editor_.draw(pane);
}

bool App::assemble_and_load() {
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;  // the editor has no room for prose

    const std::string name =
        options_.file_path.empty() ? std::string("<editor>") : options_.file_path;

    // Assemble a detached copy first, so a failed build never destroys the
    // program that is currently loaded and steppable.
    const as::SourceFile candidate(name, editor_.text());
    as::AssembledProgram attempt = as::assemble(candidate, options);

    editor_.set_diagnostics(attempt.diagnostics);
    last_error_count_ = attempt.diagnostics.error_count();
    assembled_revision_ = editor_.buffer().revision();

    if (!attempt.ok()) {
        editor_.go_to_first_error();
        const as::Diagnostic* first = attempt.diagnostics.first_error();
        set_message(first != nullptr ? first->message : "assembly failed", true);
        return false;
    }

    session_->load_source(editor_.text(), name, options);
    registers_.forget_history();
    source_.follow_pc();
    encoding_.follow_pc();

    char text[128];
    std::snprintf(text, sizeof text, "assembled: %zu instructions, %zu bytes of data",
                  session_->program().imem_words.size(), session_->program().dmem_bytes.size());
    set_message(text);
    return true;
}

bool App::save() {
    if (options_.file_path.empty()) {
        set_message("no file name; this buffer cannot be saved", true);
        return false;
    }
    std::ofstream file(options_.file_path, std::ios::binary);
    if (!file) {
        set_message("cannot write " + options_.file_path, true);
        return false;
    }
    file << editor_.text();
    if (!file) {
        set_message("write to " + options_.file_path + " failed", true);
        return false;
    }
    editor_.mark_saved();
    set_message("saved " + options_.file_path);
    return true;
}

bool App::handle_edit_key(const Key& key) {
    switch (key.code) {
        case KeyCode::F5:
            if (assemble_and_load()) {
                base_mode_ = AppMode::Debug;
                mode_ = AppMode::Debug;
            }
            return true;
        case KeyCode::F2:
            base_mode_ = AppMode::Debug;
            mode_ = AppMode::Debug;
            return true;
        case KeyCode::F4: {
            // Walk to the next diagnostic, which is what you want after a
            // failed assemble.
            if (!editor_.go_to_first_error()) set_message("no errors");
            return true;
        }
        case KeyCode::F9: {
            const u32 line = editor_.cursor().line;
            const bool added = session_->breakpoints().toggle_line(line, session_->map());
            set_message(added ? "breakpoint set on line " + std::to_string(line)
                              : "breakpoint cleared on line " + std::to_string(line));
            return true;
        }
        case KeyCode::F1: mode_ = AppMode::Help; return true;
        default: break;
    }

    if (key.ctrl && key.code == KeyCode::Char) {
        switch (key.ch | 0x20u) {
            case 's': save(); return true;
            case 'q': quit_ = true; return true;
            case 'd':
                base_mode_ = AppMode::Debug;
                mode_ = AppMode::Debug;
                return true;
            default: break;
        }
    }

    if (editor_.on_key(key)) {
        // The encoding panel follows the caret, so a line explains itself while
        // it is still being written.
        const auto& addrs = session_->map().addrs_for_line(editor_.cursor().line);
        if (!addrs.empty()) encoding_.set_address(addrs.front());

        if (const as::Diagnostic* diagnostic = editor_.diagnostic_at_cursor()) {
            set_message(diagnostic->message, diagnostic->severity == as::Severity::Error);
        } else {
            message_.clear();
        }
        return true;
    }
    return false;
}

void App::draw(Canvas& canvas) {
    const Size size = canvas.size();
    if (LayoutEngine::too_small(size)) {
        draw_too_small(canvas);
        return;
    }

    Pane screen(canvas, Rect{0, 0, size.width, size.height});
    screen.fill_all(StyleId::Normal);

    const DebugLayout layout = LayoutEngine::debug(size, zoom_dock_);

    draw_status(screen.sub(layout.status));

    if (mode_ == AppMode::Help) {
        // The overlay takes the body and leaves the chrome, so it is obvious
        // it is temporary.
        Rect body = layout.code;
        body.height = layout.dock.bottom() - layout.code.y;
        draw_help(screen.sub(body));
    } else if (base_mode_ == AppMode::Edit) {
        // Editing gets the body, with the dock kept so the encoding panel can
        // follow the caret.
        const EditLayout edit_layout = LayoutEngine::edit(size);
        draw_editor(screen.sub(edit_layout.editor));
        draw_message(screen.sub(edit_layout.diagnostic));
        draw_hints(screen.sub(edit_layout.hints));
        draw_prompt(screen.sub(edit_layout.prompt));
        return;
    } else {
        if (!layout.code.empty()) {
            source_.focused = !dock_focused_;
            source_.draw(screen.sub(layout.code));
        }
        if (!layout.side.empty()) registers_.draw(screen.sub(layout.side));
        draw_tabs(screen.sub(layout.tabs));
        active_dock().focused = dock_focused_;
        active_dock().draw(screen.sub(layout.dock));
    }

    draw_message(screen.sub(layout.message));
    draw_hints(screen.sub(layout.hints));
    draw_prompt(screen.sub(layout.prompt));
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::after_execution(const dbg::StopEvent& event) {
    registers_.note_step();
    source_.on_execution_moved();
    memory_.follow_last_store();
    encoding_.follow_pc();

    const std::string text = cmd::format_stop(*session_, event);
    if (!text.empty()) {
        std::string trimmed = text;
        while (!trimmed.empty() && trimmed.back() == '\n') trimmed.pop_back();
        set_message(trimmed, event.reason == dbg::StopReason::Watchdog);
    } else {
        message_.clear();
    }
    if (event.reason != dbg::StopReason::Step) run_mode_ = false;
}

void App::run_command(const std::string& line) {
    std::string output;
    cmd::CmdContext context{*session_,
                            [&output](const std::string& text) { output += text; },
                            cmd::FormatOptions{options_.numeric_registers, 80, true},
                            false};
    const cmd::CmdResult result = cmd::CommandRegistry::instance().execute(context, line);

    if (!result.ok) {
        set_message(result.error, true);
    } else {
        // Commands print multi-line reports meant for a scrolling terminal.
        // The message row gets the first meaningful line; the rest is already
        // visible in the panels.
        std::string first;
        for (const char character : output) {
            if (character == '\n') break;
            first.push_back(character);
        }
        set_message(first, false);
    }
    if (context.quit) quit_ = true;

    registers_.note_step();
    source_.on_execution_moved();
    memory_.follow_last_store();
}

bool App::handle_prompt_key(const Key& key) {
    switch (key.code) {
        case KeyCode::Escape:
            mode_ = base_mode_;
            prompt_input_.clear();
            return true;
        case KeyCode::Enter: {
            mode_ = base_mode_;
            if (!prompt_input_.empty()) {
                prompt_history_.push_back(prompt_input_);
                history_index_ = prompt_history_.size();
                run_command(prompt_input_);
            }
            prompt_input_.clear();
            return true;
        }
        case KeyCode::Backspace:
            if (!prompt_input_.empty()) prompt_input_.pop_back();
            return true;
        case KeyCode::Up:
            if (history_index_ > 0) {
                --history_index_;
                prompt_input_ = prompt_history_[history_index_];
            }
            return true;
        case KeyCode::Down:
            if (history_index_ + 1 < prompt_history_.size()) {
                ++history_index_;
                prompt_input_ = prompt_history_[history_index_];
            } else {
                history_index_ = prompt_history_.size();
                prompt_input_.clear();
            }
            return true;
        default: break;
    }
    if (key.code == KeyCode::Char && !key.ctrl && key.ch >= 0x20 && key.ch < 0x7f) {
        prompt_input_.push_back(static_cast<char>(key.ch));
        return true;
    }
    return true;  // the prompt swallows everything while it is open
}

bool App::handle_debug_key(const Key& key) {
    using dbg::StepKind;

    // Running: any key stops it. That is the escape hatch for an accidental
    // infinite loop, and it must not require knowing a particular key.
    if (run_mode_) {
        run_mode_ = false;
        set_message("interrupted");
        return true;
    }

    switch (key.code) {
        case KeyCode::F8: after_execution(session_->step(StepKind::Line)); return true;
        case KeyCode::F7: after_execution(session_->step(StepKind::Instruction)); return true;
        case KeyCode::F10: after_execution(session_->step(StepKind::LineOver)); return true;
        case KeyCode::F5:
            run_mode_ = true;
            run_executed_ = 0;
            message_.clear();
            return true;
        case KeyCode::F6: {
            if (!session_->can_reverse()) {
                set_message("no further history", true);
                return true;
            }
            after_execution(session_->reverse_step(StepKind::Line));
            return true;
        }
        case KeyCode::F4:
            session_->reset();
            registers_.forget_history();
            source_.follow_pc();
            set_message("machine reset; breakpoints kept");
            return true;
        case KeyCode::F9: {
            const u32 line = source_.selected_line();
            const bool added = session_->breakpoints().toggle_line(line, session_->map());
            set_message(added ? "breakpoint set on line " + std::to_string(line)
                              : "breakpoint cleared on line " + std::to_string(line));
            return true;
        }
        case KeyCode::F11:
            zoom_dock_ = !zoom_dock_;
            if (zoom_dock_) dock_focused_ = true;
            return true;
        case KeyCode::F1:
            mode_ = AppMode::Help;
            return true;
        case KeyCode::F2:
            base_mode_ = AppMode::Edit;
            mode_ = AppMode::Edit;
            editor_.set_cursor(Pos{session_->current_line().value_or(1), 0});
            return true;
        case KeyCode::Tab:
            dock_focused_ = !dock_focused_;
            return true;
        default: break;
    }

    if (key.code == KeyCode::Char) {
        switch (key.ch) {
            case 'q': quit_ = true; return true;
            case '?': mode_ = AppMode::Help; return true;
            case ':':
                mode_ = AppMode::Prompt;
                prompt_input_.clear();
                history_index_ = prompt_history_.size();
                return true;
            case 's': after_execution(session_->step(StepKind::Line)); return true;
            case 'i': after_execution(session_->step(StepKind::Instruction)); return true;
            case 'n': after_execution(session_->step(StepKind::LineOver)); return true;
            case 'c':
                run_mode_ = true;
                run_executed_ = 0;
                message_.clear();
                return true;
            case 'b':
                if (session_->can_reverse()) {
                    after_execution(session_->reverse_step(StepKind::Line));
                } else {
                    set_message("no further history", true);
                }
                return true;
            default: break;
        }
        if (key.ch >= '1' && key.ch <= '9') {
            const std::size_t index = key.ch - '1';
            if (index < dock_.size()) {
                dock_index_ = index;
                dock_focused_ = true;
                return true;
            }
        }
    }

    // Anything left over goes to whichever pane has focus.
    Widget& focused = dock_focused_ ? active_dock() : static_cast<Widget&>(source_);
    if (focused.on_key(key)) {
        // Moving the selection should move what the encoding panel shows, so
        // browsing the program explains itself as you go.
        if (!dock_focused_) {
            const auto& addrs = session_->map().addrs_for_line(source_.selected_line());
            if (!addrs.empty()) encoding_.set_address(addrs.front());
        }
        return true;
    }
    return true;
}

bool App::handle_key(const Key& key) {
    if (!key.valid()) return !quit_;

    if (key.code == KeyCode::Resize) return !quit_;

    if (mode_ == AppMode::Help) {
        mode_ = base_mode_;
        return !quit_;
    }
    if (mode_ == AppMode::Prompt) {
        handle_prompt_key(key);
        return !quit_;
    }
    if (mode_ == AppMode::Edit) {
        handle_edit_key(key);
        return !quit_;
    }
    handle_debug_key(key);
    return !quit_;
}

void App::tick() {
    if (!run_mode_) return;

    const u64 watchdog = session_->options().watchdog;
    const u64 slice = std::min(kRunChunk, watchdog - std::min(watchdog, run_executed_));

    if (const auto event = session_->run_chunk(slice)) {
        after_execution(*event);
        return;
    }
    run_executed_ += slice;

    if (run_executed_ >= watchdog) {
        run_mode_ = false;
        char text[128];
        std::snprintf(text, sizeof text,
                      "stopped after %llu instructions -- infinite loop?",
                      static_cast<unsigned long long>(run_executed_));
        set_message(text, true);
        return;
    }

    // Still going. Keep the panels current so a long run is watchable.
    registers_.note_step();
    source_.on_execution_moved();
}

// ---------------------------------------------------------------------------
// Hosting
// ---------------------------------------------------------------------------

std::string render_frame(App& app, int width, int height) {
    StringCanvas canvas(width, height);
    app.draw(canvas);
    return canvas.to_string();
}

int run_tui(dbg::DebugSession& session, const TuiOptions& options) {
    App app(session, options.app);

    if (options.render_once) {
        std::fputs(render_frame(app, 80, 24).c_str(), stdout);
        return 0;
    }

    if (!is_interactive()) {
        std::fprintf(stderr,
                     "rv32: the tui needs a terminal; use 'rv32 dbg' for a scriptable "
                     "debugger\n");
        return 2;
    }

    Terminal terminal;
    std::size_t replay_index = 0;

    while (!app.wants_quit()) {
        terminal.canvas().erase_all();
        app.draw(terminal.canvas());
        terminal.canvas().present();

        Key key;
        if (replay_index < options.replay.size()) {
            key = options.replay[replay_index++];
        } else {
            // Non-blocking while running, so a long program stays interruptible
            // and the panels keep updating.
            key = terminal.poll(app.is_running() ? 0 : -1);
        }

        if (key.valid()) {
            app.handle_key(key);
        } else {
            app.tick();
        }
    }
    return 0;
}

}  // namespace rv::tui
