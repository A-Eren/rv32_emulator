// Where everything goes.
//
// 24 rows is the binding constraint, not 80 columns. Tiling four panels into
// 80x24 produces four unreadable panels, so the narrow profile stacks a code
// view over a *tabbed* dock and gives F11 to zoom the focused one full screen.
//
// The engine takes a Size and returns rectangles. No terminal, no widgets, no
// state -- which is why the whole thing is tested at 80x24, 100x30 and 200x60
// without anything being drawn.
#pragma once

#include "tui/geometry.hpp"

namespace rv::tui {

enum class LayoutProfile : u8 {
    /// Code above a tabbed dock. The 80x24 case.
    Narrow,
    /// Code and registers side by side, with the dock beneath. Needs 120+.
    Wide,
};

/// Rows the chrome always claims, whatever the profile.
inline constexpr int kStatusRows = 1;
inline constexpr int kMessageRows = 1;
inline constexpr int kHintRows = 1;
inline constexpr int kPromptRows = 1;
inline constexpr int kTabBarRows = 1;

struct DebugLayout {
    Rect status;
    Rect code;
    Rect side;  // empty in the narrow profile
    Rect tabs;
    Rect dock;
    Rect message;
    Rect hints;
    Rect prompt;
    LayoutProfile profile = LayoutProfile::Narrow;
};

struct EditLayout {
    Rect status;
    Rect editor;
    Rect diagnostic;
    Rect hints;
    Rect prompt;
};

class LayoutEngine {
public:
    static LayoutProfile profile_for(Size size) {
        return size.width >= 120 ? LayoutProfile::Wide : LayoutProfile::Narrow;
    }

    /// `zoom_dock` gives the dock the whole body, which is the escape valve for
    /// the memory view on a short terminal.
    static DebugLayout debug(Size size, bool zoom_dock = false);
    static EditLayout edit(Size size);

    /// Below this the layout cannot be drawn honestly, and the app says so
    /// rather than producing garbage.
    static bool too_small(Size size);
};

}  // namespace rv::tui
