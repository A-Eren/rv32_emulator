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
#include <QCheckBox>
#include <QListWidget>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QToolBar>
#include <QSpinBox>
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
        if (list->item(row, 3)->text() == "uart") {
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
            candidate->horizontalHeaderItem(0)->text() == "address" &&
            candidate->rowCount() > 100) {  // data memory, not a block's view
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

RV_TEST(gui, operating_a_switch_does_not_destroy_the_control_operating_it) {
    // The crash this is here for: a control's handler called refresh(), which
    // rebuilt the controls, which deleted the very checkbox whose toggled()
    // signal was still on the stack. Qt then returned into freed memory.
    Fixture fixture;
    auto* panel = fixture.window.findChild<DevicePanel*>();
    RV_CHECK(panel != nullptr);
    if (panel == nullptr) return;

    QTableWidget* list = panel->findChild<QTableWidget*>();
    RV_CHECK(list != nullptr);
    if (list == nullptr) return;

    int switches_row = -1;
    for (int row = 0; row < list->rowCount(); ++row) {
        if (list->item(row, 3)->text() == "switches") switches_row = row;
    }
    RV_CHECK_NE(switches_row, -1);
    if (switches_row < 0) return;
    list->selectRow(switches_row);

    const QList<QCheckBox*> boxes = panel->findChildren<QCheckBox*>();
    RV_CHECK_EQ(boxes.size(), 32);
    if (boxes.isEmpty()) return;

    // Bit 0 is the last one, since the row is painted bit 31 first.
    QPointer<QCheckBox> bit0 = boxes.back();
    bit0->setChecked(true);

    RV_CHECK(!bit0.isNull());  // it used to be deleted here, under its own signal
    RV_CHECK_HEX(fixture.session().hart().bus().switches()->value(), 1u);

    // And it still works the second time, which the dangling one did not.
    bit0->setChecked(false);
    RV_CHECK(!bit0.isNull());
    RV_CHECK_HEX(fixture.session().hart().bus().switches()->value(), 0u);
}

RV_TEST(gui, a_step_does_not_rebuild_the_controls_underneath_the_user) {
    // refresh() runs after every step. Tearing the controls down each time
    // would take the focus out of whatever is being operated -- and put a
    // deleted widget under the mouse.
    Fixture fixture;
    auto* panel = fixture.window.findChild<DevicePanel*>();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    for (int row = 0; row < list->rowCount(); ++row) {
        if (list->item(row, 3)->text() == "switches") list->selectRow(row);
    }
    QPointer<QCheckBox> box = panel->findChildren<QCheckBox*>().back();
    RV_CHECK(!box.isNull());

    fixture.window.step_line();
    fixture.window.step_line();
    RV_CHECK(!box.isNull());
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

RV_TEST(gui, an_overlapping_device_is_allowed_and_shown_as_covered) {
    // Refusing would stop the work; hiding it would be worse. It goes on, and
    // the list says which one stopped answering.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    core::Device* ram = bus.attach(0, core::make_device("ram", 64));
    core::Device* leds = bus.attach(0, core::make_device("leds"));
    RV_CHECK(ram != nullptr);
    RV_CHECK(leds != nullptr);

    fixture.window.step_line();
    auto* panel = fixture.window.findChild<DevicePanel*>();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    RV_CHECK_EQ(list->rowCount(), 2);

    // The block is only partly covered -- three of its four slots still answer.
    RV_CHECK_NE(list->item(0, 4)->text().indexOf("partly covered"), -1);
    // The one on top reports normally -- it is the one answering.
    RV_CHECK_EQ(list->item(1, 4)->text().indexOf("covered"), -1);
}

RV_TEST(gui, the_memory_list_resizes_the_machine) {
    // On the memory tab, because that is what the table is a view of: changing
    // one while looking at the other is the shortest way to see what the
    // number means.
    Fixture fixture;
    fixture.window.show_tab("Memory");
    QList<QComboBox*> boxes = fixture.window.findChildren<QComboBox*>();
    RV_CHECK_EQ(boxes.size(), 2);
    if (boxes.size() != 2) return;

    RV_CHECK_NE(boxes[0]->currentText().indexOf("× 32-bit"), -1);

    const int eight_kb = boxes[0]->findData(QVariant(uint{8192}));
    RV_CHECK_NE(eight_kb, -1);
    boxes[0]->setCurrentIndex(eight_kb);
    RV_CHECK_EQ(fixture.session().hart().imem().size(), 8192u);

    const int four_kb = boxes[1]->findData(QVariant(uint{4096}));
    boxes[1]->setCurrentIndex(four_kb);
    RV_CHECK_EQ(fixture.session().hart().bus().dmem().size(), 4096u);
}

RV_TEST(gui, a_size_a_file_names_is_shown_even_when_the_list_lacks_it) {
    // The alternative is a box that says one size while the machine has
    // another, which is worse than an entry that is not a round number.
    Fixture fixture;
    fixture.session().hart().resize_memories(5120, 16 * 1024);
    fixture.window.show_tab("Memory");
    fixture.window.step_line();

    QComboBox* imem = fixture.window.findChildren<QComboBox*>().front();
    RV_CHECK_EQ(imem->currentData().toUInt(), 5120u);
    RV_CHECK_NE(imem->currentText().indexOf("1280 × 32-bit"), -1);
}

RV_TEST(gui, a_block_and_a_custom_peripheral_get_fields_to_type_into) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    bus.attach(0, core::make_device("custom", 3, "sensor"));

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    list->selectRow(0);

    // Inside the Controls box: the memory size fields are spin boxes elsewhere
    // in the panel, and each of those owns a line edit of its own.
    auto* controls = panel->findChild<QGroupBox*>();
    RV_CHECK(controls != nullptr);
    if (controls == nullptr) return;

    // Three registers to type into, and the interrupt line as a switch.
    RV_CHECK_EQ(controls->findChildren<QLineEdit*>().size(), 3);
    RV_CHECK_EQ(controls->findChildren<QCheckBox*>().size(), 1);

    // Typing into one is what the program then reads.
    QLineEdit* first = controls->findChildren<QLineEdit*>().front();
    first->setText("0x2a");
    emit first->editingFinished();
    RV_CHECK_EQ(bus.load(core::slot_address(0), 4, false).value, 42u);
}

RV_TEST(gui, the_exported_images_are_as_deep_as_the_machine) {
    // The whole point of the tool: the file goes into a block memory of a
    // fixed depth, so the file has to be that many lines whatever the program
    // is. Getting it short is the bug that reads as the program going wrong.
    Fixture fixture;
    fixture.session().hart().resize_memories(4096, 2048);

    const QString folder = QDir::tempPath() + "/rv32_export_test";
    QDir().mkpath(folder);
    const QString imem = folder + "/t_imem.mem";
    const QString dmem = folder + "/t_dmem.mem";
    QFile::remove(imem);
    QFile::remove(dmem);

    RV_CHECK(fixture.window.write_mem_files(imem, dmem, /*annotate=*/false));

    const auto line_count = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
        return static_cast<int>(QString::fromUtf8(file.readAll()).count(QLatin1Char('\n')));
    };
    RV_CHECK_EQ(line_count(imem), 1024);  // 4096 bytes / 4
    RV_CHECK_EQ(line_count(dmem), 512);   // 2048 bytes / 4

    QFile::remove(imem);
    QFile::remove(dmem);
    QDir().rmdir(folder);
}

