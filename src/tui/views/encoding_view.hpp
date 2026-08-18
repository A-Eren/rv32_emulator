// The bitfield breakdown panel.
//
// The other reason this tool exists. It answers "how did this line become 32
// bits?" by drawing the word as labelled fields, in the shape the instruction's
// format actually uses:
//
//   0x00000024   0x1234a537    lui a0, 0x1234a
//   ┌────────────────────┬─────┬───────┐
//   │31                12│11  7│6     0│
//   │00010010001101000101│01010│0110111│
//   └────────────────────┴─────┴───────┘
//    imm[31:12]=0x1234a   rd=x10 (a0)   opcode=0x37 lui
//   → a0 = 0x1234a000   (was 0x00000000)
//
// One renderer per format, driven by InstrDesc::format -- six small tables
// rather than fifty special cases. It also follows the editor's cursor, so the
// encoding of a line appears before it has ever been run.
#pragma once

#include "dbg/session.hpp"
#include "tui/widget.hpp"

namespace rv::tui {

class EncodingView final : public Widget {
public:
    explicit EncodingView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    std::string title() const override { return "Encode"; }

    /// Show a specific address rather than the pc. Used when the disassembly
    /// view's selection moves, and when the editor's cursor sits on a line
    /// that assembled to something.
    void set_address(Addr addr) {
        address_ = addr;
        follow_pc_ = false;
    }
    void follow_pc() { follow_pc_ = true; }

private:
    Addr shown_address() const { return follow_pc_ ? session_->cpu().pc : address_; }

    const dbg::DebugSession* session_;
    Addr address_ = 0;
    bool follow_pc_ = true;
};

}  // namespace rv::tui
