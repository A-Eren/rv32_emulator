// View rendering, with no terminal involved.
//
// This is what the Canvas abstraction bought. A view's draw() is a pure
// function from session state to a grid of cells, so the panels can be checked
// the same way any other function is -- including that the changed register is
// actually *highlighted*, not merely present.
#include "dbg/session.hpp"
#include "isa/regnames.hpp"
#include "rv_test.hpp"
#include "tui/layout.hpp"
#include "tui/string_canvas.hpp"
#include "tui/views/encoding_view.hpp"
#include "tui/views/regs_view.hpp"

using namespace rv;
using namespace rv::tui;

namespace {

dbg::DebugSession make_session(const std::string& text) {
    dbg::DebugSession session;
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;
    session.load_source(text, "test.s", options);
    return session;
}

/// Render one widget into a fresh canvas of the given size.
StringCanvas render(Widget& widget, int width, int height) {
    StringCanvas canvas(width, height);
    widget.draw(Pane(canvas, canvas.bounds()));
    return canvas;
}

}  // namespace

// ---- layout ---------------------------------------------------------------

RV_TEST(render, the_narrow_layout_fits_eighty_by_twenty_four) {
    const DebugLayout layout = LayoutEngine::debug(Size{80, 24});
    RV_CHECK_EQ(static_cast<int>(layout.profile), static_cast<int>(LayoutProfile::Narrow));

    // Every region must be inside the screen and none may overlap the chrome.
    RV_CHECK_EQ(layout.status.height, 1);
    RV_CHECK_EQ(layout.status.y, 0);
    RV_CHECK_EQ(layout.prompt.y, 23);
    RV_CHECK_EQ(layout.hints.y, 22);
    RV_CHECK_EQ(layout.message.y, 21);

    // Code sits above the tab bar, which sits above the dock.
    RV_CHECK(layout.code.bottom() == layout.tabs.y);
    RV_CHECK(layout.tabs.bottom() == layout.dock.y);
    RV_CHECK(layout.dock.bottom() == layout.message.y);
    RV_CHECK(layout.code.height >= 6);

    // The dock has room for 32 registers four-up plus the summary line.
    RV_CHECK(layout.dock.height >= 9);
    RV_CHECK(layout.side.empty());  // no side column when narrow
}

RV_TEST(render, the_wide_layout_puts_registers_beside_the_code) {
    const DebugLayout layout = LayoutEngine::debug(Size{140, 40});
    RV_CHECK_EQ(static_cast<int>(layout.profile), static_cast<int>(LayoutProfile::Wide));
    RV_CHECK(!layout.side.empty());
    RV_CHECK_EQ(layout.side.x, 0);
    RV_CHECK_EQ(layout.code.x, layout.side.right());
}

RV_TEST(render, zooming_gives_the_dock_the_whole_body) {
    const DebugLayout normal = LayoutEngine::debug(Size{80, 24}, false);
    const DebugLayout zoomed = LayoutEngine::debug(Size{80, 24}, true);
    RV_CHECK(zoomed.dock.height > normal.dock.height);
    RV_CHECK(zoomed.code.empty());
    // The tab bar survives, so it stays obvious which panel is showing.
    RV_CHECK(!zoomed.tabs.empty());
}

RV_TEST(render, layout_holds_together_at_every_size_we_claim_to_support) {
    const std::vector<Size> sizes = {{60, 18}, {80, 24}, {100, 30}, {120, 40}, {200, 60}};
    for (const Size size : sizes) {
        RV_CHECK(!LayoutEngine::too_small(size));
        const DebugLayout layout = LayoutEngine::debug(size);
        const Rect screen{0, 0, size.width, size.height};
        for (const Rect& region : {layout.status, layout.code, layout.tabs, layout.dock,
                                   layout.message, layout.hints, layout.prompt}) {
            RV_CHECK(region.intersect(screen) == region);  // nothing off-screen
        }
        RV_CHECK(layout.code.height >= 1 || layout.dock.height >= 1);
    }
    RV_CHECK(LayoutEngine::too_small(Size{59, 24}));
    RV_CHECK(LayoutEngine::too_small(Size{80, 17}));
}

// ---- the register panel ---------------------------------------------------

RV_TEST(render, registers_show_both_spellings) {
    dbg::DebugSession session = make_session("li a0, 5\nebreak\n");
    RegsView view(session);
    const StringCanvas canvas = render(view, 80, 9);

    // x10 and a0 are the same register; the encoding has the number and the
    // assembly has the name, so both are on screen.
    const std::string text = canvas.to_string();
    RV_CHECK_NE(text.find("x10 a0"), std::string::npos);
    RV_CHECK_NE(text.find("x2  sp"), std::string::npos);
    RV_CHECK_NE(text.find("x0  zero"), std::string::npos);
    RV_CHECK_NE(text.find("instret"), std::string::npos);
}

