#include "gui/main_window.hpp"

#include "core/memfile.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "asm/assembler.hpp"
#include "gui/editor_widget.hpp"
#include "gui/device_panel.hpp"
#include "gui/encoding_widget.hpp"
#include "isa/disasm.hpp"
#include "isa/regnames.hpp"

namespace rv::gui {
namespace {

/// Instructions per slice while running. Bounds worst-case UI latency while
/// keeping everything on one thread -- speed is not a goal here, and a worker
/// thread would buy a class of nondeterministic bugs for nothing.
constexpr u64 kRunChunk = 20000;

const QColor kChangedColour(0xe8, 0xc0, 0x4e);
const QColor kZeroColour(0x6a, 0x71, 0x7c);
const QColor kNormalColour(0xd6, 0xd6, 0xd6);

QFont mono(int size = 10) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(size);
    return font;
}

QString from(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), session_(std::make_unique<dbg::DebugSession>()) {
    setWindowTitle("rv32");
    resize(1280, 820);

    build_layout();
    build_actions();

    run_timer_ = new QTimer(this);
    run_timer_->setInterval(0);
    connect(run_timer_, &QTimer::timeout, this, &MainWindow::on_run_slice);

    refresh_all();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::build_layout() {
    editor_ = new EditorWidget(this);
    connect(editor_, &EditorWidget::breakpointToggled, this, &MainWindow::toggle_breakpoint);
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::on_cursor_moved);

    // Registers: both spellings, because the encoding holds the number and real
    // assembly is written with the name.
    registers_ = new QTableWidget(kNumRegs, 3, this);
    registers_->setHorizontalHeaderLabels({"reg", "hex", "signed"});
    registers_->verticalHeader()->setVisible(false);
    registers_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    registers_->setSelectionMode(QAbstractItemView::NoSelection);
    registers_->setFont(mono());
    registers_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // Compact rows: 32 registers at the default height need a lot of scrolling,
    // and the point of the panel is to take them in at a glance.
    registers_->verticalHeader()->setDefaultSectionSize(
        QFontMetrics(mono()).height() + 4);
    registers_->setShowGrid(false);

    encoding_ = new EncodingWidget(*session_, this);

    const auto make_text = [this] {
        auto* view = new QPlainTextEdit(this);
        view->setReadOnly(true);
        view->setFont(mono());
        view->setLineWrapMode(QPlainTextEdit::NoWrap);
        return view;
    };
    console_ = make_text();

    // An editable table rather than a text dump: being able to type a value
    // into memory while stopped is most of what makes poking at a program
    // useful, and a read-only hex block cannot offer it.
    memory_ = new QTableWidget(32, 5, this);
    memory_->setHorizontalHeaderLabels({"address", "+0", "+4", "+8", "+c"});
    memory_->verticalHeader()->setVisible(false);
    memory_->setFont(mono());
    memory_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    memory_->verticalHeader()->setDefaultSectionSize(QFontMetrics(mono()).height() + 4);
    connect(memory_, &QTableWidget::cellChanged, this, &MainWindow::on_memory_edited);

    devices_ = new DevicePanel(*session_, this);
    connect(devices_, &DevicePanel::devicesChanged, this, &MainWindow::refresh_all);
    connect(devices_, &DevicePanel::inputChanged, this, &MainWindow::refresh_all);
    // Resizing a memory throws away what no longer fits, so the program that
    // was loaded is no longer necessarily the program in imem. Say so rather
    // than let the next step run off the end of a truncated one.
    connect(devices_, &DevicePanel::exportRequested, this, &MainWindow::on_export_mem);
    connect(devices_, &DevicePanel::memoryResized, this, [this] {
        status_->setText("Memory resized — press F5 to assemble into the new machine.");
        refresh_all();
    });

    symbols_ = new QTableWidget(0, 4, this);
    symbols_->setHorizontalHeaderLabels({"name", "space", "value", "line"});
    symbols_->verticalHeader()->setVisible(false);
    symbols_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    symbols_->setFont(mono());
    symbols_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    dock_ = new QTabWidget(this);
    dock_->addTab(encoding_, "Encoding");
    dock_->addTab(memory_, "Memory");
    dock_->addTab(console_, "Console");
    dock_->addTab(devices_, "Devices");
    dock_->addTab(symbols_, "Symbols");

    diagnostics_ = new QListWidget(this);
    diagnostics_->setFont(mono());
    // The hints are the useful half of a diagnostic and they are often long;
    // truncating them would throw away the part that tells you what to do.
    diagnostics_->setWordWrap(true);
    connect(diagnostics_, &QListWidget::currentRowChanged, this,
            &MainWindow::on_diagnostic_activated);

    auto* right = new QSplitter(Qt::Vertical, this);
    right->addWidget(registers_);
    right->addWidget(dock_);
    right->setSizes({320, 420});

    auto* top = new QSplitter(Qt::Horizontal, this);
    top->addWidget(editor_);
    top->addWidget(right);
    // Explicit sizes rather than stretch factors: the register table's size
    // hint is wide enough to squeeze the editor down to a column of fragments,
    // and reading the code is the whole point.
    top->setSizes({760, 520});

    whole_ = new QSplitter(Qt::Vertical, this);
    whole_->addWidget(top);
    whole_->addWidget(diagnostics_);
    whole_->setSizes({740, 0});  // the list opens only when there is something in it

    setCentralWidget(whole_);

    status_ = new QLabel(this);
    message_ = new QLabel(this);
    status_->setFont(mono());
    statusBar()->addWidget(message_, 1);
    statusBar()->addPermanentWidget(status_);
}

void MainWindow::build_actions() {
    auto* file_menu = menuBar()->addMenu("&File");
    file_menu->addAction("&New", QKeySequence::New, this, &MainWindow::on_new_file);
    file_menu->addAction("&Open…", QKeySequence::Open, this, &MainWindow::on_open_file);
    file_menu->addAction("&Save", QKeySequence::Save, this, &MainWindow::on_save);
    file_menu->addAction("Save &As…", QKeySequence::SaveAs, this, &MainWindow::on_save_as);
    file_menu->addSeparator();
    file_menu->addAction("Load &peripherals…", this, &MainWindow::on_load_devices);
    file_menu->addAction("Save p&eripherals…", this, &MainWindow::on_save_devices);
    file_menu->addSeparator();
    file_menu->addAction("&Export .mem…", QKeySequence("Ctrl+E"), this,
                         &MainWindow::on_export_mem);
    file_menu->addSeparator();
    file_menu->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    auto* toolbar = addToolBar("Run");
    toolbar->setMovable(false);

    // The same keys as the terminal front-end, so switching between them does
    // not mean relearning anything.
    const auto add = [&](const QString& text, const QKeySequence& key, auto slot,
                         const QString& tip) {
        QAction* action = toolbar->addAction(text);
        action->setShortcut(key);
        action->setToolTip(tip + "  (" + key.toString() + ")");
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    add("Assemble", Qt::Key_F5, &MainWindow::assemble, "Assemble the buffer and load it");
    toolbar->addSeparator();
    add("Step", Qt::Key_F8, &MainWindow::step_line, "One source line, entering calls");
    add("Step Instr", Qt::Key_F7, &MainWindow::step_instruction,
        "One machine instruction — shows what a pseudo-instruction expands to");
    add("Step Over", Qt::Key_F10, &MainWindow::step_over, "One source line, running calls");
    action_back_ = add("Back", Qt::Key_F6, &MainWindow::step_back, "Undo one source line");
    add("Run", Qt::Key_F9, &MainWindow::run, "Run until something stops it");
    add("Reset", Qt::Key_F4, &MainWindow::reset_machine, "Restart, keeping breakpoints");

    auto* run_menu = menuBar()->addMenu("&Run");
    for (QAction* action : toolbar->actions()) {
        if (!action->isSeparator()) run_menu->addAction(action);
    }
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool MainWindow::open_file(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        set_message("cannot open " + path, true);
        return false;
    }
    QTextStream stream(&file);
    editor_->setPlainText(stream.readAll());
    editor_->document()->setModified(false);
    path_ = path;
    setWindowTitle("rv32 — " + QFileInfo(path).fileName());
    return assemble(), session_->has_program();
}

void MainWindow::on_new_file() {
    if (!confirm_discard()) return;
    editor_->setPlainText("        .text\n        .globl _start\n_start:\n        li a0, 1\n"
                          "        ebreak\n");
    editor_->document()->setModified(false);
    path_.clear();
    setWindowTitle("rv32 — untitled");
    assemble();
}

void MainWindow::on_open_file() {
    if (!confirm_discard()) return;
    const QString path =
        QFileDialog::getOpenFileName(this, "Open assembly", {}, "Assembly (*.s *.S *.asm)");
    if (!path.isEmpty()) open_file(path);
}

void MainWindow::on_save() {
    if (path_.isEmpty()) {
        on_save_as();
        return;
    }
    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        set_message("cannot write " + path_, true);
        return;
    }
    QTextStream(&file) << editor_->toPlainText();
    editor_->document()->setModified(false);
    set_message("saved " + path_);
}

void MainWindow::on_save_as() {
    const QString path =
        QFileDialog::getSaveFileName(this, "Save assembly", path_, "Assembly (*.s)");
    if (path.isEmpty()) return;
    path_ = path;
    setWindowTitle("rv32 — " + QFileInfo(path).fileName());
    on_save();
}

bool MainWindow::confirm_discard() {
    if (!editor_->document()->isModified()) return true;
    const auto answer =
        QMessageBox::question(this, "rv32", "The buffer has unsaved changes. Discard them?",
                              QMessageBox::Discard | QMessageBox::Cancel);
    return answer == QMessageBox::Discard;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

void MainWindow::assemble() {
    run_timer_->stop();

    const QByteArray text = editor_->toPlainText().toUtf8();
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;  // the diagnostics list has no room for prose

    const std::string name =
        path_.isEmpty() ? std::string("<editor>") : path_.toStdString();

    // Assemble a detached copy first, so a broken edit never destroys the
    // program that is currently loaded and steppable.
    const as::SourceFile candidate(name, text.toStdString());
    as::AssembledProgram attempt = as::assemble(candidate, options);

    editor_->set_diagnostics(attempt.diagnostics);
    last_diagnostics_ = attempt.diagnostics;
    assembled_revision_ = editor_->document()->revision();

    if (!attempt.ok()) {
        refresh_diagnostics();
        if (const as::Diagnostic* first = attempt.diagnostics.first_error()) {
            editor_->go_to_line(static_cast<int>(first->primary.line));
            set_message(from(first->message), true);
        }
        refresh_status();
        return;
    }

    session_->load_source(text.toStdString(), name, options);
    // The completer offers this program's labels as branch and load targets.
    editor_->set_symbols(&session_->symbols());
    encoding_->follow_pc();
    set_message(QString::asprintf("assembled: %zu instructions, %zu bytes of data",
                                  session_->program().imem_words.size(),
                                  session_->program().dmem_bytes.size()));
    refresh_all();
}

void MainWindow::report_stop(const dbg::StopEvent& event) {
    switch (event.reason) {
        case dbg::StopReason::Step: message_text_.clear(); break;
        case dbg::StopReason::Breakpoint:
            set_message(QString::asprintf("breakpoint %u at line %u", event.breakpoint_id,
                                          event.line.value_or(0)));
            break;
        case dbg::StopReason::Watchpoint:
            set_message(QString::asprintf("watchpoint at 0x%08x: 0x%08x → 0x%08x",
                                          event.watch_addr, event.watch_old, event.watch_new));
            break;
        case dbg::StopReason::Halted:
            set_message(QString("stopped: ") + core::halt_reason_name(event.halt));
            break;
        case dbg::StopReason::Trap:
            set_message(QString("trap: ") + core::trap_cause_name(event.cause), true);
            break;
        case dbg::StopReason::Watchdog:
            set_message("stopped after the instruction budget — infinite loop?", true);
            break;
        case dbg::StopReason::NotRunning: set_message("no program loaded", true); break;
        case dbg::StopReason::HistoryHorizon:
            set_message("no further history: this is as far back as the recording goes", true);
            break;
    }
    if (event.reason != dbg::StopReason::Step) run_timer_->stop();
    refresh_all();
}

void MainWindow::step_line() { report_stop(session_->step(dbg::StepKind::Line)); }
void MainWindow::step_instruction() { report_stop(session_->step(dbg::StepKind::Instruction)); }
void MainWindow::step_over() { report_stop(session_->step(dbg::StepKind::LineOver)); }

void MainWindow::step_back() {
    if (!session_->can_reverse()) {
        set_message("no further history", true);
        return;
    }
    report_stop(session_->reverse_step(dbg::StepKind::Line));
}

void MainWindow::run() {
    if (run_timer_->isActive()) {
        run_timer_->stop();
        set_message("interrupted");
        refresh_all();
        return;
    }
    if (!session_->has_program()) {
        set_message("no program loaded", true);
        return;
    }
    message_text_.clear();
    run_timer_->start();
}

void MainWindow::on_run_slice() {
    if (const auto event = session_->run_chunk(kRunChunk)) {
        report_stop(*event);
        return;
    }
    // Still going: keep the panels current so a long run is watchable.
    refresh_registers();
    refresh_status();
}

void MainWindow::reset_machine() {
    run_timer_->stop();
    session_->reset();
    encoding_->follow_pc();
    set_message("machine reset; breakpoints kept");
    refresh_all();
}

void MainWindow::toggle_breakpoint(int line) {
    if (!session_->has_program()) return;
    const bool added =
        session_->breakpoints().toggle_line(static_cast<u32>(line), session_->map());
    set_message(added ? QString::asprintf("breakpoint set on line %d", line)
                      : QString::asprintf("breakpoint cleared on line %d", line));
    refresh_all();
}

// ---------------------------------------------------------------------------
// Refreshing
// ---------------------------------------------------------------------------

void MainWindow::refresh_all() {
    refresh_registers();
    refresh_memory();
    refresh_console();
    refresh_symbols();
    if (devices_ != nullptr) devices_->refresh();
    refresh_diagnostics();
    refresh_status();

    std::vector<int> breakpoints;
    for (const dbg::Breakpoint& breakpoint : session_->breakpoints().breakpoints()) {
        if (breakpoint.enabled) breakpoints.push_back(static_cast<int>(breakpoint.line));
    }
    editor_->set_breakpoint_lines(std::move(breakpoints));
    editor_->set_execution_line(static_cast<int>(session_->current_line().value_or(0)));

    action_back_->setEnabled(session_->can_reverse());
    encoding_->update();
}

void MainWindow::refresh_registers() {
    const auto changed = [this](RegIdx reg) {
        for (const dbg::RegChange& change : session_->last_changes()) {
            if (change.index == reg) return true;
        }
        return false;
    };

    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        const u32 value = session_->cpu().x[reg];
        const bool just_changed = changed(reg);

        const auto set = [&](int column, const QString& text, const QColor& colour) {
            QTableWidgetItem* item = registers_->item(reg, column);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                registers_->setItem(reg, column, item);
            }
            item->setText(text);
            item->setForeground(colour);
            QFont font = mono();
            font.setBold(just_changed);
            item->setFont(font);
        };

        // Dimming the zeros is what makes the two or three registers a small
        // program actually uses stand out of a wall of 32.
        const QColor colour = just_changed ? kChangedColour
                                           : (value == 0 ? kZeroColour : kNormalColour);
        set(0, from(isa::numeric_name(reg)) + "  " + from(isa::abi_name(reg)), colour);
        set(1, QString::asprintf("%08x", value), colour);
        set(2, QString::number(static_cast<i32>(value)), colour);
    }

