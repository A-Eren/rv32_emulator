// The embedded editor: buffer, undo, and the write-F5-debug loop.
#include "dbg/session.hpp"
#include "rv_test.hpp"
#include "tui/app.hpp"
#include "tui/editor/text_buffer.hpp"
#include "tui/editor/undo.hpp"
#include "tui/string_canvas.hpp"

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

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void type(EditorView& editor, const std::string& text) {
    for (const char character : text) {
        if (character == '\n') {
            editor.on_key(Key::special(KeyCode::Enter));
        } else {
            editor.on_key(Key::character(static_cast<u32>(character)));
        }
    }
}

}  // namespace

// ---- the buffer -----------------------------------------------------------

RV_TEST(editor, a_buffer_round_trips_its_text) {
    TextBuffer buffer("one\ntwo\nthree\n");
    RV_CHECK_EQ(buffer.line_count(), std::size_t{3});
    RV_CHECK_EQ(buffer.line(1), "one");
    RV_CHECK_EQ(buffer.line(3), "three");
    RV_CHECK_STR(buffer.to_string(), "one\ntwo\nthree\n");
}

RV_TEST(editor, an_empty_buffer_still_has_one_line_to_stand_on) {
    TextBuffer buffer;
    RV_CHECK_EQ(buffer.line_count(), std::size_t{1});
    RV_CHECK_EQ(buffer.line(1), "");

    TextBuffer from_empty("");
    RV_CHECK_EQ(from_empty.line_count(), std::size_t{1});
}

RV_TEST(editor, crlf_input_does_not_leave_stray_characters) {
    TextBuffer buffer("one\r\ntwo\r\n");
    RV_CHECK_EQ(buffer.line(1), "one");
    RV_CHECK_EQ(buffer.line(2), "two");
}

RV_TEST(editor, inserting_within_a_line) {
    TextBuffer buffer("hello world\n");
    const Pos after = buffer.insert(Pos{1, 5}, ",");
    RV_CHECK_EQ(buffer.line(1), "hello, world");
    RV_CHECK_EQ(after.col, 6u);
}

RV_TEST(editor, inserting_a_newline_splits_the_line) {
    TextBuffer buffer("hello world\n");
    const Pos after = buffer.insert(Pos{1, 5}, "\n");
    RV_CHECK_EQ(buffer.line_count(), std::size_t{2});
    RV_CHECK_EQ(buffer.line(1), "hello");
    RV_CHECK_EQ(buffer.line(2), " world");
    RV_CHECK_EQ(after.line, 2u);
    RV_CHECK_EQ(after.col, 0u);
}

RV_TEST(editor, inserting_multiple_lines_at_once) {
    TextBuffer buffer("ac\n");
    buffer.insert(Pos{1, 1}, "1\n2\nb");
    RV_CHECK_STR(buffer.to_string(), "a1\n2\nbc\n");
}

RV_TEST(editor, erasing_across_lines_joins_them) {
    TextBuffer buffer("one\ntwo\nthree\n");
    const std::string removed = buffer.erase(Range{Pos{1, 2}, Pos{3, 2}});
    RV_CHECK_STR(removed, "e\ntwo\nth");
    RV_CHECK_STR(buffer.to_string(), "onree\n");
}

RV_TEST(editor, the_revision_advances_on_every_change) {
    TextBuffer buffer("a\n");
    const u64 before = buffer.revision();
    buffer.insert(Pos{1, 0}, "x");
    RV_CHECK(buffer.revision() > before);
}

RV_TEST(editor, positions_are_clamped_rather_than_faulting) {
    TextBuffer buffer("abc\n");
    RV_CHECK_EQ(buffer.clamp(Pos{99, 99}).line, 1u);
    RV_CHECK_EQ(buffer.clamp(Pos{1, 99}).col, 3u);
    RV_CHECK_EQ(buffer.line(0), "");
    RV_CHECK_EQ(buffer.line(99), "");
}

// ---- undo -----------------------------------------------------------------

RV_TEST(editor, consecutive_typing_undoes_as_one_group) {
    // Otherwise undo steps back one character at a time, which is unusable.
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("");

    type(editor, "addi");
    RV_CHECK_EQ(editor.buffer().line(1), "addi");

    editor.on_key(Key::control('z'));
    RV_CHECK_EQ(editor.buffer().line(1), "");
}

