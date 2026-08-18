// Keyboard input, decoupled from ncurses key codes.
//
// The keymap and every widget match on these, so key handling can be tested by
// synthesising events -- which is also what `--replay` does to drive the whole
// application headlessly.
#pragma once

#include <string>

#include "isa/types.hpp"

namespace rv::tui {

enum class KeyCode : u16 {
    None,
    Char,  // a printable character; see Key::ch
    Enter,
    Escape,
    Backspace,
    Delete,
    Tab,
    BackTab,  // shift+tab
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Insert,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    /// The terminal changed size. Delivered as a key so the event loop has one
    /// place to handle everything that can arrive from the outside.
    Resize,
};

struct Key {
    KeyCode code = KeyCode::None;
    /// Unicode code point when `code == KeyCode::Char`.
    u32 ch = 0;
    bool ctrl = false;
    bool alt = false;

    bool valid() const { return code != KeyCode::None; }
    bool is_char(u32 expected) const { return code == KeyCode::Char && ch == expected; }
    bool is_ctrl(u32 letter) const {
        return ctrl && code == KeyCode::Char && (ch | 0x20u) == (letter | 0x20u);
    }

    bool operator==(const Key& other) const {
        return code == other.code && ch == other.ch && ctrl == other.ctrl && alt == other.alt;
    }

    static Key character(u32 ch) { return Key{KeyCode::Char, ch, false, false}; }
    static Key control(u32 letter) { return Key{KeyCode::Char, letter, true, false}; }
    static Key special(KeyCode code) { return Key{code, 0, false, false}; }
};

/// Human-readable name, for the key-hint row and the help overlay.
std::string key_name(const Key& key);

/// Parse a name back into a Key, for `--replay` scripts and a future config
/// file. Accepts "F5", "^S", "Enter", "Esc", "Up", or a single character.
bool parse_key(std::string_view text, Key& out);

}  // namespace rv::tui