    // Scroll the register that just changed into view. Without this the whole
    // highlight is wasted whenever the write lands on one of the high
    // registers, which is exactly when you most want to see it.
    for (const dbg::RegChange& change : session_->last_changes()) {
        if (QTableWidgetItem* item = registers_->item(change.index, 0)) {
            registers_->scrollToItem(item, QAbstractItemView::EnsureVisible);
        }
    }
}

void MainWindow::refresh_memory() {
    const Addr highlight = session_->last_step().mem_written;
    // Follow a store, but only when it landed outside the window already on
    // screen: jumping on every store would make a loop unreadable.
    if (highlight != core::kNoAddr &&
        (highlight < memory_base_ || highlight >= memory_base_ + 32 * 16)) {
        memory_base_ = highlight >= 0x80u ? (highlight & ~0xfu) - 0x80u : 0u;
    }

    // Blocking keeps the programmatic fill from being mistaken for the user
    // typing, which would write memory back to itself on every step.
    const QSignalBlocker blocker(memory_);
    for (int row = 0; row < memory_->rowCount(); ++row) {
        const Addr address = memory_base_ + static_cast<Addr>(row * 16);

        const auto set = [&](int column, const QString& text, bool editable, bool changed) {
            QTableWidgetItem* item = memory_->item(row, column);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                memory_->setItem(row, column, item);
            }
            item->setText(text);
            item->setFlags(editable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                                    : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
            item->setForeground(changed ? kChangedColour
                                        : (editable ? kNormalColour : kZeroColour));
        };

        set(0, QString::asprintf("%08x", address), false, false);
        for (int index = 0; index < 4; ++index) {
            const Addr at = address + static_cast<Addr>(index * 4);
            const u32 value = session_->read_dmem_word(at);
            set(index + 1, QString::asprintf("%08x", value), true, at == highlight);
        }
    }
}

