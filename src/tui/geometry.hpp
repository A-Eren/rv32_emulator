// Rectangles and the arithmetic for splitting them.
//
// Pure value types with no dependencies, so the layout engine can be tested
// without a terminal anywhere in sight.
#pragma once

#include <algorithm>

#include "isa/types.hpp"

namespace rv::tui {

struct Size {
    int width = 0;
    int height = 0;

    bool operator==(const Size& other) const {
        return width == other.width && height == other.height;
    }
    bool operator!=(const Size& other) const { return !(*this == other); }
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }
    bool empty() const { return width <= 0 || height <= 0; }
    Size size() const { return Size{width, height}; }

    bool contains(int px, int py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }

    /// Shrink by `amount` on every side, clamped at empty.
    Rect inset(int amount) const {
        return Rect{x + amount, y + amount, std::max(0, width - 2 * amount),
                    std::max(0, height - 2 * amount)};
    }

    /// The first `rows` rows, and everything below them.
    Rect take_top(int rows) const { return Rect{x, y, width, std::min(rows, height)}; }
    Rect drop_top(int rows) const {
        const int taken = std::min(rows, height);
        return Rect{x, y + taken, width, height - taken};
    }
    Rect take_bottom(int rows) const {
        const int taken = std::min(rows, height);
        return Rect{x, bottom() - taken, width, taken};
    }
    Rect drop_bottom(int rows) const {
        const int taken = std::min(rows, height);
        return Rect{x, y, width, height - taken};
    }
    Rect take_left(int columns) const { return Rect{x, y, std::min(columns, width), height}; }
    Rect drop_left(int columns) const {
        const int taken = std::min(columns, width);
        return Rect{x + taken, y, width - taken, height};
    }

    /// The single row at offset `row` within this rectangle.
    Rect row(int index) const { return Rect{x, y + index, width, 1}; }

    Rect intersect(const Rect& other) const {
        const int left = std::max(x, other.x);
        const int top = std::max(y, other.y);
        const int right_edge = std::min(right(), other.right());
        const int bottom_edge = std::min(bottom(), other.bottom());
        return Rect{left, top, std::max(0, right_edge - left), std::max(0, bottom_edge - top)};
    }

    bool operator==(const Rect& other) const {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
    bool operator!=(const Rect& other) const { return !(*this == other); }
};

}  // namespace rv::tui