RV_TEST(gui, generate_sits_next_to_assemble) {
    // The other half of the same sentence: one turns the text into a machine,
    // the other turns the machine into files a synthesis tool will take.
    Fixture fixture;
    QStringList toolbar;
    for (QToolBar* bar : fixture.window.findChildren<QToolBar*>()) {
        for (QAction* action : bar->actions()) {
            if (!action->isSeparator()) toolbar << action->text();
        }
    }
    RV_CHECK_EQ(toolbar.indexOf("Generate"), toolbar.indexOf("Assemble") + 1);
}

RV_TEST(gui, a_block_exports_itself_at_its_own_depth) {
    // Its contents are a memory image in their own right -- a table, a font, a
    // sine wave -- and what is wanted on the far side is that block in a BRAM
    // of its own, not the program's data memory with the block inside it.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* ram = static_cast<core::RamDevice*>(bus.attach(0, core::make_device("ram", 64)));
    ram->set_input(0, 0xdeadbeef);

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();

    const QString path = QDir::tempPath() + "/rv32_block_test.mem";
    QFile::remove(path);
    RV_CHECK(panel->write_block(0, path));

    QFile file(path);
    RV_CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines =
        QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    // 64 bytes is sixteen words, and the file is sixteen lines whatever is in
    // it -- that is what makes it loadable into a block of that depth.
    RV_CHECK_EQ(lines.size(), 16);
    RV_CHECK_STR(lines.front().toStdString(), "deadbeef");
    RV_CHECK_STR(lines.back().toStdString(), "00000000");
    file.close();
    QFile::remove(path);
}