RV_TEST(render, all_thirty_two_registers_and_the_summary_fit_the_narrow_dock) {
    dbg::DebugSession session = make_session("ebreak\n");
    RegsView view(session);
    const DebugLayout layout = LayoutEngine::debug(Size{80, 24});
    const StringCanvas canvas = render(view, layout.dock.width, layout.dock.height);

    const std::string text = canvas.to_string();
    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        RV_CHECK_NE(text.find(std::string(isa::abi_name(reg))), std::string::npos);
    }
    RV_CHECK_NE(text.find("pc "), std::string::npos);
}

RV_TEST(render, the_changed_register_is_highlighted_not_merely_present) {
    dbg::DebugSession session = make_session(
        "        li a0, 5\n"
        "        li a1, 6\n"
        "        ebreak\n");
    RegsView view(session);

    session.step(dbg::StepKind::Line);  // writes a0
    view.note_step();
    StringCanvas canvas = render(view, 80, 9);

    // a0 changed, so its value carries the highlight style. Checking the style
    // rather than the text is the point: "the value is on screen" would pass
    // even if the highlighting were completely broken.
    RV_CHECK(canvas.to_string().find("*x10 a0") != std::string::npos);

    bool found_changed = false;
    for (int y = 0; y < 9 && !found_changed; ++y) {
        found_changed = canvas.row_has_style(y, StyleId::ChangedValue);
    }
    RV_CHECK(found_changed);
}

RV_TEST(render, the_highlight_fades_over_the_following_steps) {
    dbg::DebugSession session = make_session(
        "        li a0, 5\n"
        "        li a1, 6\n"
        "        li a2, 7\n"
        "        li a3, 8\n"
        "        li a4, 9\n"
        "        ebreak\n");
    RegsView view(session);

    session.step(dbg::StepKind::Line);
    view.note_step();
    RV_CHECK(render(view, 80, 9).to_string().find("*x10 a0") != std::string::npos);

    // One step later a0 is no longer the freshest change, so it loses the
    // marker -- otherwise every register would end up starred.
    session.step(dbg::StepKind::Line);
    view.note_step();
    const std::string after = render(view, 80, 9).to_string();
    RV_CHECK(after.find("*x11 a1") != std::string::npos);
    RV_CHECK(after.find("*x10 a0") == std::string::npos);
}

RV_TEST(render, zero_registers_are_dimmed_so_the_used_ones_stand_out) {
    dbg::DebugSession session = make_session("li a0, 5\nebreak\n");
    RegsView view(session);
    session.step(dbg::StepKind::Line);
    view.note_step();

    StringCanvas canvas(80, 9);
    view.draw(Pane(canvas, canvas.bounds()));

    // x1 (ra) is still zero here, so its value is dim rather than normal.
    const std::string first_row = canvas.line(0);
    RV_CHECK_NE(first_row.find("x1  ra"), std::string::npos);
    RV_CHECK(canvas.row_has_style(0, StyleId::ZeroValue));
}

// ---- the encoding panel ---------------------------------------------------

RV_TEST(render, the_encoding_panel_draws_the_fields_of_a_u_type) {
    dbg::DebugSession session = make_session("lui a0, 0x12345\nebreak\n");
    EncodingView view(session);
    const StringCanvas canvas = render(view, 80, 9);
    const std::string text = canvas.to_string();

    RV_CHECK_NE(text.find("lui a0, 0x12345"), std::string::npos);
    RV_CHECK_NE(text.find("imm[31:12]"), std::string::npos);
    RV_CHECK_NE(text.find("rd=x10 (a0)"), std::string::npos);
    RV_CHECK_NE(text.find("opcode="), std::string::npos);
    // The box is drawn, and the binary digits are on screen.
    RV_CHECK_NE(text.find("┌"), std::string::npos);
    RV_CHECK_NE(text.find("00010010001101000101"), std::string::npos);
}

RV_TEST(render, the_encoding_panel_names_the_scrambled_branch_immediate) {
    // The B-type immediate is the one everybody gets wrong. Naming the pieces
    // is the whole point of the panel.
    dbg::DebugSession session = make_session(
        "here:   beq a0, a1, here\n"
        "        ebreak\n");
    EncodingView view(session);
    const std::string text = render(view, 80, 9).to_string();

    RV_CHECK_NE(text.find("imm[12|10:5]"), std::string::npos);
    RV_CHECK_NE(text.find("imm[4:1|11]"), std::string::npos);
    RV_CHECK_NE(text.find("rs1=x10 (a0)"), std::string::npos);
    RV_CHECK_NE(text.find("rs2=x11 (a1)"), std::string::npos);
}