RV_TEST(editor, a_newline_starts_a_fresh_undo_group) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("");

    type(editor, "one\ntwo");
    RV_CHECK_EQ(editor.buffer().line_count(), std::size_t{2});

    editor.on_key(Key::control('z'));  // undo "two"
    RV_CHECK_EQ(editor.buffer().line(2), "");
    editor.on_key(Key::control('z'));  // undo the newline
    RV_CHECK_EQ(editor.buffer().line_count(), std::size_t{1});
    editor.on_key(Key::control('z'));  // undo "one"
    RV_CHECK_EQ(editor.buffer().line(1), "");
}

RV_TEST(editor, moving_the_cursor_breaks_the_typing_group) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("");

    type(editor, "ab");
    editor.on_key(Key::special(KeyCode::Home));
    type(editor, "xy");
    RV_CHECK_EQ(editor.buffer().line(1), "xyab");

    editor.on_key(Key::control('z'));
    RV_CHECK_EQ(editor.buffer().line(1), "ab");
}

RV_TEST(editor, redo_replays_what_undo_took_back) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("");

    type(editor, "hello");
    editor.on_key(Key::control('z'));
    RV_CHECK_EQ(editor.buffer().line(1), "");
    editor.on_key(Key::control('y'));
    RV_CHECK_EQ(editor.buffer().line(1), "hello");
}

RV_TEST(editor, a_new_edit_discards_the_redo_branch) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("");

    type(editor, "abc");
    editor.on_key(Key::control('z'));
    type(editor, "x");
    editor.on_key(Key::control('y'));  // nothing to redo now
    RV_CHECK_EQ(editor.buffer().line(1), "x");
}

RV_TEST(editor, undo_survives_a_multi_line_edit) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("start\n");
    editor.set_cursor(Pos{1, 5});

    type(editor, "\nmiddle\nend");
    RV_CHECK_EQ(editor.buffer().line_count(), std::size_t{3});

    while (editor.buffer().line_count() > 1) editor.on_key(Key::control('z'));
    RV_CHECK_STR(editor.buffer().to_string(), "start\n");
}

// ---- editing behaviour ----------------------------------------------------

RV_TEST(editor, enter_copies_the_previous_indentation) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("    addi a0, a1, 1\n");
    editor.set_cursor(editor.buffer().end_of_line(1));

    editor.on_key(Key::special(KeyCode::Enter));
    RV_CHECK_EQ(editor.buffer().line(2), "    ");
    RV_CHECK_EQ(editor.cursor().col, 4u);
}

RV_TEST(editor, enter_after_a_label_indents_one_level) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("loop:\n");
    editor.set_cursor(editor.buffer().end_of_line(1));

    editor.on_key(Key::special(KeyCode::Enter));
    RV_CHECK_EQ(editor.buffer().line(2), "    ");

    // A trailing comment must not hide the colon.
    editor.set_text("done:  # finished\n");
    editor.set_cursor(editor.buffer().end_of_line(1));
    editor.on_key(Key::special(KeyCode::Enter));
    RV_CHECK_EQ(editor.buffer().line(2), "    ");
}

RV_TEST(editor, backspace_removes_a_whole_indent_level) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("        addi\n");
    editor.set_cursor(Pos{1, 8});

    editor.on_key(Key::special(KeyCode::Backspace));
    RV_CHECK_EQ(editor.buffer().line(1), "    addi");

    // But inside text it removes one character.
    editor.set_cursor(editor.buffer().end_of_line(1));
    editor.on_key(Key::special(KeyCode::Backspace));
    RV_CHECK_EQ(editor.buffer().line(1), "    add");
}

RV_TEST(editor, backspace_at_the_start_of_a_line_joins_it_to_the_one_above) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("one\ntwo\n");
    editor.set_cursor(Pos{2, 0});

    editor.on_key(Key::special(KeyCode::Backspace));
    RV_CHECK_EQ(editor.buffer().line_count(), std::size_t{1});
    RV_CHECK_EQ(editor.buffer().line(1), "onetwo");
    RV_CHECK_EQ(editor.cursor().col, 3u);
}

RV_TEST(editor, vertical_motion_remembers_the_column_it_wanted) {
    // The detail everyone forgets and everyone notices: going down through a
    // short line and back up must return to the original column.
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("aaaaaaaaaa\nbb\ncccccccccc\n");
    editor.set_cursor(Pos{1, 9});

    editor.on_key(Key::special(KeyCode::Down));
    RV_CHECK_EQ(editor.cursor().col, 2u);  // clamped to the short line
    editor.on_key(Key::special(KeyCode::Down));
    RV_CHECK_EQ(editor.cursor().col, 9u);  // and restored on the long one
}