void MainWindow::on_memory_edited(int row, int column) {
    if (column < 1 || column > 4) return;
    QTableWidgetItem* item = memory_->item(row, column);
    if (item == nullptr) return;

    bool valid = false;
    const u32 value = item->text().trimmed().toUInt(&valid, 16);
    if (!valid) {
        set_message("not a hex value: " + item->text(), true);
        refresh_memory();
        return;
    }

    const Addr address =
        memory_base_ + static_cast<Addr>(row * 16) + static_cast<Addr>((column - 1) * 4);
    // poke rather than store: this is the debugger writing, so it must not
    // trigger whatever a device would do on a write from the program.
    session_->hart().bus().poke_word(address, value);
    set_message(QString::asprintf("[0x%08x] = 0x%08x", address, value));
    refresh_all();
}

void MainWindow::refresh_console() {
    const QString text = QString::fromStdString(session_->uart_output());
    console_->setPlainText(text.isEmpty() ? "(nothing written to the uart yet)" : text);
}

void MainWindow::refresh_symbols() {
    const auto& symbols = session_->symbols().all();
    symbols_->setRowCount(static_cast<int>(symbols.size()));
    for (int row = 0; row < static_cast<int>(symbols.size()); ++row) {
        const as::Symbol& symbol = symbols[static_cast<std::size_t>(row)];
        const QStringList cells = {QString::fromStdString(symbol.name),
                                   as::space_name(symbol.space),
                                   QString::asprintf("0x%08x", symbol.value),
                                   QString::number(symbol.definition.line)};
        for (int column = 0; column < cells.size(); ++column) {
            auto* item = new QTableWidgetItem(cells[column]);
            // Colour by space: this is the distinction that explains why
            // jumping to a data symbol gets rejected.
            item->setForeground(symbol.space == as::Space::Data ? QColor(0xe8, 0xa0, 0x4e)
                                                                : QColor(0x89, 0xd1, 0x85));
            symbols_->setItem(row, column, item);
        }
    }
}

