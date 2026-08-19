// The desktop interface, driven offscreen.
//
// The same bargain the TUI's headless canvas bought: the window is created for
// real, told to do what a person would do, and then asked what it shows. Qt's
// offscreen platform makes this work with no display, so it runs in CI.
//
// Widgets are driven through MainWindow's public actions rather than by
// synthesising clicks, because the actions are what the toolbar and the menu
// call too -- testing them tests the real path.
#include <QApplication>
#include <QListWidget>
#include <QTableWidget>

#include "gui/editor_widget.hpp"
#include "gui/encoding_widget.hpp"
#include "asm/lexer.hpp"
#include "gui/highlighter.hpp"
#include "gui/main_window.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::gui;

namespace {

constexpr const char* kProgram =
    "        .data\n"
    "msg:    .asciz \"hi\"\n"
    "        .text\n"
    "_start: li   a0, 1\n"
    "        li   a1, 0x12345\n"
    "        add  a2, a0, a1\n"
    "        ebreak\n";

/// A window with a program already loaded and assembled.
struct Fixture {
    MainWindow window;

    explicit Fixture(const char* text = kProgram) {
        window.editor().setPlainText(text);
        window.assemble();
    }

    dbg::DebugSession& session() { return window.session(); }
    EditorWidget& editor() { return window.editor(); }
};

QString register_row(MainWindow& window, RegIdx reg, int column) {
    auto* table = window.findChild<QTableWidget*>();
    if (table == nullptr) return {};
    QTableWidgetItem* item = table->item(reg, column);
    return item == nullptr ? QString{} : item->text();
}

QStringList diagnostic_lines(MainWindow& window) {
    QStringList out;
    if (auto* list = window.findChild<QListWidget*>()) {
        for (int row = 0; row < list->count(); ++row) out << list->item(row)->text();
    }
    return out;
}

bool has(const QStringList& lines, const char* needle) {
    for (const QString& line : lines) {
        if (line.contains(needle)) return true;
    }
    return false;
}

}  // namespace

RV_TEST(gui, a_program_assembles_and_loads) {
    Fixture fixture;
    RV_CHECK(fixture.session().has_program());
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{5});
    RV_CHECK(diagnostic_lines(fixture.window).isEmpty());
}

RV_TEST(gui, stepping_updates_the_register_table) {
    Fixture fixture;
    RV_CHECK_STR(register_row(fixture.window, 10, 1).toStdString(), "00000000");

    fixture.window.step_line();
    RV_CHECK_STR(register_row(fixture.window, 10, 1).toStdString(), "00000001");
    // Both spellings, because the encoding holds the number and assembly is
    // written with the name.
    RV_CHECK_STR(register_row(fixture.window, 10, 0).toStdString(), "x10  a0");
    RV_CHECK_STR(register_row(fixture.window, 10, 2).toStdString(), "1");
}

RV_TEST(gui, the_changed_register_is_marked_rather_than_merely_updated) {
    Fixture fixture;
    fixture.window.step_line();

    auto* table = fixture.window.findChild<QTableWidget*>();
    RV_CHECK(table != nullptr);
    // a0 just changed, so it is bold; a1 did not, so it is not.
    RV_CHECK(table->item(10, 1)->font().bold());
    RV_CHECK(!table->item(11, 1)->font().bold());
}

RV_TEST(gui, the_execution_arrow_follows_the_pc) {
    Fixture fixture;
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 4u);

    fixture.window.step_line();
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 5u);
}

RV_TEST(gui, stepping_by_instruction_lands_inside_a_pseudo) {
    // The whole reason the instruction-level step exists: seeing that
    // `li a1, 0x12345` is really two instructions.
    Fixture fixture;
    fixture.window.step_line();  // onto the li

    fixture.window.step_instruction();
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 5u);
    const auto slot = fixture.session().current_slot();
    RV_CHECK(slot.has_value());
    RV_CHECK_EQ(slot->first, 1);
    RV_CHECK_EQ(slot->second, 2);
    RV_CHECK_NE(fixture.window.status_text().indexOf("[2/2]"), -1);
}