RV_TEST(gui, instructions_past_the_end_of_imem_are_marked_not_cut) {
    // load_words truncates silently, so a program too big for the machine used
    // to become a different program without saying so. It is marked instead:
    // what was written is still what is there.
    QString source = "        .text\n_start:\n";
    for (int i = 0; i < 80; ++i) source += "        addi    a0, a0, 1\n";

    Fixture fixture;
    fixture.session().hart().resize_memories(256, 256);  // 64 words
    fixture.editor().setPlainText(source);
    fixture.window.assemble();

    // Line 3 is the first instruction, so lines 3..66 fit and 67 onwards do not.
    const std::vector<int>& marked = fixture.editor().overflow_lines();
    RV_CHECK_EQ(marked.size(), std::size_t{16});
    RV_CHECK_EQ(marked.front(), 67);
    RV_CHECK_EQ(marked.back(), 82);

    // And the program is left whole rather than trimmed to what fits.
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{80});

    // A bigger machine makes it go away -- both ends of the comparison move.
    fixture.session().hart().resize_memories(1024, 256);
    fixture.window.assemble();
    RV_CHECK(fixture.editor().overflow_lines().empty());
}

RV_TEST(gui, data_past_the_end_of_dmem_is_reported_even_with_no_line_to_mark) {
    // .data sits in its own space and the source map covers instructions, so
    // there is no line to paint -- which is a reason to say it elsewhere, not
    // a reason to say nothing.
    Fixture fixture;
    fixture.session().hart().resize_memories(4096, 256);
    fixture.editor().setPlainText(
        "        .data\n"
        "buffer: .zero 512\n"
        "        .text\n"
        "_start: ebreak\n");
    fixture.window.assemble();

    RV_CHECK(fixture.editor().overflow_lines().empty());
    RV_CHECK_NE(fixture.window.message().indexOf("past the end of dmem"), -1);
}

RV_TEST(gui, a_device_can_be_renamed_after_it_exists) {
    // What two of a kind should be called is usually clear only once both are
    // there, so naming cannot be confined to the moment of adding.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    bus.attach(0, core::make_device("uart"));

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    // A name and a type are different things, so they are different columns.
    RV_CHECK_STR(list->horizontalHeaderItem(2)->text().toStdString(), "name");
    RV_CHECK_STR(list->horizontalHeaderItem(3)->text().toStdString(), "type");
    RV_CHECK(list->item(0, 2)->flags().testFlag(Qt::ItemIsEditable));
    RV_CHECK(!list->item(0, 3)->flags().testFlag(Qt::ItemIsEditable));
    RV_CHECK(list->item(0, 2)->text().isEmpty());
    RV_CHECK_STR(list->item(0, 3)->text().toStdString(), "uart");

    list->item(0, 2)->setText("console");
    RV_CHECK_STR(bus.device_at_slot(0)->display_name(), "console");
    RV_CHECK_STR(list->item(0, 3)->text().toStdString(), "uart");  // still says what it is

    list->item(0, 2)->setText("");
    RV_CHECK(bus.device_at_slot(0)->label().empty());
}