void MainWindow::refresh_diagnostics() {
    diagnostics_->clear();
    // Give the list room only when it has something to say; an empty pane at
    // the bottom of the window is pure waste.
    const bool any = !last_diagnostics_.items().empty();
    if (whole_ != nullptr) whole_->setSizes(any ? QList<int>{600, 160} : QList<int>{740, 0});
    for (const as::Diagnostic& diagnostic : last_diagnostics_.items()) {
        QString text = QString::asprintf("%s:%u:%u  ",
                                         QFileInfo(path_).fileName().toUtf8().constData(),
                                         diagnostic.primary.line, diagnostic.primary.col + 1);
        text += as::severity_name(diagnostic.severity);
        if (!diagnostic.code.empty()) text += "[" + from(diagnostic.code) + "]";
        text += ": " + from(diagnostic.message);
        if (!diagnostic.hint.empty()) text += "   — " + from(diagnostic.hint);

        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, diagnostic.primary.line);
        item->setForeground(diagnostic.severity == as::Severity::Error ? QColor(0xf2, 0x6d, 0x6d)
                                                                       : QColor(0xe8, 0xc0, 0x4e));
        diagnostics_->addItem(item);
    }
}

QString MainWindow::status_text() const { return status_->text(); }

bool MainWindow::show_tab(const QString& title) {
    for (int index = 0; index < dock_->count(); ++index) {
        if (dock_->tabText(index).compare(title, Qt::CaseInsensitive) == 0) {
            dock_->setCurrentIndex(index);
            return true;
        }
    }
    return false;
}

