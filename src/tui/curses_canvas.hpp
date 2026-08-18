// A Canvas backed by the real terminal.
//
// The header deliberately does not include <curses.h> -- see canvas.hpp for
// why. Nothing outside curses_canvas.cpp needs to know what a WINDOW is.
#pragma once

#include "tui/canvas.hpp"

namespace rv::tui {

class CursesCanvas final : public Canvas {
public:
    CursesCanvas();

    Size size() const override;
    int put(int x, int y, std::string_view text, StyleId style) override;
    void fill(Rect area, std::string_view glyph, StyleId style) override;
    void set_cursor(int x, int y, bool visible) override;

    /// Blank the back buffer before drawing a frame.
    void erase_all();
    /// Push the frame to the terminal and place the hardware cursor.
    void present();

private:
    int cursor_x_ = 0;
    int cursor_y_ = 0;
    bool cursor_visible_ = false;
};

/// Allocate the colour pairs the theme needs. Called once, after the terminal
/// is initialised.
void init_styles();

}  // namespace rv::tui
