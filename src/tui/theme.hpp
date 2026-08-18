// Semantic styles.
//
// Views never name a colour. They name a *role* -- StyleId::Mnemonic,
// StyleId::ChangedValue -- and the theme decides what that looks like. That is
// what lets the whole UI be recoloured in one place, and what lets the headless
// StringCanvas record styles as small integers it can assert on.
#pragma once

#include "isa/types.hpp"

namespace rv::tui {

enum class StyleId : u8 {
    Normal,
    Dim,          // line numbers, inactive chrome
    Frame,        // box drawing and separators
    Title,        // panel headings
    StatusBar,
    StatusAccent, // the mode indicator inside the status bar
    Hint,         // the key-hint row

    // Source and assembly
    LineNumber,
    Mnemonic,
    Register,
    Number,
    Label,
    Directive,
    Comment,
    StringLit,
    Punctuation,

    // Debugger state
    CurrentLine,   // the row the pc is on
    Arrow,         // the execution marker itself
    Breakpoint,
    ChangedValue,  // a register or word the last step wrote
    RecentValue,   // changed a step or two ago, fading out
    ZeroValue,     // dim, so the handful of non-zero registers stand out

    // Diagnostics
    Error,
    Warning,
    Note,
    Caret,
    /// The exact text a diagnostic points at. Underlined rather than covered
    /// with carets: in an editor the offending word has to stay readable, and
    /// an underline costs no cells on a 24-row screen.
    ErrorSpan,
    WarningSpan,

    Count,
};

/// Attribute flags, kept separate from colour so a style can be bold *and*
/// themed. Values match nothing in ncurses on purpose; term.cpp translates.
enum class Attr : u16 {
    None = 0,
    Bold = 1 << 0,
    Underline = 1 << 1,
    Reverse = 1 << 2,
    Dim = 1 << 3,
};

constexpr Attr operator|(Attr a, Attr b) {
    return static_cast<Attr>(static_cast<u16>(a) | static_cast<u16>(b));
}
constexpr bool has(Attr value, Attr flag) {
    return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
}

struct Style {
    /// 256-colour indices. -1 means "the terminal's default".
    short fg = -1;
    short bg = -1;
    Attr attr = Attr::None;
};

/// Look up the style for a role. Free function rather than an object because
/// there is exactly one theme and threading it through every draw call would be
/// ceremony for nothing.
const Style& style_for(StyleId id);

/// Switch to the palette that suits a light-background terminal. Off by
/// default: a dark terminal is the common case, and the difference matters
/// because mid-grey "dim" text is unreadable on white.
void use_light_theme(bool light);

}  // namespace rv::tui