void MainWindow::refresh_status() {
    const core::CpuState& cpu = session_->cpu();
    QString text = QString::asprintf("pc %08x   cycle %llu   instret %llu",
                                     cpu.pc, static_cast<unsigned long long>(cpu.cycle),
                                     static_cast<unsigned long long>(cpu.instret));

    // Which word of its source line the pc is on. That [1/2] is how a
    // two-instruction `li` stops being a mystery.
    if (const auto slot = session_->current_slot(); slot && slot->second > 1) {
        text += QString::asprintf("   [%u/%u]", slot->first + 1, slot->second);
    }
    if (session_->halted()) text += "   HALTED";
    if (run_timer_->isActive()) text += "   RUNNING";
    if (editor_->document()->revision() != assembled_revision_) text += "   (not assembled)";

    status_->setText(text);
    message_->setText(message_text_);
    message_->setStyleSheet(message_is_error_ ? "color: #f26d6d;" : "");
}

void MainWindow::set_message(const QString& text, bool is_error) {
    message_text_ = text;
    message_is_error_ = is_error;
    if (message_ != nullptr) {
        message_->setText(text);
        message_->setStyleSheet(is_error ? "color: #f26d6d;" : "");
    }
}

// ---------------------------------------------------------------------------
// Reacting
// ---------------------------------------------------------------------------

