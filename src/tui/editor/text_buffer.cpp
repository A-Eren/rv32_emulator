#include "tui/editor/text_buffer.hpp"

#include <algorithm>

namespace rv::tui {
namespace {

/// Index into `lines_` for a 1-based line number, or the size if out of range.
std::size_t index_of(u32 line) { return line == 0 ? 0 : static_cast<std::size_t>(line - 1); }

}  // namespace

std::string_view TextBuffer::line(u32 number) const {
    if (number == 0 || number > lines_.size()) return {};
    return lines_[index_of(number)];
}

u32 TextBuffer::line_length(u32 number) const {
    return static_cast<u32>(line(number).size());
}

Pos TextBuffer::clamp(Pos position) const {
    Pos out = position;
    if (out.line < 1) out.line = 1;
    if (out.line > lines_.size()) out.line = static_cast<u32>(lines_.size());
    out.col = std::min(out.col, line_length(out.line));
    return out;
}

Pos TextBuffer::end_of_line(u32 number) const { return Pos{number, line_length(number)}; }

Pos TextBuffer::end() const {
    const auto last = static_cast<u32>(lines_.size());
    return Pos{last, line_length(last)};
}

void TextBuffer::set_text(std::string_view text) {
    lines_.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos) {
            lines_.emplace_back(text.substr(start));
            break;
        }
        std::string row(text.substr(start, newline - start));
        // Tolerate CRLF input rather than showing a stray glyph at every EOL.
        if (!row.empty() && row.back() == '\r') row.pop_back();
        lines_.push_back(std::move(row));
        start = newline + 1;
    }
    // A file ending in a newline does not have an extra empty line after it,
    // but an empty buffer still needs one line to put the cursor on.
    if (lines_.size() > 1 && lines_.back().empty()) lines_.pop_back();
    if (lines_.empty()) lines_.emplace_back();
    ++revision_;
}

std::string TextBuffer::to_string() const {
    std::string out;
    for (const std::string& row : lines_) {
        out += row;
        out.push_back('\n');
    }
    return out;
}

Pos TextBuffer::insert(Pos at, std::string_view text) {
    const Pos start = clamp(at);
    if (text.empty()) return start;
    ++revision_;

    std::string& first = lines_[index_of(start.line)];
    const std::string tail = first.substr(start.col);
    first.erase(start.col);

    std::size_t position = 0;
    Pos cursor = start;
    std::vector<std::string> inserted_lines;

    while (true) {
        const std::size_t newline = text.find('\n', position);
        const std::string_view piece = text.substr(
            position, newline == std::string_view::npos ? std::string_view::npos : newline - position);

        if (inserted_lines.empty()) {
            lines_[index_of(start.line)] += piece;
            cursor.col = static_cast<u32>(lines_[index_of(start.line)].size());
        } else {
            inserted_lines.back() += piece;
            cursor.col = static_cast<u32>(inserted_lines.back().size());
        }

        if (newline == std::string_view::npos) break;
        inserted_lines.emplace_back();
        position = newline + 1;
        ++cursor.line;
        cursor.col = 0;
    }

    if (!inserted_lines.empty()) {
        inserted_lines.back() += tail;
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(index_of(start.line)) + 1,
                      inserted_lines.begin(), inserted_lines.end());
    } else {
        lines_[index_of(start.line)] += tail;
    }
    return cursor;
}

std::string TextBuffer::text_in(Range range) const {
    const Pos begin = clamp(range.begin);
    const Pos end = clamp(range.end);
    if (end <= begin) return {};

    if (begin.line == end.line) {
        return std::string(line(begin.line).substr(begin.col, end.col - begin.col));
    }

    std::string out(line(begin.line).substr(begin.col));
    out.push_back('\n');
    for (u32 number = begin.line + 1; number < end.line; ++number) {
        out += line(number);
        out.push_back('\n');
    }
    out += line(end.line).substr(0, end.col);
    return out;
}

std::string TextBuffer::erase(Range range) {
    const Pos begin = clamp(range.begin);
    const Pos end = clamp(range.end);
    if (end <= begin) return {};

    std::string removed = text_in(Range{begin, end});
    ++revision_;

    std::string head(line(begin.line).substr(0, begin.col));
    const std::string tail(line(end.line).substr(end.col));

    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index_of(begin.line)),
                 lines_.begin() + static_cast<std::ptrdiff_t>(index_of(end.line)) + 1);
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(index_of(begin.line)), head + tail);

    if (lines_.empty()) lines_.emplace_back();
    return removed;
}

}  // namespace rv::tui