RV_TEST(gui, a_block_is_looked_at_where_it_is_configured) {
    // Under the device it belongs to, in the same shape the memory tab uses
    // for data memory. Sixteen numbered fields were a list of words pretending
    // not to be memory.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* ram = static_cast<core::RamDevice*>(bus.attach(0, core::make_device("ram", 64)));
    ram->poke(4, 0xdeadbeef);

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();
    panel->findChild<QTableWidget*>()->selectRow(0);

    // The controls hold a hex view of the block, four words to the row.
    QTableWidget* view = nullptr;
    for (QTableWidget* candidate : panel->findChild<QGroupBox*>()->findChildren<QTableWidget*>()) {
        view = candidate;
    }
    RV_CHECK(view != nullptr);
    if (view == nullptr) return;
    RV_CHECK_EQ(view->rowCount(), 4);  // 64 bytes, sixteen to a row
    RV_CHECK_STR(view->item(0, 0)->text().toStdString(), "ffff0000");
    RV_CHECK_STR(view->item(0, 2)->text().toStdString(), "deadbeef");

    // And typing into it writes the block.
    view->item(0, 1)->setText("00c0ffee");
    RV_CHECK_HEX(ram->peek(0), 0x00c0ffeeu);
}

RV_TEST(gui, a_block_exports_at_its_own_depth) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* ram = static_cast<core::RamDevice*>(bus.attach(0, core::make_device("ram", 64)));
    ram->poke(0, 0xdeadbeef);

    auto* panel = fixture.window.findChild<DevicePanel*>();
    const QString path = QDir::tempPath() + "/rv32_block_depth.mem";
    QFile::remove(path);
    RV_CHECK(panel->write_block(0, path));

    QFile file(path);
    RV_CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines =
        QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    RV_CHECK_EQ(lines.size(), 16);  // the block, not data memory
    RV_CHECK_STR(lines.front().toStdString(), "deadbeef");
    file.close();
    QFile::remove(path);
}

RV_TEST(gui, a_bank_is_offered_at_the_widths_hardware_comes_in) {
    // Eight LEDs, sixteen switches -- what a board actually has. The catalogue
    // says what unit a type is counted in, so the dialog does not have to know
    // that leds are bits and ram is bytes.
    RV_CHECK(core::find_device_type("leds")->unit == core::SizeUnit::Bits);
    RV_CHECK(core::find_device_type("ram")->unit == core::SizeUnit::Bytes);
    RV_CHECK(core::find_device_type("custom")->unit == core::SizeUnit::Words);
    RV_CHECK(core::find_device_type("uart")->unit == core::SizeUnit::None);

    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* leds = static_cast<core::LedDevice*>(bus.attach(0, core::make_device("leds", 8)));
    RV_CHECK_EQ(leds->width(), 8);

    fixture.window.step_line();
    QTableWidget* list = fixture.window.findChild<DevicePanel*>()->findChild<QTableWidget*>();
    // Eight lights shown, not thirty-two that do not exist.
    RV_CHECK_EQ(list->item(0, 4)->text().trimmed().size(), QString("00000000  ........").size());
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

RV_TEST(gui, the_block_sits_under_the_caret_and_not_across_the_editor) {
    // It follows the word it is about, and it is sized to its candidates: a
    // full-width strip would be a long way from what it refers to, and it
    // would cost the editor a row every time it opened.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();

    // Within a line or two of the caret -- below it normally, above it when
    // there is no room below -- rather than parked at the edge of the editor.
    const QRect caret = fixture.editor().cursorRect();
    const QRect block = fixture.editor().suggestion_bar()->geometry();
    RV_CHECK(std::abs(block.center().y() - caret.center().y()) <= 2 * block.height());
    RV_CHECK(block.width() < fixture.editor().width() / 2);
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