void MainWindow::on_diagnostic_activated(int row) {
    if (row < 0) return;
    QListWidgetItem* item = diagnostics_->item(row);
    if (item == nullptr) return;
    editor_->go_to_line(item->data(Qt::UserRole).toInt());
    editor_->setFocus();
}

bool MainWindow::load_devices(const QString& path) {
    if (!devices_->load_config(path)) return false;
    devices_path_ = path;
    const QString warning = devices_->last_warning();
    set_message(warning.isEmpty() ? "machine built from " + path
                                  : "machine built from " + path + " — " + warning);
    refresh_all();
    return true;
}

void MainWindow::on_load_devices() {
    const QString path = QFileDialog::getOpenFileName(this, "Load peripherals", devices_path_,
                                                      "Device configuration (*.toml)");
    if (path.isEmpty()) return;
    load_devices(path);
}

void MainWindow::on_save_devices() {
    const QString path = QFileDialog::getSaveFileName(this, "Save peripherals", devices_path_,
                                                      "Device configuration (*.toml)");
    if (path.isEmpty()) return;
    if (devices_->save_config(path)) {
        devices_path_ = path;
        set_message("peripherals saved to " + path);
    }
}

void MainWindow::on_cursor_moved() {
    // The encoding panel follows the caret, so a line explains itself while it
    // is still being written.
    const int line = editor_->current_line();
    const auto& addrs = session_->map().addrs_for_line(static_cast<u32>(line));
    if (!addrs.empty()) {
        encoding_->set_address(addrs.front());
        if (dock_->currentWidget() != encoding_) encoding_->update();
    }

    if (const as::Diagnostic* diagnostic = editor_->diagnostic_on_line(line)) {
        set_message(from(diagnostic->message), diagnostic->severity == as::Severity::Error);
    }
    refresh_status();
}

namespace {

/// Where the .mem files go and what they are called. It shows the depth of
/// each image beside it, because that is the number the Block Memory Generator
/// asks for on the other side -- getting it wrong is the mistake that produces
/// a memory full of the right words in the wrong places.
class ExportMemDialog final : public QDialog {
public:
    ExportMemDialog(QWidget* parent, u32 imem_bytes, u32 dmem_bytes)
        : QDialog(parent), imem_bytes_(imem_bytes), dmem_bytes_(dmem_bytes) {
        setWindowTitle("Export .mem");
        auto* form = new QFormLayout(this);

        folder_ = new QLineEdit(QDir::currentPath(), this);
        auto* browse = new QPushButton("Browse…", this);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString picked =
                QFileDialog::getExistingDirectory(this, "Export to", folder_->text());
            if (!picked.isEmpty()) folder_->setText(picked);
        });
        auto* folder_row = new QHBoxLayout;
        folder_row->addWidget(folder_, 1);
        folder_row->addWidget(browse);
        form->addRow("Folder", folder_row);

