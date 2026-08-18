#include "tui/editor/editor_view.hpp"

#include <algorithm>
#include <cstdio>

#include "tui/highlight.hpp"

namespace rv::tui {
namespace {

constexpr int kScrollOff = 2;

/// The leading whitespace of a line, for auto-indent.
std::string_view leading_space(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");
    return text.substr(0, first == std::string_view::npos ? text.size() : first);
}

bool ends_a_label(std::string_view text) {
    // Ignore a trailing comment when deciding, so `loop:  # go` still indents.
    const std::size_t comment = text.find_first_of("#;");
    std::string_view code = text.substr(0, comment);
    while (!code.empty() && (code.back() == ' ' || code.back() == '\t')) code.remove_suffix(1);
    return !code.empty() && code.back() == ':';
}

StyleId style_for_severity(as::Severity severity) {
    switch (severity) {
        case as::Severity::Error: return StyleId::Error;
        case as::Severity::Warning: return StyleId::Warning;
        case as::Severity::Note:
        case as::Severity::Help: return StyleId::Note;
    }
    return StyleId::Normal;
}

/// The style for the span a diagnostic points at.
StyleId span_style_for_severity(as::Severity severity) {
    return severity == as::Severity::Error ? StyleId::ErrorSpan : StyleId::WarningSpan;
}

}  // namespace

void EditorView::set_text(std::string_view text) {
    buffer_.set_text(text);
    undo_.clear();
    diagnostics_.clear();
    cursor_ = Pos{1, 0};
    goal_column_ = 0;
    top_ = 1;
    left_ = 0;
    modified_ = false;
}

void EditorView::set_cursor(Pos position) {
    cursor_ = buffer_.clamp(position);
    goal_column_ = cursor_.col;
    // A deliberate jump ends the current typing run, so undo lands on a
    // boundary that matches what the person did.
    undo_.break_group();
}

void EditorView::set_diagnostics(const as::DiagBag& diagnostics) { diagnostics_ = diagnostics; }

const as::Diagnostic* EditorView::diagnostic_at_cursor() const {
    const std::vector<const as::Diagnostic*> found = diagnostics_.for_line(cursor_.line);
    return found.empty() ? nullptr : found.front();
}

bool EditorView::go_to_first_error() {
    const as::Diagnostic* first = diagnostics_.first_error();
    if (first == nullptr) return false;
    set_cursor(Pos{first->primary.line, first->primary.col});
    return true;
}

int EditorView::gutter_width() const {
    int digits = 1;
    for (std::size_t value = buffer_.line_count(); value >= 10; value /= 10) ++digits;
    // breakpoint/diagnostic marker, execution arrow, number, separator
    return 2 + std::max(digits, 2) + 1;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void EditorView::insert_text(std::string_view text, EditKind kind) {
    EditRecord record;
    record.at = cursor_;
    record.cursor_before = cursor_;
    record.inserted = std::string(text);

    cursor_ = buffer_.insert(cursor_, text);
    record.cursor_after = cursor_;
    undo_.record(std::move(record), kind);

    goal_column_ = cursor_.col;
    modified_ = true;
}

void EditorView::delete_range(Range range, EditKind kind) {
    if (range.empty()) return;

    EditRecord record;
    record.at = range.begin;
    record.cursor_before = cursor_;
    record.removed = buffer_.erase(range);

    cursor_ = range.begin;
    record.cursor_after = cursor_;
    undo_.record(std::move(record), kind);

    goal_column_ = cursor_.col;
    modified_ = true;
}

void EditorView::insert_newline_with_indent() {
    const std::string_view current = buffer_.line(cursor_.line);
    std::string indent(leading_space(current.substr(0, cursor_.col)));
    // A label opens a block, so the next line goes one level in. Small touch,
    // and it removes most of the manual spacing from writing assembly.
    if (ends_a_label(current)) indent.append(kIndentWidth, ' ');

    insert_text("\n" + indent, EditKind::Structural);
}

void EditorView::backspace() {
    if (cursor_.col > 0) {
        // Deleting back through indentation removes a whole level, which is
        // what makes a four-space indent feel like a tab.
        const std::string_view text = buffer_.line(cursor_.line);
        const std::string_view before = text.substr(0, cursor_.col);
        u32 width = 1;
        if (before.find_first_not_of(' ') == std::string_view::npos && cursor_.col > 0) {
            const u32 over = cursor_.col % kIndentWidth;
            width = over == 0 ? kIndentWidth : over;
        }
        width = std::min(width, cursor_.col);
        delete_range(Range{Pos{cursor_.line, cursor_.col - width}, cursor_},
                     width == 1 ? EditKind::DeleteBack : EditKind::Structural);
        return;
    }
    if (cursor_.line == 1) return;
    // Joining onto the previous line is structural: undo should restore the
    // split in one go.
    const Pos target = buffer_.end_of_line(cursor_.line - 1);
    delete_range(Range{target, cursor_}, EditKind::Structural);
}

void EditorView::delete_forward() {
    const u32 length = buffer_.line_length(cursor_.line);
    if (cursor_.col < length) {
        delete_range(Range{cursor_, Pos{cursor_.line, cursor_.col + 1}}, EditKind::DeleteForward);
        return;
    }
    if (cursor_.line >= buffer_.line_count()) return;
    delete_range(Range{cursor_, Pos{cursor_.line + 1, 0}}, EditKind::Structural);
}

void EditorView::move_cursor(Pos target, bool keep_goal_column) {
    const u32 wanted = keep_goal_column ? goal_column_ : target.col;
    cursor_ = buffer_.clamp(Pos{target.line, wanted});
    if (!keep_goal_column) goal_column_ = cursor_.col;
    undo_.break_group();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool EditorView::on_key(const Key& key) {
    const auto lines = static_cast<u32>(buffer_.line_count());
    const int page = std::max(1, last_height_ - 1);

    switch (key.code) {
        case KeyCode::Left:
            if (cursor_.col > 0) {
                move_cursor(Pos{cursor_.line, cursor_.col - 1});
            } else if (cursor_.line > 1) {
                move_cursor(buffer_.end_of_line(cursor_.line - 1));
            }
            return true;
        case KeyCode::Right:
            if (cursor_.col < buffer_.line_length(cursor_.line)) {
                move_cursor(Pos{cursor_.line, cursor_.col + 1});
            } else if (cursor_.line < lines) {
                move_cursor(Pos{cursor_.line + 1, 0});
            }
            return true;
        case KeyCode::Up:
            if (cursor_.line > 1) move_cursor(Pos{cursor_.line - 1, 0}, true);
            return true;
        case KeyCode::Down:
            if (cursor_.line < lines) move_cursor(Pos{cursor_.line + 1, 0}, true);
            return true;
        case KeyCode::PageUp:
            move_cursor(Pos{cursor_.line > static_cast<u32>(page) ? cursor_.line - static_cast<u32>(page) : 1, 0},
                        true);
            return true;
        case KeyCode::PageDown:
            move_cursor(Pos{std::min<u32>(cursor_.line + static_cast<u32>(page), lines), 0}, true);
            return true;
        case KeyCode::Home: move_cursor(Pos{cursor_.line, 0}); return true;
        case KeyCode::End: move_cursor(buffer_.end_of_line(cursor_.line)); return true;
        case KeyCode::Enter: insert_newline_with_indent(); return true;
        case KeyCode::Backspace: backspace(); return true;
        case KeyCode::Delete: delete_forward(); return true;
        case KeyCode::Tab:
            insert_text(std::string(kIndentWidth, ' '), EditKind::Structural);
            return true;
        default: break;
    }

    if (key.ctrl && key.code == KeyCode::Char) {
        switch (key.ch | 0x20u) {
            case 'z':
                if (undo_.undo(buffer_, cursor_)) {
                    modified_ = true;
                    goal_column_ = cursor_.col;
                }
                return true;
            case 'y':
                if (undo_.redo(buffer_, cursor_)) {
                    modified_ = true;
                    goal_column_ = cursor_.col;
                }
                return true;
            case 'a': move_cursor(Pos{cursor_.line, 0}); return true;
            case 'e': move_cursor(buffer_.end_of_line(cursor_.line)); return true;
            case 'k':
                // Kill to end of line, or join the next line if already there.
                if (cursor_.col < buffer_.line_length(cursor_.line)) {
                    delete_range(Range{cursor_, buffer_.end_of_line(cursor_.line)},
                                 EditKind::Structural);
                } else {
                    delete_forward();
                }
                return true;
            default: return false;  // let the app handle ^S and the rest
        }
    }

    if (key.code == KeyCode::Char && !key.alt && key.ch >= 0x20 && key.ch < 0x7f) {
        insert_text(std::string(1, static_cast<char>(key.ch)), EditKind::InsertChar);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void EditorView::ensure_visible(int height, int text_width) {
    const auto lines = static_cast<u32>(buffer_.line_count());
    const int margin = std::min(kScrollOff, std::max(0, (height - 1) / 2));

    if (cursor_.line < top_ + static_cast<u32>(margin)) {
        top_ = cursor_.line > static_cast<u32>(margin) ? cursor_.line - static_cast<u32>(margin) : 1;
    }
    const u32 bottom = top_ + static_cast<u32>(height) - 1;
    if (cursor_.line + static_cast<u32>(margin) > bottom) {
        const u32 wanted = cursor_.line + static_cast<u32>(margin);
        top_ = wanted > static_cast<u32>(height) ? wanted - static_cast<u32>(height) + 1 : 1;
    }
    const u32 max_top =
        lines > static_cast<u32>(height) ? lines - static_cast<u32>(height) + 1 : 1;
    top_ = std::clamp<u32>(top_, 1, max_top);

    // Horizontal scrolling matters at 75 usable columns: assembly comments run
    // long, and a caret that walks off the right edge is unusable.
    if (text_width > 0) {
        if (cursor_.col < left_) left_ = cursor_.col;
        const auto right = static_cast<u32>(text_width);
        if (cursor_.col >= left_ + right) left_ = cursor_.col - right + 1;
    }
}

void EditorView::draw(Pane pane) {
    if (pane.height() <= 0 || pane.width() <= 0) return;

    const int gutter = gutter_width();
    const int text_width = std::max(0, pane.width() - gutter);
    last_height_ = pane.height();
    last_text_width_ = text_width;
    ensure_visible(pane.height(), text_width);

    const auto lines = static_cast<u32>(buffer_.line_count());
    const auto executing = session_->current_line();

    for (int row = 0; row < pane.height(); ++row) {
        const u32 line = top_ + static_cast<u32>(row);
        if (line > lines) break;

        const bool is_cursor_line = line == cursor_.line;
        const bool has_breakpoint = session_->breakpoints().has_breakpoint_on_line(line);
        const bool is_executing = executing.has_value() && *executing == line;
        const std::vector<const as::Diagnostic*> diags = diagnostics_.for_line(line);

        if (is_cursor_line) pane.fill_row(row, StyleId::CurrentLine);

        int column = 0;
        // Marker precedence: a breakpoint is a decision the user made, so it
        // outranks a diagnostic the tool inferred.
        if (has_breakpoint) {
            pane.put_at(column, row, "●", StyleId::Breakpoint);
        } else if (!diags.empty()) {
            const as::Severity worst = diags.front()->severity;
            pane.put_at(column, row, worst == as::Severity::Error ? "E" : "W",
                        style_for_severity(worst));
        } else {
            pane.put_at(column, row, " ");
        }
        pane.put_at(column, row, is_executing ? "▶" : " ", StyleId::Arrow);

        char number[16];
        std::snprintf(number, sizeof number, "%*u", gutter - 3, line);
        pane.put_at(column, row, number, is_cursor_line ? StyleId::Normal : StyleId::LineNumber);
        pane.put_at(column, row, "│", StyleId::Frame);

        const std::string_view text = buffer_.line(line);
        if (text_width <= 0) continue;
        const std::string_view visible =
            left_ < text.size() ? text.substr(left_, static_cast<std::size_t>(text_width))
                                : std::string_view{};
        if (!visible.empty()) pane.put(gutter, row, visible);

        for (const HighlightSpan& span : highlight_line(text, line)) {
            if (span.begin >= text.size() || span.end <= left_) continue;
            const u32 begin = std::max<u32>(span.begin, left_);
            const auto end = static_cast<u32>(std::min<std::size_t>(span.end, text.size()));
            if (end <= begin) continue;
            pane.put(gutter + static_cast<int>(begin - left_), row,
                     text.substr(begin, end - begin), span.style);
        }

        // Mark the exact span each diagnostic points at, by restyling the text
        // rather than covering it. A row of carets underneath would cost a
        // line per diagnostic, and carets drawn *over* the text would hide the
        // very word the message is about.
        for (const as::Diagnostic* diagnostic : diags) {
            const as::SourceSpan& span = diagnostic->primary;
            if (span.end_col() <= left_ || span.col >= text.size()) continue;
            const u32 begin = std::max<u32>(span.col, left_);
            const auto end = static_cast<u32>(
                std::min<std::size_t>(std::max<u32>(span.end_col(), span.col + 1), text.size()));
            if (end <= begin) continue;
            pane.put(gutter + static_cast<int>(begin - left_), row,
                     text.substr(begin, end - begin),
                     span_style_for_severity(diagnostic->severity));
        }
    }

    if (focused) {
        const int cursor_row = static_cast<int>(cursor_.line - top_);
        const int cursor_column = gutter + static_cast<int>(cursor_.col - left_);
        if (cursor_row >= 0 && cursor_row < pane.height()) {
            pane.set_cursor(cursor_column, cursor_row);
        }
    }
}

}  // namespace rv::tui