RV_TEST(render, the_encoding_panel_shows_r_type_funct_fields) {
    dbg::DebugSession session = make_session("add a0, a1, a2\nebreak\n");
    EncodingView view(session);
    const std::string text = render(view, 80, 9).to_string();
    RV_CHECK_NE(text.find("funct7"), std::string::npos);
    RV_CHECK_NE(text.find("funct3"), std::string::npos);
    RV_CHECK_NE(text.find("rs2=x12 (a2)"), std::string::npos);
}

RV_TEST(render, the_encoding_panel_reports_what_the_instruction_did) {
    dbg::DebugSession session = make_session("li a0, 5\nebreak\n");
    EncodingView view(session);
    session.step(dbg::StepKind::Instruction);

    // After executing, the panel follows the pc -- so point it back at the
    // instruction that just ran.
    view.set_address(0);
    const std::string text = render(view, 80, 9).to_string();
    RV_CHECK_NE(text.find("→ a0 = 0x00000005"), std::string::npos);
    RV_CHECK_NE(text.find("was 0x00000000"), std::string::npos);
}

RV_TEST(render, the_encoding_panel_works_before_anything_has_run) {
    // Following the editor's cursor is the reason this matters: you can see
    // what a line encodes to without running it.
    dbg::DebugSession session = make_session(
        "        li a0, 0x12345\n"
        "        ebreak\n");
    EncodingView view(session);
    view.set_address(4);  // the second word of the li
    const std::string text = render(view, 80, 9).to_string();
    RV_CHECK_NE(text.find("from line 1, word 2 of 2"), std::string::npos);
}

RV_TEST(render, an_invalid_word_says_so_rather_than_drawing_nonsense) {
    dbg::DebugSession session;
    session.load_image({0xffffffffu});
    EncodingView view(session);
    const std::string text = render(view, 80, 9).to_string();
    RV_CHECK_NE(text.find("not a valid instruction"), std::string::npos);
}

RV_TEST(render, views_survive_being_drawn_into_a_tiny_pane) {
    // The app clamps to a minimum size, but a panel must never write outside
    // its rectangle or fault when the rectangle is absurd.
    dbg::DebugSession session = make_session("li a0, 5\nebreak\n");
    RegsView regs(session);
    EncodingView encoding(session);

    for (const Size size : {Size{1, 1}, Size{10, 2}, Size{40, 3}, Size{0, 0}}) {
        StringCanvas canvas(size.width, size.height);
        regs.draw(Pane(canvas, canvas.bounds()));
        encoding.draw(Pane(canvas, canvas.bounds()));
        // No crash, and nothing beyond the declared height.
        RV_CHECK_EQ(canvas.size().height, size.height);
    }
}

// ---- the canvas itself ----------------------------------------------------

RV_TEST(render, the_canvas_clips_rather_than_overflowing) {
    StringCanvas canvas(10, 2);
    canvas.put(6, 0, "abcdefgh", StyleId::Normal);
    RV_CHECK_STR(canvas.line(0), "      abcd");

    canvas.put(-2, 1, "abcdef", StyleId::Normal);
    RV_CHECK_STR(canvas.line(1), "cdef");

    canvas.put(0, 99, "off screen", StyleId::Normal);  // must not fault
}

RV_TEST(render, a_pane_offsets_and_clips_what_it_is_given) {
    StringCanvas canvas(20, 3);
    Pane pane(canvas, Rect{5, 1, 6, 1});
    pane.put(0, 0, "abcdefghij", StyleId::Normal);
    RV_CHECK_STR(canvas.line(1), "     abcdef");
    RV_CHECK_STR(canvas.line(0), "");
}

RV_TEST(render, a_background_fill_keeps_the_text_underneath) {
    // This is how the current-line highlight works without drawing twice.
    StringCanvas canvas(10, 1);
    canvas.put(0, 0, "hello", StyleId::Normal);
    canvas.fill(Rect{0, 0, 10, 1}, {}, StyleId::CurrentLine);
    RV_CHECK_STR(canvas.line(0), "hello");
    RV_CHECK_EQ(static_cast<int>(canvas.style_at(1, 0)), static_cast<int>(StyleId::CurrentLine));
}

RV_TEST(render, multibyte_glyphs_occupy_one_column_each) {
    StringCanvas canvas(6, 1);
    canvas.put(0, 0, "──▶", StyleId::Frame);
    RV_CHECK_EQ(display_width("──▶"), 3);
    RV_CHECK_STR(canvas.line(0), "──▶");
}
