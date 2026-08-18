// The application, driven headlessly.
//
// App draws into a Canvas and consumes Keys, so the entire interface can be
// exercised without a terminal: press F8, look at the frame, assert the arrow
// moved. That is the same path --replay and --render-once take, which means
// these tests cover the real code rather than a parallel one.
#include "dbg/session.hpp"
#include "rv_test.hpp"
#include "tui/app.hpp"
#include "tui/string_canvas.hpp"

using namespace rv;
using namespace rv::tui;

namespace {

constexpr const char* kProgram =
    "        .data\n"
    "msg:    .asciz \"hi\"\n"
    "        .text\n"
    "_start: li   a0, 1\n"
    "        li   a1, 0x12345\n"
    "        add  a2, a0, a1\n"
    "        li   a3, 0xffff0000\n"
    "        sb   a0, 0(a3)\n"
    "        ebreak\n";

dbg::DebugSession make_session(const std::string& text = kProgram) {
    dbg::DebugSession session;
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;
    session.load_source(text, "test.s", options);
    return session;
}

std::string frame(App& app, int width = 80, int height = 24) {
    return render_frame(app, width, height);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// The row carrying the execution arrow. Matching on this rather than on an
/// exact gutter spelling keeps the tests from breaking every time the gutter
/// width changes.
std::string arrow_row(const std::string& text) {
    const std::size_t marker = text.find("▶");
    if (marker == std::string::npos) return {};
    const std::size_t begin = text.rfind('\n', marker);
    const std::size_t end = text.find('\n', marker);
    return text.substr(begin == std::string::npos ? 0 : begin + 1,
                       end == std::string::npos ? std::string::npos : end - begin - 1);
}

/// Press a run of keys, as a user would.
void press(App& app, std::initializer_list<Key> keys) {
    for (const Key& key : keys) app.handle_key(key);
}

void type(App& app, const std::string& text) {
    for (const char character : text) {
        app.handle_key(Key::character(static_cast<u32>(character)));
    }
}

}  // namespace

RV_TEST(app, a_frame_has_all_the_chrome) {
    dbg::DebugSession session = make_session();
    App app(session);
    const std::string text = frame(app);

    RV_CHECK(contains(text, "DEBUG"));
    RV_CHECK(contains(text, "pc 00000000"));
    RV_CHECK(contains(text, "[1 Regs]"));   // the active tab is bracketed
    RV_CHECK(contains(text, "6 Sym"));
    RV_CHECK(contains(text, "F8 step"));
    RV_CHECK(contains(text, "q quit"));
    // The status bar names the instruction about to run.
    RV_CHECK(contains(text, "addi a0, zero, 1"));
}

RV_TEST(app, the_arrow_marks_the_current_line_and_moves_when_stepping) {
    dbg::DebugSession session = make_session();
    App app(session);

    RV_CHECK(contains(arrow_row(frame(app)), "_start: li   a0, 1"));
    press(app, {Key::special(KeyCode::F8)});
    RV_CHECK(contains(arrow_row(frame(app)), "li   a1, 0x12345"));
}

RV_TEST(app, stepping_by_instruction_shows_the_two_halves_of_a_pseudo) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::special(KeyCode::F8)});  // onto the li on line 5

    // F7 executes one machine instruction, so it lands inside the pseudo and
    // the status bar says which word it is on. This is the whole reason the
    // instruction-level step exists.
    press(app, {Key::special(KeyCode::F7)});
    const std::string text = frame(app);
    RV_CHECK(contains(text, "[2/2]"));
    RV_CHECK(contains(arrow_row(text), "li   a1, 0x12345"));  // same source line
}

RV_TEST(app, the_changed_register_is_marked_after_a_step) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::special(KeyCode::F8)});
    RV_CHECK(contains(frame(app), "*x10 a0   00000001"));
}

RV_TEST(app, f9_toggles_a_breakpoint_on_the_selected_line) {
    dbg::DebugSession session = make_session();
    App app(session);

    RV_CHECK(!contains(frame(app), "●"));
    press(app, {Key::special(KeyCode::F9)});
    const std::string with = frame(app);
    RV_CHECK(contains(with, "●"));
    RV_CHECK(contains(with, "breakpoint set on line 4"));

    press(app, {Key::special(KeyCode::F9)});
    RV_CHECK(!contains(frame(app), "●"));
}

