#include "tui/editor/undo.hpp"

#include <string_view>

namespace rv::tui {
namespace {

/// Where an edit leaves the text, for deciding whether the next one is adjacent.
Pos end_of(const EditRecord& record) { return record.cursor_after; }

/// The position `text` reaches when written starting at `from`. Recomputed
/// rather than stored, because a later edit can shift a stored position.
///
/// Not named `advance`: a std::string argument drags namespace std in through
/// ADL, and std::advance then joins the overload set.
Pos position_after(Pos from, std::string_view text) {
    Pos out = from;
    for (const char character : text) {
        if (character == '\n') {
            ++out.line;
            out.col = 0;
        } else {
            ++out.col;
        }
    }
    return out;
}

bool joins_previous(EditKind kind, EditKind last_kind, Pos last_end, const EditRecord& next) {
    if (kind != last_kind) return false;
    if (kind == EditKind::Structural) return false;
    if (next.cursor_before.line != last_end.line) return false;

    switch (kind) {
        case EditKind::InsertChar:
            // Typing continues where the last character landed.
            return next.cursor_before.col == last_end.col;
        case EditKind::DeleteBack:
            // Repeated backspace walks left, so each deletion starts where the
            // previous one ended.
            return next.cursor_before.col == last_end.col;
        case EditKind::DeleteForward:
            // Delete leaves the caret still, so the position repeats.
            return next.cursor_before.col == last_end.col;
        case EditKind::Structural:
            return false;
    }
    return false;
}

}  // namespace

void UndoStack::record(EditRecord record, EditKind kind) {
    // Any new edit invalidates the redo branch; keeping it would let redo
    // replay something that no longer makes sense.
    undone_.clear();

    if (group_open_ && joins_previous(kind, last_kind_, last_end_, record)) {
        record.group = next_group_ - 1;
    } else {
        record.group = next_group_++;
        group_open_ = true;
    }

    last_kind_ = kind;
    last_end_ = end_of(record);
    done_.push_back(std::move(record));
}

bool UndoStack::undo(TextBuffer& buffer, Pos& cursor) {
    if (done_.empty()) return false;
    const u32 group = done_.back().group;

    while (!done_.empty() && done_.back().group == group) {
        EditRecord record = done_.back();
        done_.pop_back();

        // Replace what was inserted with what was removed.
        if (!record.inserted.empty()) {
            buffer.erase(Range{record.at, position_after(record.at, record.inserted)});
        }
        if (!record.removed.empty()) buffer.insert(record.at, record.removed);
        cursor = buffer.clamp(record.cursor_before);
        undone_.push_back(std::move(record));
    }

    group_open_ = false;
    return true;
}

bool UndoStack::redo(TextBuffer& buffer, Pos& cursor) {
    if (undone_.empty()) return false;
    const u32 group = undone_.back().group;

    while (!undone_.empty() && undone_.back().group == group) {
        EditRecord record = undone_.back();
        undone_.pop_back();

        if (!record.removed.empty()) {
            buffer.erase(Range{record.at, position_after(record.at, record.removed)});
        }
        if (!record.inserted.empty()) buffer.insert(record.at, record.inserted);
        cursor = buffer.clamp(record.cursor_after);
        done_.push_back(std::move(record));
    }

    group_open_ = false;
    return true;
}

void UndoStack::clear() {
    done_.clear();
    undone_.clear();
    group_open_ = false;
    next_group_ = 1;
}

}  // namespace rv::tui
