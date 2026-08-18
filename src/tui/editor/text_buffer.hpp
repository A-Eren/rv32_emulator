// The editor's text storage.
//
// A vector of lines, not a gap buffer. Every consumer of this text is
// line-indexed -- the gutter, breakpoint marks, error markers, the execution
// arrow, the source map, the highlighter and the assembler -- so a gap buffer
// would mean maintaining a separate line-offset index and invalidating it on
// every keystroke. That is paying for the one thing we constantly need.
//
// The size argument settles it: instruction memory holds 4096 instructions, so
// a source file here is realistically under 2000 lines. A gap buffer's
// asymptotic advantage never materialises, while an O(line length) memmove on a
// forty-character line is free. The honest cost is that deleting 500 lines is
// an O(n) vector shift -- microseconds at this scale.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "isa/types.hpp"

namespace rv::tui {

/// A caret position. Lines are 1-based, matching what the gutter shows and what
/// diagnostics report; columns are 0-based byte offsets within the line.
struct Pos {
    u32 line = 1;
    u32 col = 0;

    bool operator==(const Pos& other) const { return line == other.line && col == other.col; }
    bool operator!=(const Pos& other) const { return !(*this == other); }
    bool operator<(const Pos& other) const {
        return line != other.line ? line < other.line : col < other.col;
    }
    bool operator<=(const Pos& other) const { return *this < other || *this == other; }
};

/// A half-open span of text, always normalised so that `begin <= end`.
struct Range {
    Pos begin;
    Pos end;

    bool empty() const { return begin == end; }
    static Range normalised(Pos a, Pos b) { return a <= b ? Range{a, b} : Range{b, a}; }
};

class TextBuffer {
public:
    TextBuffer() : lines_{""} {}
    explicit TextBuffer(std::string_view text) { set_text(text); }

    std::size_t line_count() const { return lines_.size(); }
    /// 1-based. Out-of-range lines read as empty rather than faulting, so a
    /// viewport can scroll past the end without special cases.
    std::string_view line(u32 number) const;
    u32 line_length(u32 number) const;

    /// Insert text at `at`, handling embedded newlines. Returns the position
    /// just past what was inserted.
    Pos insert(Pos at, std::string_view text);

    /// Remove a range and return what was there, so undo can put it back.
    std::string erase(Range range);

    std::string text_in(Range range) const;

    std::string to_string() const;
    void set_text(std::string_view text);

    /// Bumped on every mutation. Drives re-highlighting, the modified flag and
    /// assemble-on-idle, none of which need to know *what* changed.
    u64 revision() const { return revision_; }

    Pos clamp(Pos position) const;
    Pos end_of_line(u32 number) const;
    Pos end() const;

private:
    std::vector<std::string> lines_;
    u64 revision_ = 0;
};

}  // namespace rv::tui
