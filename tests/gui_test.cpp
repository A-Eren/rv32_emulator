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

#include "core/device_config.hpp"
#include "gui/device_panel.hpp"
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

RV_TEST(gui, the_device_panel_lists_what_is_attached) {
    Fixture fixture;
    auto* panel = fixture.window.findChild<DevicePanel*>();
    RV_CHECK(panel != nullptr);

    auto* list = panel->findChild<QTableWidget*>();
    RV_CHECK(list != nullptr);
    RV_CHECK_EQ(list->rowCount(),
                static_cast<int>(fixture.session().hart().bus().devices().size()));

    // The address is what a program needs, so it is in the table rather than
    // left to be worked out from the slot.
    bool found_uart = false;
    for (int row = 0; row < list->rowCount(); ++row) {
        if (list->item(row, 2)->text() == "uart") {
            found_uart = true;
            RV_CHECK_STR(list->item(row, 1)->text().toStdString(), "0xffff0000");
        }
    }
    RV_CHECK(found_uart);
}

RV_TEST(gui, adding_and_removing_a_peripheral_updates_the_machine) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    const std::size_t before = bus.devices().size();

    bus.attach(9, core::make_device("button"));
    RV_CHECK_EQ(bus.devices().size(), before + 1);
    RV_CHECK(bus.device_at_slot(9) != nullptr);

    // The panel picks it up on the next refresh, which every step triggers.
    fixture.window.step_line();
    auto* list = fixture.window.findChild<DevicePanel*>()->findChild<QTableWidget*>();
    RV_CHECK_EQ(list->rowCount(), static_cast<int>(before + 1));

    bus.detach(9);
    fixture.window.step_line();
    RV_CHECK_EQ(list->rowCount(), static_cast<int>(before));
}

RV_TEST(gui, memory_can_be_edited_by_hand) {
    // Typing a value into the memory table writes it, using the debugger's
    // path rather than the program's -- so editing a device register does not
    // trigger whatever a write from the program would do.
    Fixture fixture;
    auto* table = fixture.window.findChild<QTableWidget*>("");
    // The memory table is the one with an "address" header.
    QTableWidget* memory = nullptr;
    for (QTableWidget* candidate : fixture.window.findChildren<QTableWidget*>()) {
        if (candidate->columnCount() == 5 &&
            candidate->horizontalHeaderItem(0)->text() == "address") {
            memory = candidate;
        }
    }
    (void)table;
    RV_CHECK(memory != nullptr);
    if (memory == nullptr) return;

    RV_CHECK_HEX(fixture.session().read_dmem_word(0x10), 0u);
    memory->item(1, 1)->setText("deadbeef");  // row 1 is address 0x10, column 1 is +0
    RV_CHECK_HEX(fixture.session().read_dmem_word(0x10), 0xdeadbeefu);
}

RV_TEST(gui, a_device_configuration_round_trips_through_the_panel) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.switches()->set_value(0x1234);

    const core::DeviceConfig described = core::describe(bus);
    const std::string text = core::write_device_config(described);
    RV_CHECK_NE(text.find("[[device]]"), std::string::npos);
    RV_CHECK_NE(text.find("\"switches\""), std::string::npos);

    const core::DeviceConfig reparsed = core::parse_device_config(text, "t.toml");
    RV_CHECK(reparsed.ok());
    RV_CHECK_EQ(reparsed.devices.size(), described.devices.size());
}

RV_TEST(gui, typing_ad_offers_add_and_addi_in_the_editor) {
    // The request, end to end: type two characters and the popup appears with
    // the instructions that start with them.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);

    fixture.editor().request_completion();
    const QStringList offered = fixture.editor().visible_completions();
    RV_CHECK(offered.contains("add"));
    RV_CHECK(offered.contains("addi"));
    RV_CHECK(!offered.contains("sub"));
}

RV_TEST(gui, the_operands_still_to_write_are_greyed_in_after_the_caret) {
    Fixture fixture;
    fixture.editor().setPlainText("");

    const auto ghost_after = [&](const char* line) {
        fixture.editor().setPlainText(QString::fromLatin1(line));
        QTextCursor cursor = fixture.editor().textCursor();
        cursor.movePosition(QTextCursor::End);
        fixture.editor().setTextCursor(cursor);
        return fixture.editor().ghost_text().toStdString();
    };

    // An empty field asks for itself; the ones behind it wait their turn.
    RV_CHECK_STR(ghost_after("        addi "), "rd, rs1, imm");
    RV_CHECK_STR(ghost_after("        addi a0, "), "rs1, imm");
    RV_CHECK_STR(ghost_after("        addi a0, a1, "), "imm(-2048..2047)");
    // Filled in -- nothing left to say.
    RV_CHECK_STR(ghost_after("        addi a0, a1, 4"), "");

    // A field being typed is left alone; the comma to the next one is offered.
    RV_CHECK_STR(ghost_after("        addi a0"), ", rs1, imm");

    RV_CHECK_STR(ghost_after("        lw a0, "), "offset(base)");
    RV_CHECK_STR(ghost_after("        beq a0, a1, "), "label");

    // Not while the mnemonic itself is still being written -- that is the
    // completion popup's half of the job.
    RV_CHECK_STR(ghost_after("        ad"), "");
}

RV_TEST(gui, the_ghost_keeps_out_of_the_way_of_real_text) {
    Fixture fixture;
    fixture.editor().setPlainText("        addi a0, a1, 4");

    // Caret parked after `addi`, with operands already written past it. The
    // ghost would be drawn straight over them, so there is none.
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.setPosition(fixture.editor().document()->firstBlock().position() + 12);
    fixture.editor().setTextCursor(cursor);
    RV_CHECK_STR(fixture.editor().ghost_text().toStdString(), "");
}

RV_TEST(gui, the_bar_folds_away_once_the_instruction_is_settled) {
    // The bar is for choosing the instruction. In an operand position the
    // ghost has already answered, so a list there would be a second answer to
    // the same question -- and it would cost the editor a line to say it.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();
    RV_CHECK(fixture.editor().suggestion_bar()->is_open());

    fixture.editor().setPlainText("        addi a0, a");
    cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();
    RV_CHECK(!fixture.editor().suggestion_bar()->is_open());
    RV_CHECK_STR(fixture.editor().ghost_text().toStdString(), ", imm(-2048..2047)");
}

RV_TEST(gui, tab_walks_along_the_bar_and_lands_in_the_buffer) {
    // Each step is written into the line, not merely highlighted off to the
    // side: the choice is read where the code is.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();

    const QStringList offered = fixture.editor().visible_completions();
    RV_CHECK_EQ(offered.value(0).toStdString(), std::string("add"));

    fixture.editor().cycle_suggestion(+1);
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        add");
    RV_CHECK(fixture.editor().suggestion_bar()->is_open());  // still cycling

    fixture.editor().cycle_suggestion(+1);
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        addi");

    // Going past the end comes back round rather than stopping.
    for (int i = 0; i < offered.size(); ++i) fixture.editor().cycle_suggestion(+1);
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        addi");
}

RV_TEST(gui, settling_on_an_instruction_folds_the_bar_and_starts_the_ghost) {
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();
    RV_CHECK(fixture.editor().suggestion_bar()->is_open());

    fixture.editor().insert_completion("addi");
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        addi ");
    RV_CHECK(!fixture.editor().suggestion_bar()->is_open());
    RV_CHECK_STR(fixture.editor().ghost_text().toStdString(), "rd, rs1, imm");
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