RV_TEST(gui, breakpoints_toggle_and_stop_a_run) {
    Fixture fixture;
    fixture.window.toggle_breakpoint(6);
    RV_CHECK_EQ(fixture.session().breakpoints().breakpoints().size(), std::size_t{1});

    // run() starts a timer; drive one slice directly, which is what it does.
    const auto event = fixture.session().run_chunk(100000);
    RV_CHECK(event.has_value());
    RV_CHECK_EQ(static_cast<int>(event->reason), static_cast<int>(dbg::StopReason::Breakpoint));
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 6u);

    fixture.window.toggle_breakpoint(6);
    RV_CHECK(fixture.session().breakpoints().breakpoints().empty());
}

RV_TEST(gui, stepping_backwards_undoes_the_last_line) {
    Fixture fixture;
    fixture.window.step_line();
    fixture.window.step_line();
    RV_CHECK_STR(register_row(fixture.window, 11, 1).toStdString(), "00012345");

    fixture.window.step_back();
    RV_CHECK_STR(register_row(fixture.window, 11, 1).toStdString(), "00000000");
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 5u);
}

RV_TEST(gui, a_failed_assemble_lists_the_errors_and_keeps_the_text) {
    Fixture fixture;
    fixture.editor().setPlainText(
        "_start: nop\n"
        "        addii a0, a1, 5\n"
        "        ebreak\n");
    fixture.window.assemble();

    const QStringList lines = diagnostic_lines(fixture.window);
    RV_CHECK_EQ(lines.size(), 1);
    RV_CHECK(has(lines, "unknown instruction"));
    RV_CHECK(has(lines, "did you mean 'addi'"));  // the hint reaches the list

    // The buffer is untouched and the caret is on the problem.
    RV_CHECK_NE(fixture.editor().toPlainText().indexOf("addii"), -1);
    RV_CHECK_EQ(fixture.editor().current_line(), 2);
    RV_CHECK(fixture.editor().diagnostic_on_line(2) != nullptr);
}

RV_TEST(gui, a_failed_assemble_leaves_the_loaded_program_alone) {
    // Assembling a detached copy means a broken edit never destroys the program
    // that is currently steppable.
    Fixture fixture;
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{5});

    fixture.editor().setPlainText("        addii bad\n");
    fixture.window.assemble();
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{5});
}

RV_TEST(gui, the_harvard_error_reaches_the_diagnostics_list) {
    Fixture fixture;
    fixture.editor().setPlainText(
        "        .data\n"
        "msg:    .asciz \"x\"\n"
        "        .text\n"
        "_start: call msg\n");
    fixture.window.assemble();

    const QStringList lines = diagnostic_lines(fixture.window);
    RV_CHECK(has(lines, "E0310"));
    RV_CHECK(has(lines, "data symbol"));
}

RV_TEST(gui, the_encoding_panel_names_the_fields_of_the_current_instruction) {
    Fixture fixture;
    auto* encoding = fixture.window.findChild<EncodingWidget*>();
    RV_CHECK(encoding != nullptr);

    // The add on line 6.
    const auto& addrs = fixture.session().map().addrs_for_line(6);
    RV_CHECK(!addrs.empty());
    encoding->set_address(addrs.front());

    const QStringList fields = encoding->field_summary();
    RV_CHECK(has(fields, "funct7"));
    RV_CHECK(has(fields, "rs1=x10 (a0)"));
    RV_CHECK(has(fields, "rs2=x11 (a1)"));
    RV_CHECK(has(fields, "rd=x12 (a2)"));
    RV_CHECK(has(fields, "opcode="));
}

