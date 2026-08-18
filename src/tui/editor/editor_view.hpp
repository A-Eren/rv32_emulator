// The embedded editor.
//
// The centrepiece: write assembly here, press F5, step through what you wrote.
//
// It shares the gutter with the debugger's source view -- same breakpoint
// marks, same execution arrow -- so switching between editing and stepping does
// not feel like switching tools. Diagnostics from the last assemble are shown
// inline, and the highlighter is the assembler's own lexer, so the colours
// cannot disagree with what F5 will do.
//
// Deliberately not supported, and worth saying out loud because each would eat
// the project: multiple files or buffers, split panes, regex replace, macros,
// mouse input, and non-ASCII *input*. The buffer is byte-oriented ASCII;
// assembly is ASCII, and wide-character cursor arithmetic would spread through
// every one of these functions for no benefit.
#pragma once

#include <string>
#include <vector>

#include "asm/diagnostic.hpp"
#include "dbg/session.hpp"
#include "tui/editor/text_buffer.hpp"
#include "tui/editor/undo.hpp"
#include "tui/widget.hpp"

namespace rv::tui {

class EditorView final : public Widget {
public:
    explicit EditorView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    bool on_key(const Key& key) override;
    std::string title() const override { return "Edit"; }

    TextBuffer& buffer() { return buffer_; }
    const TextBuffer& buffer() const { return buffer_; }

    void set_text(std::string_view text);
    std::string text() const { return buffer_.to_string(); }

    Pos cursor() const { return cursor_; }
    void set_cursor(Pos position);

    bool modified() const { return modified_; }
    void mark_saved() { modified_ = false; }

    /// Diagnostics from the last assemble, shown in the gutter and underlined.
    void set_diagnostics(const as::DiagBag& diagnostics);
    void clear_diagnostics() { diagnostics_.clear(); }
    /// The diagnostic under the cursor, for the message row. Null if none.
    const as::Diagnostic* diagnostic_at_cursor() const;

    /// Jump to the first error, which is what should happen when F5 fails.
    bool go_to_first_error();

    /// Number of columns the gutter takes, so the app can align other things
    /// with the text.
    int gutter_width() const;

private:
    void insert_text(std::string_view text, EditKind kind);
    void delete_range(Range range, EditKind kind);
    void insert_newline_with_indent();
    void backspace();
    void delete_forward();
    void move_cursor(Pos target, bool keep_goal_column = false);
    void ensure_visible(int height, int text_width);

    const dbg::DebugSession* session_;
    TextBuffer buffer_;
    UndoStack undo_;
    as::DiagBag diagnostics_;

    Pos cursor_{1, 0};
    /// The column an up/down motion aims for. Without it, moving down through a
    /// short line and back up loses the original column -- the detail everyone
    /// forgets and everyone notices.
    u32 goal_column_ = 0;

    u32 top_ = 1;       // first visible line
    u32 left_ = 0;      // horizontal scroll, in bytes
    int last_height_ = 0;
    bool modified_ = false;

public:
    /// Spaces per indent. Tab inserts spaces: mixed tabs and spaces in the
    /// gutter make the caret and the diagnostic underline disagree.
    static constexpr u32 kIndentWidth = 4;
};

}  // namespace rv::tui
