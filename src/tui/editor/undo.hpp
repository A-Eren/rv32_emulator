// Undo and redo.
//
// Command records rather than snapshots: each entry knows the range it touched,
// what was there, and what replaced it, so undoing is applying the inverse.
//
// Consecutive typing coalesces into one group -- otherwise undo would step back
// one character at a time, which is unusable. Grouping is decided by adjacency
// and operation kind rather than by a clock, which keeps it deterministic and
// therefore testable; a wall-clock timeout would make the same keystrokes
// produce different histories on a slow machine.
//
// This is not optional. An editor without undo loses people's work.
#pragma once

#include <string>
#include <vector>

#include "tui/editor/text_buffer.hpp"

namespace rv::tui {

struct EditRecord {
    /// Where the edit began. The removed text was there; the inserted text is
    /// there now.
    Pos at;
    std::string removed;
    std::string inserted;
    Pos cursor_before;
    Pos cursor_after;
    /// Edits sharing a group id undo together.
    u32 group = 0;
};

/// What kind of edit this was, which decides whether it can join the previous
/// group.
enum class EditKind : u8 {
    InsertChar,   // one printable character
    DeleteBack,   // backspace
    DeleteForward,
    /// Anything structural: newline, paste, delete a line, indent a block.
    /// Always starts a fresh group, so undo lands on a boundary a person
    /// recognises.
    Structural,
};

class UndoStack {
public:
    /// Record an edit that has already been applied to the buffer.
    void record(EditRecord record, EditKind kind);

    bool can_undo() const { return !done_.empty(); }
    bool can_redo() const { return !undone_.empty(); }

    /// Undo one group. Returns false if there is nothing to undo; on success
    /// `cursor` is moved to where the edit started.
    bool undo(TextBuffer& buffer, Pos& cursor);
    bool redo(TextBuffer& buffer, Pos& cursor);

    /// Force the next edit to start a new group -- after a cursor jump, a save,
    /// or a mode change, where continuing to coalesce would surprise.
    void break_group() { group_open_ = false; }

    void clear();

    std::size_t depth() const { return done_.size(); }

private:
    std::vector<EditRecord> done_;
    std::vector<EditRecord> undone_;
    u32 next_group_ = 1;
    bool group_open_ = false;
    EditKind last_kind_ = EditKind::Structural;
    Pos last_end_{};
};

}  // namespace rv::tui