RV_TEST(gui, the_encoding_panel_names_the_scrambled_branch_immediate) {
    // The B-type immediate is the one everybody gets wrong; naming the pieces
    // is the point of the panel.
    Fixture fixture(
        "here:   beq a0, a1, here\n"
        "        ebreak\n");
    auto* encoding = fixture.window.findChild<EncodingWidget*>();
    encoding->set_address(0);

    const QStringList fields = encoding->field_summary();
    RV_CHECK(has(fields, "imm[12|10:5]"));
    RV_CHECK(has(fields, "imm[4:1|11]"));
}

RV_TEST(gui, the_encoding_panel_follows_the_caret_before_anything_runs) {
    Fixture fixture;
    auto* encoding = fixture.window.findChild<EncodingWidget*>();

    fixture.editor().go_to_line(6);  // the add
    RV_CHECK(has(encoding->field_summary(), "opcode=0x33 add"));

    fixture.editor().go_to_line(4);  // the first li
    RV_CHECK(has(encoding->field_summary(), "opcode=0x13 addi"));
}

RV_TEST(gui, the_status_line_says_whether_the_buffer_has_been_built) {
    Fixture fixture;
    RV_CHECK_EQ(fixture.window.status_text().indexOf("not assembled"), -1);

    fixture.editor().insertPlainText("        nop\n");
    // The status only refreshes on the events that matter; stepping is one.
    fixture.window.step_line();
    RV_CHECK_NE(fixture.window.status_text().indexOf("not assembled"), -1);
}

RV_TEST(gui, reset_keeps_breakpoints_and_restores_data) {
    Fixture fixture(
        "        .data\n"
        "slot:   .word 0x1234\n"
        "        .text\n"
        "_start: la a1, slot\n"
        "        sw zero, 0(a1)\n"
        "        ebreak\n");
    fixture.window.toggle_breakpoint(5);
    fixture.window.step_line();
    fixture.window.step_line();
    RV_CHECK_HEX(fixture.session().read_dmem_word(0), 0u);

    fixture.window.reset_machine();
    RV_CHECK_HEX(fixture.session().read_dmem_word(0), 0x1234u);
    RV_CHECK_EQ(fixture.session().breakpoints().breakpoints().size(), std::size_t{1});
    RV_CHECK(!fixture.session().can_reverse());
}

RV_TEST(gui, interrupts_are_visible_in_the_io_panel) {
    Fixture fixture(
        "        .equ MTIMECMP, 0xffff0040\n"
        "_start: li t0, MTIMECMP\n"
        "        li t1, 50\n"
        "        sw t1, 0(t0)\n"
        "        ebreak\n");
    for (int i = 0; i < 3; ++i) fixture.window.step_line();

    RV_CHECK(fixture.session().hart().bus().timer()->armed());
    RV_CHECK_HEX(fixture.session().hart().bus().timer()->compare(), 50u);
}

RV_TEST(gui, syntax_roles_come_from_the_assemblers_own_tables) {
    // `addi` is a mnemonic because the instruction table says so, and `addii`
    // stops being one the moment the second i is typed.
    const auto role_of = [](const char* text, std::size_t index) {
        const auto tokens = as::tokenize_for_highlight(text, 1);
        return role_for_token(tokens[index]);
    };
    RV_CHECK_EQ(static_cast<int>(role_of("addi a0, a1, 5", 0)), static_cast<int>(Role::Mnemonic));
    RV_CHECK_EQ(static_cast<int>(role_of("addii a0, a1, 5", 0)), static_cast<int>(Role::Normal));
    RV_CHECK_EQ(static_cast<int>(role_of("addi a0, a1, 5", 1)), static_cast<int>(Role::Register));
    RV_CHECK_EQ(static_cast<int>(role_of("loop:", 0)), static_cast<int>(Role::Label));
    RV_CHECK_EQ(static_cast<int>(role_of(".text", 0)), static_cast<int>(Role::Directive));
    RV_CHECK_EQ(static_cast<int>(role_of("# hi", 0)), static_cast<int>(Role::Comment));
}

int main(int argc, char** argv) {
    // Offscreen so this needs no display, in CI or anywhere else.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return rvtest::run_all(argc, argv);
}
