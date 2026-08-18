#include "tui/string_canvas.hpp"

#include <algorithm>
#include <cstring>

namespace rv::tui {

std::size_t utf8_length(std::string_view text, std::size_t position) {
    if (position >= text.size()) return 0;
    const auto lead = static_cast<unsigned char>(text[position]);
    std::size_t length = 1;
    if ((lead & 0xe0u) == 0xc0u) length = 2;
    else if ((lead & 0xf0u) == 0xe0u) length = 3;
    else if ((lead & 0xf8u) == 0xf0u) length = 4;
    // Truncated input advances by one byte, so a decoder can never get stuck.
    return std::min(length, text.size() - position);
}

int display_width(std::string_view text) {
    int columns = 0;
    for (std::size_t position = 0; position < text.size();) {
        position += utf8_length(text, position);
        ++columns;
    }
    return columns;
}

StringCanvas::StringCanvas(int width, int height)
    : width_(std::max(0, width)),
      height_(std::max(0, height)),
      cells_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)) {}

int StringCanvas::put(int x, int y, std::string_view text, StyleId style) {
    if (y < 0 || y >= height_) return 0;
    int column = x;
    int drawn = 0;

    for (std::size_t position = 0; position < text.size();) {
        const std::size_t length = utf8_length(text, position);
        if (column >= width_) break;
        if (column >= 0) {
            Cell& cell = at(column, y);
            cell.length = static_cast<u8>(std::min<std::size_t>(length, 4));
            std::memset(cell.text, 0, sizeof cell.text);
            std::memcpy(cell.text, text.data() + position, cell.length);
            cell.style = style;
            ++drawn;
        }
        position += length;
        ++column;
    }
    return drawn;
}

void StringCanvas::fill(Rect area, std::string_view glyph, StyleId style) {
    const Rect clipped = area.intersect(bounds());
    const std::string_view text = glyph.empty() ? std::string_view(" ") : glyph;
    const std::size_t length = std::min<std::size_t>(utf8_length(text, 0), 4);

    for (int y = clipped.y; y < clipped.bottom(); ++y) {
        for (int x = clipped.x; x < clipped.right(); ++x) {
            Cell& cell = at(x, y);
            // A fill with no glyph repaints the background without erasing the
            // text already there -- that is how the current-line highlight
            // works without having to draw the line twice.
            if (!glyph.empty()) {
                cell.length = static_cast<u8>(length);
                std::memset(cell.text, 0, sizeof cell.text);
                std::memcpy(cell.text, text.data(), length);
            }
            cell.style = style;
        }
    }
}

void StringCanvas::set_cursor(int x, int y, bool visible) {
    cursor_x_ = x;
    cursor_y_ = y;
    cursor_visible_ = visible;
}

std::string StringCanvas::line(int y) const {
    if (y < 0 || y >= height_) return {};
    std::string out;
    for (int x = 0; x < width_; ++x) {
        const Cell& cell = at(x, y);
        out.append(cell.text, cell.length);
    }
    // Trailing spaces make goldens miserable to read and to write.
    const std::size_t last = out.find_last_not_of(' ');
    if (last == std::string::npos) return {};
    out.resize(last + 1);
    return out;
}

std::string StringCanvas::to_string() const {
    std::string out;
    for (int y = 0; y < height_; ++y) {
        out += line(y);
        out.push_back('\n');
    }
    return out;
}

StyleId StringCanvas::style_at(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return StyleId::Normal;
    return at(x, y).style;
}

bool StringCanvas::row_has_style(int y, StyleId style) const {
    if (y < 0 || y >= height_) return false;
    for (int x = 0; x < width_; ++x) {
        if (at(x, y).style == style) return true;
    }
    return false;
}

void StringCanvas::reset() {
    std::fill(cells_.begin(), cells_.end(), Cell{});
    cursor_visible_ = false;
}

void Pane::horizontal_rule(int y, StyleId style) {
    fill(Rect{0, y, width(), 1}, "─", style);
}

}  // namespace rv::tui
