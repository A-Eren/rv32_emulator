// The code pane.
//
// Shows the program's own source with a gutter carrying the execution arrow,
// breakpoint marks and diagnostic markers. Not a disassembly listing: the point
// is to debug the text that was written, so the line under the arrow is the
// line the programmer typed.
#pragma once

#include "dbg/session.hpp"
#include "tui/widget.hpp"

namespace rv::tui {

class SourceView final : public Widget {
public:
    explicit SourceView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    bool on_key(const Key& key) override;
    std::string title() const override { return "Source"; }

    /// The line the selection sits on. Follows the pc unless the user has
    /// scrolled away, which is what makes browsing the program while stopped
    /// feel natural instead of fighting the cursor.
    u32 selected_line() const { return selected_; }
    void select(u32 line);
    void follow_pc();
    bool following() const { return follow_; }

    /// Called after each step so the view can re-centre on the new pc.
    void on_execution_moved();

private:
    void ensure_visible(int height);
    int gutter_width() const;

    const dbg::DebugSession* session_;
    u32 selected_ = 1;
    u32 top_ = 1;      // first visible line
    int last_height_ = 0;
    bool follow_ = true;
};

}  // namespace rv::tui
