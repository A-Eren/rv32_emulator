// A canvas that draws into memory.
//
// Every view can therefore be rendered and asserted on with no terminal:
//
//     StringCanvas canvas(80, 9);
//     RegsView(session).draw(Pane(canvas, canvas.bounds()));
//     RV_CHECK_STR(canvas.line(0), " zero 00000000  ra   00000010 ...");
//
// It also backs `--render-once`, which dumps a single frame to stdout. Both
// matter more than they sound: debugging a curses application is painful
// precisely because stdout belongs to the UI.
#pragma once

#include <string>
#include <vector>

#include "tui/canvas.hpp"

namespace rv::tui {

class StringCanvas final : public Canvas {
public:
    StringCanvas(int width, int height);

    Size size() const override { return Size{width_, height_}; }
    Rect bounds() const { return Rect{0, 0, width_, height_}; }

    int put(int x, int y, std::string_view text, StyleId style) override;
    void fill(Rect area, std::string_view glyph, StyleId style) override;
    void set_cursor(int x, int y, bool visible) override;

    /// One row of text, with trailing spaces removed so goldens stay readable.
    std::string line(int y) const;
    /// The whole frame, newline separated.
    std::string to_string() const;

    /// The style of a single cell, for asserting that the changed register is
    /// actually highlighted rather than merely present.
    StyleId style_at(int x, int y) const;
    /// True if any cell in the row carries this style.
    bool row_has_style(int y, StyleId style) const;

    int cursor_x() const { return cursor_x_; }
    int cursor_y() const { return cursor_y_; }
    bool cursor_visible() const { return cursor_visible_; }

    void reset();

private:
    struct Cell {
        // A single code point of UTF-8, NUL padded. Four bytes covers
        // everything this UI draws.
        char text[4] = {' ', 0, 0, 0};
        u8 length = 1;
        StyleId style = StyleId::Normal;
    };

    Cell& at(int x, int y) { return cells_[static_cast<std::size_t>(y) * width_ + x]; }
    const Cell& at(int x, int y) const {
        return cells_[static_cast<std::size_t>(y) * width_ + x];
    }

    int width_;
    int height_;
    std::vector<Cell> cells_;
    int cursor_x_ = 0;
    int cursor_y_ = 0;
    bool cursor_visible_ = false;
};

}  // namespace rv::tui
