// ncurses translation unit 1 of 2. See curses_canvas.cpp for the naming rules
// this file has to live under.
#include "tui/term.hpp"

#include <curses.h>
#include <locale.h>
#include <unistd.h>

namespace rv::tui {
namespace {

Key from_curses(int code) {
    switch (code) {
        case ERR: return Key{};
        case KEY_RESIZE: return Key::special(KeyCode::Resize);
        case KEY_UP: return Key::special(KeyCode::Up);
        case KEY_DOWN: return Key::special(KeyCode::Down);
        case KEY_LEFT: return Key::special(KeyCode::Left);
        case KEY_RIGHT: return Key::special(KeyCode::Right);
        case KEY_HOME: return Key::special(KeyCode::Home);
        case KEY_END: return Key::special(KeyCode::End);
        case KEY_NPAGE: return Key::special(KeyCode::PageDown);
        case KEY_PPAGE: return Key::special(KeyCode::PageUp);
        case KEY_IC: return Key::special(KeyCode::Insert);
        case KEY_DC: return Key::special(KeyCode::Delete);
        case KEY_BTAB: return Key::special(KeyCode::BackTab);
        // Terminals disagree about which of these Backspace sends, so accept
        // all three rather than leaving some users unable to delete.
        case KEY_BACKSPACE:
        case 127:
        case 8:
            return Key::special(KeyCode::Backspace);
        case '\n':
        case '\r':
        case KEY_ENTER:
            return Key::special(KeyCode::Enter);
        case '\t': return Key::special(KeyCode::Tab);
        case 27: return Key::special(KeyCode::Escape);
        default: break;
    }

    if (code >= KEY_F(1) && code <= KEY_F(12)) {
        const int index = code - KEY_F(1);
        return Key::special(static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + index));
    }
    // Control characters arrive as 1..26 for ^A..^Z.
    if (code >= 1 && code <= 26) return Key::control(static_cast<u32>('a' + code - 1));
    if (code >= 32 && code < 256) return Key::character(static_cast<u32>(code));
    return Key{};
}

}  // namespace

bool is_interactive() { return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0; }

Terminal::Terminal() {
    // Must come before initscr, or ncursesw cannot decode the UTF-8 box
    // characters the frame is drawn with.
    setlocale(LC_ALL, "");

    initscr();
    raw();                  // deliver ^C and ^S to us rather than the shell
    noecho();
    nonl();
    keypad(stdscr, TRUE);   // decode arrow and function keys
    curs_set(0);
    set_escdelay(25);       // otherwise Escape lags by a second

    canvas_ = std::make_unique<CursesCanvas>();
    init_styles();
    active_ = true;
}

Terminal::~Terminal() {
    if (!active_) return;
    curs_set(1);
    endwin();
    active_ = false;
}

Size Terminal::size() const {
    int rows = 0;
    int columns = 0;
    getmaxyx(stdscr, rows, columns);
    return Size{columns, rows};
}

Key Terminal::poll(int timeout_ms) {
    wtimeout(stdscr, timeout_ms);
    return from_curses(wgetch(stdscr));
}

bool Terminal::supports_color() const { return has_colors(); }

void Terminal::suspended(const std::function<void()>& body) {
    def_prog_mode();
    endwin();
    body();
    reset_prog_mode();
    wrefresh(stdscr);
}

}  // namespace rv::tui
