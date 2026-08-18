// Terminal lifetime and input.
//
// RAII: constructing a Terminal puts the display into raw full-screen mode and
// destroying it puts the display back, including when an exception unwinds
// through it. A crash that leaves the terminal unusable is a bad afternoon.
#pragma once

#include <functional>
#include <memory>

#include "tui/curses_canvas.hpp"
#include "tui/key.hpp"

namespace rv::tui {

class Terminal {
public:
    Terminal();
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    Size size() const;
    CursesCanvas& canvas() { return *canvas_; }

    /// Wait for a key. `timeout_ms` of -1 blocks; 0 polls and may return an
    /// invalid Key. A terminal resize arrives as KeyCode::Resize.
    Key poll(int timeout_ms);

    bool supports_color() const;

    /// Leave full-screen mode temporarily, run `body`, then come back. Used
    /// for shelling out and for printing a crash report legibly.
    void suspended(const std::function<void()>& body);

private:
    std::unique_ptr<CursesCanvas> canvas_;
    bool active_ = false;
};

/// True if stdin and stdout are both terminals. The TUI refuses to start
/// otherwise and says why, rather than producing a screenful of escape codes.
bool is_interactive();

/// The smallest terminal the layout can honestly use.
inline constexpr int kMinWidth = 60;
inline constexpr int kMinHeight = 18;

}  // namespace rv::tui