RV_TEST(editor, tab_inserts_spaces) {
    // Mixed tabs and spaces make the caret and the diagnostic underline
    // disagree, so tabs are expanded on the way in.
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("");
    editor.on_key(Key::special(KeyCode::Tab));
    RV_CHECK_EQ(editor.buffer().line(1), "    ");
}

RV_TEST(editor, the_modified_flag_tracks_edits_and_saves) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("nop\n");
    RV_CHECK(!editor.modified());

    type(editor, "x");
    RV_CHECK(editor.modified());
    editor.mark_saved();
    RV_CHECK(!editor.modified());
}

// ---- rendering ------------------------------------------------------------

RV_TEST(editor, the_gutter_carries_line_numbers_and_markers) {
    dbg::DebugSession session = make_session("_start: li a0, 1\n        ebreak\n");
    EditorView editor(session);
    editor.set_text(session.source().text());
    session.breakpoints().add_line(2, session.map());

    StringCanvas canvas(60, 6);
    editor.draw(Pane(canvas, canvas.bounds()));
    const std::string text = canvas.to_string();

    RV_CHECK(contains(text, "▶"));   // the pc is on line 1
    RV_CHECK(contains(text, "●"));   // and a breakpoint on line 2
    RV_CHECK(contains(text, "_start: li a0, 1"));
}

RV_TEST(editor, diagnostics_appear_in_the_gutter_and_under_the_text) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("        addi a0, a1, 5000\n");

    const as::SourceFile source("t.s", editor.text());
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;
    editor.set_diagnostics(as::assemble(source, options).diagnostics);

    StringCanvas canvas(60, 4);
    editor.draw(Pane(canvas, canvas.bounds()));
    const std::string text = canvas.to_string();

    RV_CHECK(contains(text, "E"));  // severity marker in the gutter
    // The offending text stays readable and is restyled, rather than being
    // covered by a row of carets.
    RV_CHECK(contains(text, "5000"));
    RV_CHECK(canvas.row_has_style(0, StyleId::ErrorSpan));
    RV_CHECK(editor.diagnostic_at_cursor() != nullptr);
}

RV_TEST(editor, jumping_to_the_first_error_puts_the_caret_on_it) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text(
        "        nop\n"
        "        addi a0, a1, 5000\n");

    const as::SourceFile source("t.s", editor.text());
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;
    editor.set_diagnostics(as::assemble(source, options).diagnostics);

    RV_CHECK(editor.go_to_first_error());
    RV_CHECK_EQ(editor.cursor().line, 2u);
}

RV_TEST(editor, long_lines_scroll_horizontally_rather_than_losing_the_caret) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("        addi a0, a1, 1   # " + std::string(120, 'x') + "\n");
    editor.set_cursor(editor.buffer().end_of_line(1));

    StringCanvas canvas(40, 3);
    editor.focused = true;
    editor.draw(Pane(canvas, canvas.bounds()));

    // The caret must be somewhere on screen, not off the right edge.
    RV_CHECK(canvas.cursor_x() >= 0);
    RV_CHECK(canvas.cursor_x() < 40);
}

// ---- the write-assemble-debug loop ----------------------------------------

RV_TEST(editor, f5_assembles_the_buffer_and_switches_to_debugging) {
    dbg::DebugSession session = make_session("        ebreak\n");
    AppOptions options;
    options.start_in_editor = true;
    App app(session, options);

    app.editor().set_text(
        "_start: li a0, 7\n"
        "        ebreak\n");
    app.handle_key(Key::special(KeyCode::F5));

    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Debug));
    RV_CHECK(contains(app.message(), "assembled"));

    app.handle_key(Key::special(KeyCode::F8));
    RV_CHECK(contains(render_frame(app, 80, 24), "*x10 a0   00000007"));
}

RV_TEST(editor, a_failed_f5_keeps_the_text_and_lands_on_the_error) {
    // The whole point of the loop: a mistake must not cost the buffer, and it
    // must put the caret where the problem is.
    dbg::DebugSession session = make_session("        ebreak\n");
    AppOptions options;
    options.start_in_editor = true;
    App app(session, options);

    app.editor().set_text(
        "_start: nop\n"
        "        addii a0, a1, 5\n"
        "        ebreak\n");
    app.handle_key(Key::special(KeyCode::F5));

    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Edit));
    RV_CHECK_EQ(app.editor().cursor().line, 2u);
    RV_CHECK(contains(app.message(), "unknown instruction"));
    RV_CHECK(contains(app.editor().text(), "addii"));

    const std::string frame = render_frame(app, 80, 24);
    RV_CHECK(contains(frame, "1 error"));
    RV_CHECK(contains(frame, "addii"));  // the mistake stays legible
}

