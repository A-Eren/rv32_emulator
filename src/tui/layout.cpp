#include "tui/layout.hpp"

#include <algorithm>

#include "tui/term.hpp"

namespace rv::tui {
namespace {

/// Width of the register column in the wide profile: four characters of ABI
/// name, a space, eight of hex, plus the change marker and a gutter.
constexpr int kSideWidth = 26;

}  // namespace

bool LayoutEngine::too_small(Size size) {
    return size.width < kMinWidth || size.height < kMinHeight;
}

DebugLayout LayoutEngine::debug(Size size, bool zoom_dock) {
    DebugLayout out;
    out.profile = profile_for(size);

    Rect screen{0, 0, size.width, size.height};

    out.status = screen.take_top(kStatusRows);
    screen = screen.drop_top(kStatusRows);

    out.prompt = screen.take_bottom(kPromptRows);
    screen = screen.drop_bottom(kPromptRows);
    out.hints = screen.take_bottom(kHintRows);
    screen = screen.drop_bottom(kHintRows);
    out.message = screen.take_bottom(kMessageRows);
    screen = screen.drop_bottom(kMessageRows);

    // What is left is the body. At 80x24 that is 19 rows.
    Rect body = screen;

    if (zoom_dock) {
        // One panel, full body. Still keep the tab bar so it is obvious which
        // panel is showing and how to get back.
        out.tabs = body.take_top(kTabBarRows);
        out.dock = body.drop_top(kTabBarRows);
        return out;
    }

    if (out.profile == LayoutProfile::Wide) {
        out.side = body.take_left(kSideWidth);
        body = body.drop_left(kSideWidth);
    }

    // Split the body between code and dock. The dock needs nine rows to show
    // all 32 registers three-up plus a summary line; below that it gets a
    // proportional share and scrolls.
    constexpr int kPreferredDockRows = 9;
    const int dock_rows =
        out.profile == LayoutProfile::Wide
            ? std::max(3, std::min(kPreferredDockRows, body.height / 2))
            : std::max(3, std::min(kPreferredDockRows, body.height - 6));

    out.tabs = body.take_bottom(dock_rows + kTabBarRows).take_top(kTabBarRows);
    out.dock = body.take_bottom(dock_rows);
    out.code = body.drop_bottom(dock_rows + kTabBarRows);
    return out;
}

EditLayout LayoutEngine::edit(Size size) {
    EditLayout out;
    Rect screen{0, 0, size.width, size.height};

    out.status = screen.take_top(kStatusRows);
    screen = screen.drop_top(kStatusRows);

    out.prompt = screen.take_bottom(kPromptRows);
    screen = screen.drop_bottom(kPromptRows);
    out.hints = screen.take_bottom(kHintRows);
    screen = screen.drop_bottom(kHintRows);
    // One row for the diagnostic under the cursor. Editing is what this mode is
    // for, so everything else gets the minimum.
    out.diagnostic = screen.take_bottom(1);
    out.editor = screen.drop_bottom(1);
    return out;
}

}  // namespace rv::tui
