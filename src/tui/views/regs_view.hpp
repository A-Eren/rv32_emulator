// The register panel.
//
// One of the two reasons this tool exists: after every step, the register that
// just changed has to be the first thing the eye lands on. The highlight fades
// over the following few steps so recent history is visible at a glance without
// the whole panel turning yellow.
//
// Both spellings are shown -- `x10 a0` -- because the encoding contains the
// number and real assembly is written with the name, and a learner needs the
// correspondence rather than one or the other.
#pragma once

#include <array>

#include "dbg/session.hpp"
#include "tui/widget.hpp"

namespace rv::tui {

class RegsView final : public Widget {
public:
    explicit RegsView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    std::string title() const override { return "Regs"; }

    /// Record what the last step changed. Called by the app after each step
    /// rather than sampled during draw, because a redraw can happen many times
    /// between two steps and the fade must not advance with it.
    void note_step();
    void forget_history();

private:
    /// How many steps a highlight survives before fading out entirely.
    static constexpr u64 kFadeSteps = 3;

    StyleId style_for_value(RegIdx reg) const;

    const dbg::DebugSession* session_;
    /// instret at the moment each register last changed, or 0 for never.
    std::array<u64, kNumRegs> changed_at_{};
    bool have_history_ = false;
};

}  // namespace rv::tui