RV_TEST(app, running_stops_at_a_breakpoint) {
    dbg::DebugSession session = make_session();
    App app(session);

    // Select line 6 and break there.
    press(app, {Key::special(KeyCode::Down), Key::special(KeyCode::Down),
                Key::special(KeyCode::F9)});
    press(app, {Key::special(KeyCode::F5)});
    RV_CHECK(app.is_running());

    app.tick();  // one slice is plenty for this program
    RV_CHECK(!app.is_running());
    RV_CHECK(contains(app.message(), "breakpoint"));
    RV_CHECK(contains(arrow_row(frame(app)), "add  a2, a0, a1"));
}

RV_TEST(app, any_key_interrupts_a_run) {
    // The escape hatch for an accidental infinite loop must not require
    // knowing a particular key.
    dbg::DebugSession session = make_session("spin: j spin\n");
    App app(session);
    press(app, {Key::special(KeyCode::F5)});
    RV_CHECK(app.is_running());

    press(app, {Key::character('x')});
    RV_CHECK(!app.is_running());
    RV_CHECK(contains(app.message(), "interrupted"));
}

RV_TEST(app, a_watchdog_stop_is_reported_as_a_probable_loop) {
    dbg::SessionOptions session_options;
    session_options.watchdog = 2000;
    dbg::DebugSession session(session_options);
    as::AssembleOptions assemble_options;
    assemble_options.explain_pseudo_sizing = false;
    session.load_source("spin: j spin\n", "test.s", assemble_options);

    App app(session);
    press(app, {Key::special(KeyCode::F5)});
    for (int i = 0; i < 4 && app.is_running(); ++i) app.tick();
    RV_CHECK(!app.is_running());
    RV_CHECK(contains(app.message(), "infinite loop"));
}

RV_TEST(app, stepping_backwards_undoes_the_last_line) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::special(KeyCode::F8), Key::special(KeyCode::F8)});
    RV_CHECK(contains(arrow_row(frame(app)), "add  a2, a0, a1"));

    press(app, {Key::special(KeyCode::F6)});
    RV_CHECK(contains(arrow_row(frame(app)), "li   a1, 0x12345"));
    // a1 was written by line 5, so undoing it puts the register back.
    RV_CHECK(contains(frame(app), "x11 a1   00000000"));
}

RV_TEST(app, reverse_stepping_past_the_start_says_so_rather_than_doing_nothing) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::special(KeyCode::F6)});
    RV_CHECK(contains(app.message(), "no further history"));
}

RV_TEST(app, the_dock_tabs_switch_panels) {
    dbg::DebugSession session = make_session();
    App app(session);

    press(app, {Key::character('3')});
    const std::string encoding = frame(app);
    RV_CHECK(contains(encoding, "[3 Encode]"));
    RV_CHECK(contains(encoding, "opcode="));

    press(app, {Key::character('6')});
    const std::string symbols = frame(app);
    RV_CHECK(contains(symbols, "[6 Sym]"));
    RV_CHECK(contains(symbols, "msg"));
    // The space column is the thing that explains why jumping to a data symbol
    // gets rejected.
    RV_CHECK(contains(symbols, "data"));
    RV_CHECK(contains(symbols, "text"));
}

RV_TEST(app, the_memory_panel_follows_a_store) {
    dbg::DebugSession session = make_session(
        "_start: li a1, 0x40\n"
        "        li a0, 0x1234\n"
        "        sw a0, 0(a1)\n"
        "        ebreak\n");
    App app(session);
    press(app, {Key::character('2')});  // memory tab

    press(app, {Key::special(KeyCode::F8), Key::special(KeyCode::F8),
                Key::special(KeyCode::F8)});
    RV_CHECK(contains(frame(app), "00001234"));
}

RV_TEST(app, the_console_panel_shows_uart_output) {
    dbg::DebugSession session = make_session(
        "_start: li a3, 0xffff0000\n"
        "        li a0, 'h'\n"
        "        sb a0, 0(a3)\n"
        "        li a0, 'i'\n"
        "        sb a0, 0(a3)\n"
        "        ebreak\n");
    App app(session);
    press(app, {Key::character('4')});
    RV_CHECK(contains(frame(app), "nothing written"));

    for (int i = 0; i < 5; ++i) press(app, {Key::special(KeyCode::F8)});
    RV_CHECK(contains(frame(app), "hi"));
}

