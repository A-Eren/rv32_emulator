// ncurses translation unit 2 of 2.
//
// Everything that touches <curses.h> lives here or in term.cpp. Note that
// curses #defines `move`, `clear`, `erase`, `refresh`, `line`, `instr`,
// `timeout`, `box`, `border` and `scroll`, so nothing in this file may use
// those as identifiers, and no project header may be included after it in a
// way that would expose them.
#include "tui/curses_canvas.hpp"

#include <curses.h>

#include <algorithm>
#include <string>

namespace rv::tui {
namespace {

/// Colour pair number for a style. Pair 0 is reserved by curses for the
/// terminal default, so styles start at 1.
short pair_for(StyleId id) { return static_cast<short>(static_cast<int>(id) + 1); }

/// The palette is expressed in 256-colour indices. Rather than inventing a
/// lossy mapping down to the basic eight, a terminal that cannot do 256 colours
/// gets the attribute-only path below, which carries the same information
/// through bold, dim and reverse.
bool colors_available() {
    static const bool available = has_colors() && COLORS >= 256;
    return available;
}

attr_t attributes_for(StyleId id) {
    const Style& style = style_for(id);
    attr_t attributes = A_NORMAL;
    if (has(style.attr, Attr::Bold)) attributes |= A_BOLD;
    if (has(style.attr, Attr::Underline)) attributes |= A_UNDERLINE;
    if (has(style.attr, Attr::Reverse)) attributes |= A_REVERSE;
    if (has(style.attr, Attr::Dim)) attributes |= A_DIM;

    if (colors_available()) {
        attributes |= COLOR_PAIR(pair_for(id));
    } else {
        // With no colour, roles that carried meaning through hue need another
        // channel or they become invisible.
        if (id == StyleId::ChangedValue || id == StyleId::Arrow || id == StyleId::Error) {
            attributes |= A_BOLD;
        }
        if (id == StyleId::CurrentLine) attributes |= A_REVERSE;
        if (id == StyleId::Dim || id == StyleId::ZeroValue || id == StyleId::Comment) {
            attributes |= A_DIM;
        }
    }
    return attributes;
}

}  // namespace

void init_styles() {
    if (!colors_available()) return;
    start_color();
    use_default_colors();
    for (int index = 0; index < static_cast<int>(StyleId::Count); ++index) {
        const auto id = static_cast<StyleId>(index);
        const Style& style = style_for(id);
        init_pair(pair_for(id), style.fg, style.bg);
    }
}

CursesCanvas::CursesCanvas() = default;

Size CursesCanvas::size() const {
    int rows = 0;
    int columns = 0;
    getmaxyx(stdscr, rows, columns);
    return Size{columns, rows};
}

int CursesCanvas::put(int x, int y, std::string_view text, StyleId style) {
    const Size extent = size();
    if (y < 0 || y >= extent.height || x >= extent.width) return 0;

    // Never write the bottom-right cell: on many terminals that triggers a
    // scroll, which shifts the whole frame by a row.
    const int limit = (y == extent.height - 1) ? extent.width - 1 : extent.width;

    const attr_t attributes = attributes_for(style);
    wattr_on(stdscr, attributes, nullptr);

    int column = x;
    int drawn = 0;
    for (std::size_t position = 0; position < text.size();) {
        const std::size_t length = utf8_length(text, position);
        if (column >= limit) break;
        if (column >= 0) {
            mvaddnstr(y, column, text.data() + position, static_cast<int>(length));
            ++drawn;
        }
        position += length;
        ++column;
    }

    wattr_off(stdscr, attributes, nullptr);
    return drawn;
}

void CursesCanvas::fill(Rect area, std::string_view glyph, StyleId style) {
    const Size extent = size();
    const Rect clipped = area.intersect(Rect{0, 0, extent.width, extent.height});
    if (clipped.empty()) return;

    const attr_t attributes = attributes_for(style);
    wattr_on(stdscr, attributes, nullptr);

    if (glyph.empty()) {
        // Repaint the background without erasing what is already there. This
        // is how the current-line highlight works without redrawing the text.
        for (int y = clipped.y; y < clipped.bottom(); ++y) {
            const int limit = (y == extent.height - 1) ? std::min(clipped.right(), extent.width - 1)
                                                       : clipped.right();
            for (int x = clipped.x; x < limit; ++x) {
                const chtype existing = mvinch(y, x) & A_CHARTEXT;
                mvaddch(y, x, existing | attributes);
            }
        }
    } else {
        const std::size_t length = utf8_length(glyph, 0);
        const std::string cell(glyph.data(), length);
        for (int y = clipped.y; y < clipped.bottom(); ++y) {
            const int limit = (y == extent.height - 1) ? std::min(clipped.right(), extent.width - 1)
                                                       : clipped.right();
            for (int x = clipped.x; x < limit; ++x) {
                mvaddnstr(y, x, cell.c_str(), static_cast<int>(length));
            }
        }
    }

    wattr_off(stdscr, attributes, nullptr);
}

void CursesCanvas::set_cursor(int x, int y, bool visible) {
    cursor_x_ = x;
    cursor_y_ = y;
    cursor_visible_ = visible;
}

void CursesCanvas::erase_all() {
    werase(stdscr);
    cursor_visible_ = false;
}

void CursesCanvas::present() {
    curs_set(cursor_visible_ ? 1 : 0);
    if (cursor_visible_) wmove(stdscr, cursor_y_, cursor_x_);
    wnoutrefresh(stdscr);
    doupdate();
}

}  // namespace rv::tui
