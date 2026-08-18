// The drawing surface.
//
// This is the seam that keeps the TUI honest. Every view draws through a
// Canvas and none of them include <curses.h>; only term.cpp and
// curses_canvas.cpp do. Two reasons, both practical:
//
//   1. A view's draw() becomes a pure function from state to a grid of cells,
//      so it can be rendered into a StringCanvas and compared against a golden
//      frame with no terminal involved.
//   2. <curses.h> #defines ordinary words -- instr, move, clear, line,
//      timeout, box, border, scroll. Letting that leak into a codebase built
//      around DecodedInstr would produce unreadable errors. Hence also the
//      method names here: put, fill, present -- never clear, move or refresh.
#pragma once

#include <string>
#include <string_view>

#include "tui/geometry.hpp"
#include "tui/theme.hpp"

namespace rv::tui {

/// Byte length of the UTF-8 sequence beginning at `text[position]`. Malformed
/// input advances by one byte so a decoder can never get stuck.
std::size_t utf8_length(std::string_view text, std::size_t position);

/// Number of columns `text` occupies, treating every code point as one column.
/// True enough here: the UI is ASCII plus box-drawing and arrows, and this
/// project explicitly does not support wide-character editing.
int display_width(std::string_view text);

class Canvas {
public:
    virtual ~Canvas() = default;

    virtual Size size() const = 0;

    /// Draw UTF-8 text starting at (x, y), clipped to the canvas. Returns the
    /// number of columns drawn.
    virtual int put(int x, int y, std::string_view text, StyleId style) = 0;

    /// Repeat `glyph` over `area`. An empty glyph means a space.
    virtual void fill(Rect area, std::string_view glyph, StyleId style) = 0;

    /// Where the hardware cursor should sit after the frame is drawn. Only the
    /// editor sets this; everything else leaves it hidden.
    virtual void set_cursor(int x, int y, bool visible) = 0;
};

/// A clipped, offset view onto a Canvas.
///
/// Views receive a Pane rather than a Canvas and draw in their own coordinates
/// starting at (0, 0). They never learn where on screen they are, which is what
/// lets the layout engine move them around freely.
class Pane {
public:
    Pane(Canvas& canvas, Rect area) : canvas_(&canvas), area_(area) {}

    Size size() const { return area_.size(); }
    int width() const { return area_.width; }
    int height() const { return area_.height; }

    int put(int x, int y, std::string_view text, StyleId style = StyleId::Normal) {
        if (y < 0 || y >= area_.height || x >= area_.width) return 0;
        // Clip on the left by dropping leading code points rather than bytes.
        while (x < 0 && !text.empty()) {
            const std::size_t length = utf8_length(text, 0);
            text.remove_prefix(length);
            ++x;
        }
        if (text.empty() || x < 0) return 0;
        return canvas_->put(area_.x + x, area_.y + y, text, style);
    }

    /// Draw text and return the column just past it, so callers can chain
    /// runs of differently styled text without tracking widths by hand.
    int put_at(int& x, int y, std::string_view text, StyleId style = StyleId::Normal) {
        const int drawn = put(x, y, text, style);
        x += display_width(text);
        return drawn;
    }

    void fill(Rect area, std::string_view glyph = {}, StyleId style = StyleId::Normal) {
        canvas_->fill(Rect{area_.x + area.x, area_.y + area.y, area.width, area.height}
                          .intersect(area_),
                      glyph, style);
    }

    void fill_all(StyleId style = StyleId::Normal) {
        canvas_->fill(area_, {}, style);
    }

    /// Fill one row, for highlighting the current line.
    void fill_row(int y, StyleId style) {
        if (y < 0 || y >= area_.height) return;
        canvas_->fill(Rect{area_.x, area_.y + y, area_.width, 1}, {}, style);
    }

    void horizontal_rule(int y, StyleId style = StyleId::Frame);

    Pane sub(Rect relative) const {
        const Rect absolute{area_.x + relative.x, area_.y + relative.y, relative.width,
                            relative.height};
        return Pane(*canvas_, absolute.intersect(area_));
    }

    void set_cursor(int x, int y, bool visible = true) {
        canvas_->set_cursor(area_.x + x, area_.y + y, visible);
    }

private:
    Canvas* canvas_;
    Rect area_;
};

}  // namespace rv::tui