RV_TEST(app, the_prompt_runs_the_same_commands_the_cli_does) {
    dbg::DebugSession session = make_session();
    App app(session);

    press(app, {Key::character(':')});
    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Prompt));
    type(app, "break 6");
    RV_CHECK(contains(frame(app), ":break 6"));

    press(app, {Key::special(KeyCode::Enter)});
    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Debug));
    RV_CHECK(contains(app.message(), "breakpoint 1 at line 6"));
    RV_CHECK(contains(frame(app), "●"));
}

RV_TEST(app, the_prompt_reports_a_bad_command_without_leaving_the_ui) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::character(':')});
    type(app, "brek 3");
    press(app, {Key::special(KeyCode::Enter)});

    RV_CHECK(contains(app.message(), "did you mean 'break'"));
    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Debug));
}

RV_TEST(app, escape_abandons_the_prompt) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::character(':')});
    type(app, "quit");
    press(app, {Key::special(KeyCode::Escape)});

    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Debug));
    RV_CHECK(!app.wants_quit());
}

RV_TEST(app, prompt_history_recalls_the_previous_command) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::character(':')});
    type(app, "info break");
    press(app, {Key::special(KeyCode::Enter)});

    press(app, {Key::character(':'), Key::special(KeyCode::Up)});
    RV_CHECK(contains(frame(app), ":info break"));
}

RV_TEST(app, moving_the_selection_moves_what_the_encoding_panel_shows) {
    // Browsing the program should explain itself as you go, which is why the
    // panel follows the selection rather than only the pc.
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::character('3')});                 // encoding tab
    press(app, {Key::special(KeyCode::Tab)});          // focus the code pane
    press(app, {Key::special(KeyCode::Down),
                Key::special(KeyCode::Down)});         // down to line 6: add

    RV_CHECK(contains(frame(app), "add a2, a0, a1"));
    RV_CHECK(contains(frame(app), "funct7"));
}

RV_TEST(app, help_is_reachable_and_dismissed_by_any_key) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::character('?')});
    const std::string help = frame(app);
    RV_CHECK(contains(help, "step one machine instruction"));
    RV_CHECK(contains(help, "toggle a breakpoint"));

    press(app, {Key::character(' ')});
    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Debug));
}

RV_TEST(app, zooming_gives_the_dock_the_screen) {
    dbg::DebugSession session = make_session();
    App app(session);
    press(app, {Key::character('2'), Key::special(KeyCode::F11)});
    const std::string text = frame(app);

    RV_CHECK(contains(text, "F11 unzoom"));
    RV_CHECK(!contains(text, "_start: li"));  // the code pane is gone
    RV_CHECK(contains(text, "00000000 00000000"));  // and memory fills the body
}

RV_TEST(app, a_terminal_that_is_too_small_says_so_instead_of_drawing_garbage) {
    dbg::DebugSession session = make_session();
    App app(session);
    const std::string text = frame(app, 40, 10);
    RV_CHECK(contains(text, "terminal too small"));
    RV_CHECK(contains(text, "60x18"));
}

RV_TEST(app, the_frame_never_exceeds_the_canvas) {
    // Every panel must stay inside its rectangle at every size we claim to
    // support, including the exact 80x24 the layout is tuned for.
    dbg::DebugSession session = make_session();
    App app(session);
    for (const Size size : {Size{60, 18}, Size{80, 24}, Size{100, 30}, Size{140, 45}}) {
        StringCanvas canvas(size.width, size.height);
        app.draw(canvas);
        for (int y = 0; y < size.height; ++y) {
            RV_CHECK(static_cast<int>(display_width(canvas.line(y))) <= size.width);
        }
    }
}

RV_TEST(app, q_quits) {
    dbg::DebugSession session = make_session();
    App app(session);
    RV_CHECK(app.handle_key(Key::character('s')));
    RV_CHECK(!app.handle_key(Key::character('q')));
    RV_CHECK(app.wants_quit());
}