        base_ = new QLineEdit("rv32", this);
        form->addRow("Base name", base_);

        imem_ = new QCheckBox(QString("imem  —  %1 × 32-bit").arg(imem_bytes / 4), this);
        dmem_ = new QCheckBox(QString("dmem  —  %1 × 32-bit").arg(dmem_bytes / 4), this);
        imem_->setChecked(true);
        dmem_->setChecked(true);
        form->addRow("Write", imem_);
        form->addRow("", dmem_);

        annotate_ = new QCheckBox("comment each imem word with its disassembly", this);
        form->addRow("", annotate_);

        auto* note = new QLabel(
            "Each file is padded to the full depth, so $readmemh fills the whole "
            "array and nothing is left holding whatever the tool put there.",
            this);
        note->setWordWrap(true);
        note->setStyleSheet("color: #7d8590;");
        form->addRow("", note);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText("Export");
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    QString imem_path() const { return path_for("imem"); }
    QString dmem_path() const { return path_for("dmem"); }
    bool wants_imem() const { return imem_->isChecked(); }
    bool wants_dmem() const { return dmem_->isChecked(); }
    bool annotate() const { return annotate_->isChecked(); }

private:
    QString path_for(const char* which) const {
        return QDir(folder_->text()).filePath(base_->text() + "_" + which + ".mem");
    }

    u32 imem_bytes_;
    u32 dmem_bytes_;
    QLineEdit* folder_ = nullptr;
    QLineEdit* base_ = nullptr;
    QCheckBox* imem_ = nullptr;
    QCheckBox* dmem_ = nullptr;
    QCheckBox* annotate_ = nullptr;
};

}  // namespace

bool MainWindow::write_mem_files(const QString& imem_path, const QString& dmem_path,
                                 bool annotate) {
    if (!session_->has_program()) {
        set_message("nothing to export — press F5 to assemble first");
        return false;
    }

    const as::AssembledProgram& program = session_->program();
    QStringList written;

    core::MemFileOptions options;
    // Always padded. A .mem shorter than the array it loads into leaves the
    // tail holding whatever the tool put there, and that is the class of bug
    // that looks like the program going wrong rather than the file being short.
    options.pad_to_full = true;

    if (!imem_path.isEmpty()) {
        options.full_size = session_->hart().imem().size() / 4;
        options.annotate = annotate;
        const core::MemFileResult result =
            core::write_mem_file(imem_path.toStdString(), program.imem_words, options);
        if (!result.ok) {
            QMessageBox::warning(this, "rv32", QString::fromStdString(result.error));
            return false;
        }
        written << QFileInfo(imem_path).fileName();
    }

    if (!dmem_path.isEmpty()) {
        options.full_size = session_->hart().bus().dmem().size() / 4;
        options.annotate = false;  // disassembling data would be nonsense
        const std::vector<Word> words = core::pack_bytes_to_words(program.dmem_bytes);
        const core::MemFileResult result =
            core::write_mem_file(dmem_path.toStdString(), words, options);
        if (!result.ok) {
            QMessageBox::warning(this, "rv32", QString::fromStdString(result.error));
            return false;
        }
        written << QFileInfo(dmem_path).fileName();
    }

    if (written.isEmpty()) {
        set_message("nothing selected to export");
        return false;
    }
    set_message("wrote " + written.join(" and ") + " to " +
                QFileInfo(imem_path.isEmpty() ? dmem_path : imem_path).path());
    return true;
}

void MainWindow::on_export_mem() {
    if (!session_->has_program()) {
        set_message("nothing to export — press F5 to assemble first");
        return;
    }

    ExportMemDialog dialog(this, session_->hart().imem().size(),
                           session_->hart().bus().dmem().size());
    if (dialog.exec() != QDialog::Accepted) return;

    write_mem_files(dialog.wants_imem() ? dialog.imem_path() : QString(),
                    dialog.wants_dmem() ? dialog.dmem_path() : QString(), dialog.annotate());
}


}  // namespace rv::gui