RV_TEST(editor, a_failed_assemble_leaves_the_loaded_program_alone) {
    // Assembling a detached copy means a broken edit never destroys the program
    // that is currently steppable.
    dbg::DebugSession session = make_session("_start: li a0, 42\n        ebreak\n");
    AppOptions options;
    options.start_in_editor = true;
    App app(session, options);

    app.editor().set_text("        addii bad\n");
    app.handle_key(Key::special(KeyCode::F5));

    RV_CHECK_EQ(session.program().imem_words.size(), std::size_t{2});
    RV_CHECK(session.has_program());
}

RV_TEST(editor, the_status_bar_says_whether_the_buffer_has_been_built) {
    dbg::DebugSession session = make_session("        ebreak\n");
    AppOptions options;
    options.start_in_editor = true;
    App app(session, options);

    RV_CHECK(contains(render_frame(app, 80, 24), "assembled"));
    app.editor().on_key(Key::character('x'));
    RV_CHECK(contains(render_frame(app, 80, 24), "not assembled"));
}

RV_TEST(editor, f2_moves_between_editing_and_debugging) {
    dbg::DebugSession session = make_session("_start: li a0, 1\n        ebreak\n");
    App app(session);
    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Debug));

    app.handle_key(Key::special(KeyCode::F2));
    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Edit));
    RV_CHECK(contains(render_frame(app, 80, 24), "EDIT"));

    app.handle_key(Key::special(KeyCode::F2));
    RV_CHECK_EQ(static_cast<int>(app.mode()), static_cast<int>(AppMode::Debug));
}

RV_TEST(editor, breakpoints_set_while_editing_survive_the_rebuild) {
    // This is why breakpoints are keyed by line: the F5 loop reassembles
    // constantly, and losing them each time would make the tool infuriating.
    dbg::DebugSession session = make_session(
        "_start: li a0, 1\n"
        "        li a1, 2\n"
        "        ebreak\n");
    AppOptions options;
    options.start_in_editor = true;
    App app(session, options);

    app.editor().set_cursor(Pos{2, 0});
    app.handle_key(Key::special(KeyCode::F9));
    RV_CHECK_EQ(session.breakpoints().breakpoints().size(), std::size_t{1});

    // Grow line 1 so line 2's address moves, then rebuild.
    app.editor().set_text(
        "_start: li a0, 0x12345\n"
        "        li a1, 2\n"
        "        ebreak\n");
    RV_CHECK(app.assemble_and_load());

    RV_CHECK_EQ(session.breakpoints().breakpoints().size(), std::size_t{1});
    const dbg::Breakpoint& breakpoint = session.breakpoints().breakpoints().front();
    RV_CHECK_EQ(breakpoint.line, 2u);
    RV_CHECK_HEX(breakpoint.addr, 8u);
}

RV_TEST(editor, the_editor_shows_syntax_colours_from_the_assemblers_own_lexer) {
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);
    editor.set_text("loop:   addi a0, a1, 5   # count\n");

    StringCanvas canvas(60, 3);
    editor.draw(Pane(canvas, canvas.bounds()));

    RV_CHECK(canvas.row_has_style(0, StyleId::Label));
    RV_CHECK(canvas.row_has_style(0, StyleId::Mnemonic));
    RV_CHECK(canvas.row_has_style(0, StyleId::Register));
    RV_CHECK(canvas.row_has_style(0, StyleId::Number));
    RV_CHECK(canvas.row_has_style(0, StyleId::Comment));
}

RV_TEST(editor, a_mistyped_mnemonic_stops_being_coloured_as_one) {
    // Because the highlighter looks names up in the same table the assembler
    // uses, `addii` loses its colour the instant the second i is typed.
    dbg::DebugSession session = make_session("nop\n");
    EditorView editor(session);

    editor.set_text("        addi a0, a1, 5\n");
    StringCanvas good(60, 2);
    editor.draw(Pane(good, good.bounds()));
    RV_CHECK(good.row_has_style(0, StyleId::Mnemonic));

    editor.set_text("        addii a0, a1, 5\n");
    StringCanvas bad(60, 2);
    editor.draw(Pane(bad, bad.bounds()));
    RV_CHECK(!bad.row_has_style(0, StyleId::Mnemonic));
}
