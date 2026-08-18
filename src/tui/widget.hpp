// The widget interface.
//
// Flat on purpose: there are about ten widgets and the layout engine decides
// where they go, so a scene graph with containers and children would be pure
// ceremony. A widget draws into the Pane it is handed and, if focused, may
// consume a key.
#pragma once

#include <string>

#include "tui/canvas.hpp"
#include "tui/key.hpp"

namespace rv::tui {

class Widget {
public:
    virtual ~Widget() = default;

    virtual void draw(Pane pane) = 0;

    /// Return true if the key was consumed. Unconsumed keys fall through to
    /// the application's global bindings.
    virtual bool on_key(const Key& key) {
        (void)key;
        return false;
    }

    /// Shown in the dock's tab bar.
    virtual std::string title() const { return {}; }

    /// A one-line summary for the status bar, when this widget is focused.
    virtual std::string status_hint() const { return {}; }

    bool focused = false;
};

}  // namespace rv::tui
