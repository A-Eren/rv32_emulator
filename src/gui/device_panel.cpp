#include "gui/device_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include "core/device_config.hpp"
#include "core/memfile.hpp"

namespace rv::gui {
namespace {

/// "16 KB — 4096 x 32-bit". Both halves matter: the bytes are what the
/// machine has, the depth is what a block memory is ordered by.
QString memory_size_label(u32 bytes) {
    return QString("%1 KB — %2 × 32-bit").arg(bytes / 1024).arg(bytes / 4);
}

QFont mono(int size = 10) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(size);
    return font;
}

/// Ask for a device to add: type, address, and the extras the type needs.
class AddDeviceDialog final : public QDialog {
public:
    AddDeviceDialog(QWidget* parent, const core::Bus& bus) : QDialog(parent), bus_(&bus) {
        setWindowTitle("Add peripheral");
        auto* form = new QFormLayout(this);

        type_ = new QComboBox(this);
        for (const core::DeviceType& type : core::device_catalogue()) {
            type_->addItem(QString("%1 — %2")
                               .arg(QString::fromUtf8(type.name.data(),
                                                      static_cast<int>(type.name.size())))
                               .arg(QString::fromUtf8(type.description.data(),
                                                      static_cast<int>(type.description.size()))),
                           QString::fromUtf8(type.name.data(), static_cast<int>(type.name.size())));
        }
        form->addRow("Type", type_);

        // An address, not a slot number: an address is what the program uses
        // and what every other panel shows. The slot is arithmetic, and doing
        // arithmetic to place a device is the interface's job, not the
        // person's.
        address_ = new QLineEdit(this);
        address_->setFont(mono());
        form->addRow("Address", address_);

        note_ = new QLabel(this);
        note_->setWordWrap(true);
        form->addRow("", note_);

        name_ = new QLineEdit(this);
        name_->setPlaceholderText("what this peripheral is called");
        name_row_ = form->rowCount();
        form->addRow("Name", name_);

        // A list, for the same reason the memory sizes are one: these are
        // hardware quantities and they come in powers of two. A board has 8 or
        // 16 switches, not 13.
        size_ = new QComboBox(this);
        size_row_ = form->rowCount();
        form->addRow("Size", size_);

        load_ = new QLineEdit(this);
        load_->setPlaceholderText("optional .mem image to preload");
        load_row_ = form->rowCount();
        form->addRow("Load", load_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);

        connect(type_, &QComboBox::currentIndexChanged, this, &AddDeviceDialog::retype);
        connect(address_, &QLineEdit::textChanged, this, &AddDeviceDialog::update_fields);
        connect(size_, &QComboBox::currentIndexChanged, this, &AddDeviceDialog::update_fields);
        retype();
    }

    QString type() const { return type_->currentData().toString(); }
    std::size_t slot() const { return core::slot_of_address(address()); }
    u32 size() const { return size_->currentData().toUInt(); }
    QString name() const { return name_->text(); }
    QString load_path() const { return load_->text(); }

    /// The address typed, rounded down to the slot it falls in. Anything
    /// unparseable reads as the start of the window rather than as an error --
    /// the note below the field says what was understood.
    Addr address() const {
        bool valid = false;
        QString text = address_->text().trimmed();
        if (text.startsWith("0x", Qt::CaseInsensitive)) text = text.mid(2);
        const auto typed = static_cast<Addr>(text.toUInt(&valid, 16));
        if (!valid || typed < core::kMmioBase) return core::kMmioBase;
        const Addr offset = typed - core::kMmioBase;
        if (offset >= core::kMmioSize) return core::kMmioBase;
        return core::kMmioBase + (offset / core::kSlotSize) * core::kSlotSize;
    }

private:
    /// The number of slots the device as configured would take, so the dialog
    /// can offer a free address and warn about the right range.
    std::size_t span() const {
        std::unique_ptr<core::Device> probe =
            core::make_device(type().toStdString(), size(), name().toStdString());
        return probe == nullptr ? 1 : std::max<std::size_t>(1, probe->slot_count());
    }

    /// The type or the size changed, so the sensible default address changed
    /// with it.
    void retype() {
        const core::DeviceType* selected = core::find_device_type(type().toStdString());
        const core::SizeUnit unit =
            selected != nullptr ? selected->unit : core::SizeUnit::None;
        const bool sized = unit != core::SizeUnit::None;
        const bool named = selected != nullptr && selected->takes_name;
        const bool block = unit == core::SizeUnit::Bytes;

        {
            const QSignalBlocker block_size(size_);
            size_->clear();
            const QString units = QString::fromUtf8(core::unit_name(unit).data(),
                                                    static_cast<int>(core::unit_name(unit).size()));
            for (const u32 value : core::common_sizes(unit)) {
                size_->addItem(QString("%1 %2").arg(value).arg(units),
                               QVariant(static_cast<uint>(value)));
            }
            // The largest width, or a middling block -- a 32-bit register and
            // a 256-byte block are what someone means when they do not say.
            size_->setCurrentIndex(unit == core::SizeUnit::Bytes ? 2 : size_->count() - 1);
        }

        if (auto* form = qobject_cast<QFormLayout*>(layout())) {
            form->setRowVisible(name_row_, named);
            form->setRowVisible(size_row_, sized);
            form->setRowVisible(load_row_, block);
        }
        address_->setText(
            QString::asprintf("0x%08x", core::slot_address(core::first_free_slot(*bus_, span()))));
        update_fields();
    }

    void update_fields() {
        const std::size_t width = span();
        const Addr base = address();
        QString note = QString::asprintf("slot %zu", core::slot_of_address(base));
        if (width > 1) {
            note += QString::asprintf(", through 0x%08x",
                                      base + static_cast<Addr>(width * core::kSlotSize) - 1);
        }

        // An overlap is allowed. Saying so here, before the button is pressed,
        // is the difference between a warning and a scolding.
        const std::vector<core::Device*> clash =
            bus_->devices_overlapping(core::slot_of_address(base), width);
        if (!clash.empty()) {
            QStringList names;
            for (const core::Device* device : clash) {
                names << QString::fromUtf8(device->type_name().data(),
                                           static_cast<int>(device->type_name().size()));
            }
            note += "  —  overlaps " + names.join(", ") + "; the new one wins those addresses";
            note_->setStyleSheet("color: #e0b055;");
        } else {
            note_->setStyleSheet("color: #7d8590;");
        }
        note_->setText(note);
    }

    const core::Bus* bus_;
    QComboBox* type_ = nullptr;
    QLineEdit* address_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* size_ = nullptr;
    QLineEdit* load_ = nullptr;
    QLabel* note_ = nullptr;
    int name_row_ = 0;
    int size_row_ = 0;
    int load_row_ = 0;
};

}  // namespace

DevicePanel::DevicePanel(dbg::DebugSession& session, QWidget* parent)
    : QWidget(parent), session_(&session) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    list_ = new QTableWidget(0, 4, this);
    list_->setHorizontalHeaderLabels({"slot", "address", "type", "state"});
    list_->verticalHeader()->setVisible(false);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setFont(mono());
    list_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(list_, &QTableWidget::itemSelectionChanged, this,
            &DevicePanel::on_selection_changed);
    layout->addWidget(list_, 1);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton("Add…", this);
    remove_ = new QPushButton("Remove", this);
    remove_->setEnabled(false);
    connect(add, &QPushButton::clicked, this, &DevicePanel::on_add);
    connect(remove_, &QPushButton::clicked, this, &DevicePanel::on_remove);
    buttons->addWidget(add);
    buttons->addWidget(remove_);

    // Beside the sizes rather than only in the File menu: the depth being
    // exported is the number in the list next to it, and the two belong on the
    // same row for the same reason they belong on the same line of the label.
    auto* export_button = new QPushButton("Export .mem…", this);
    connect(export_button, &QPushButton::clicked, this, &DevicePanel::exportRequested);
    buttons->addWidget(export_button);
    buttons->addStretch();

    // The two memories are part of the machine in the same way the peripherals
    // are, so they are configured in the same place. Both spaces are exported a
    // word per line, which is why the step is four bytes.
    // A list rather than a number to type, and labelled by depth in words as
    // well as bytes: a block memory is configured by how many words deep it
    // is, so the label is the number that goes into the Block Memory Generator
    // beside the .mem file this exports.
    const auto memory_box = [this](const char* label) {
        auto* box = new QComboBox(this);
        for (const u32 size : core::kCommonMemorySizes) {
            box->addItem(QString("%1  %2").arg(label, memory_size_label(size)),
                         QVariant(static_cast<uint>(size)));
        }
        return box;
    };
    imem_ = memory_box("imem");
    dmem_ = memory_box("dmem");
    buttons->addWidget(imem_);
    buttons->addWidget(dmem_);
    connect(imem_, &QComboBox::currentIndexChanged, this, &DevicePanel::on_memory_resized);
    connect(dmem_, &QComboBox::currentIndexChanged, this, &DevicePanel::on_memory_resized);
    layout->addLayout(buttons);

    controls_ = new QGroupBox("Controls", this);
    controls_layout_ = new QVBoxLayout(controls_);
    layout->addWidget(controls_);

    sync_memory_fields();
    refresh();
}

void DevicePanel::sync_memory_fields() {
    const auto select = [](QComboBox* box, u32 size, const char* label) {
        const QSignalBlocker block(box);
        const int found = box->findData(QVariant(static_cast<uint>(size)));
        if (found >= 0) {
            box->setCurrentIndex(found);
            return;
        }
        // A file may name a size the list does not offer. Showing it is the
        // only honest thing to do -- the alternative is a box that says one
        // size while the machine has another.
        box->addItem(QString("%1  %2").arg(label, memory_size_label(size)),
                     QVariant(static_cast<uint>(size)));
        box->setCurrentIndex(box->count() - 1);
    };
    select(imem_, session_->hart().imem().size(), "imem");
    select(dmem_, session_->hart().bus().dmem().size(), "dmem");
}

void DevicePanel::on_memory_resized() {
    session_->hart().resize_memories(imem_->currentData().toUInt(),
                                     dmem_->currentData().toUInt());
    sync_memory_fields();
    emit memoryResized();
}

void DevicePanel::refresh() {
    refresh_list();

    // Only when what the controls were built from has actually changed. A
    // rebuild deletes every control widget, so doing it on every step would
    // tear down whatever the user is in the middle of operating.
    if (controls_signature() != controls_built_) rebuild_controls();
}

std::string DevicePanel::controls_signature() const {
    const std::vector<core::Device*> devices = session_->hart().bus().devices();
    const int row = list_->currentRow();
    std::string out = std::to_string(row) + "/" + std::to_string(devices.size());
    if (row >= 0 && row < static_cast<int>(devices.size())) {
        const core::Device* device = devices[static_cast<std::size_t>(row)];
        out += ":" + std::to_string(device->slot());
        for (const core::InputControl& control : device->inputs()) {
            out += "," + std::to_string(control.value);
        }
    }
    return out;
}

void DevicePanel::input_changed() {
    // The control already shows the new value -- it is the thing that set it.
    // Recording the signature here is what stops the next refresh from
    // rebuilding on top of it.
    controls_built_ = controls_signature();
    refresh_list();
    emit inputChanged();
}

void DevicePanel::refresh_list() {
    const std::vector<core::Device*> devices = session_->hart().bus().devices();

    // Keep the selection across a refresh, which happens on every step.
    const int selected = list_->currentRow();
    list_->setRowCount(static_cast<int>(devices.size()));

    for (int row = 0; row < static_cast<int>(devices.size()); ++row) {
        core::Device* device = devices[static_cast<std::size_t>(row)];
        QStringList cells = {
            QString::number(device->slot()),
            QString::asprintf("0x%08x", device->base_address()),
            QString::fromUtf8(device->type_name().data(),
                              static_cast<int>(device->type_name().size())),
            QString::fromStdString(device->state_summary()),
        };
        // A device nothing resolves to is still attached and still worth
        // showing -- silently listing it as though it answered would be worse
        // than allowing the overlap in the first place.
        const std::size_t reachable = session_->hart().bus().reachable_slots(device);
        const std::size_t span = std::max<std::size_t>(1, device->slot_count());
        if (reachable == 0) {
            cells[3] = "covered by another device";
        } else if (reachable < span) {
            cells[3] += QString("  (partly covered)");
        }
        for (int column = 0; column < cells.size(); ++column) {
            QTableWidgetItem* item = list_->item(row, column);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                list_->setItem(row, column, item);
            }
            item->setText(cells[column]);
        }
    }
    if (selected >= 0 && selected < list_->rowCount()) list_->selectRow(selected);
    list_->resizeColumnsToContents();
    list_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
}

void DevicePanel::on_selection_changed() {
    remove_->setEnabled(list_->currentRow() >= 0);
    rebuild_controls();
}

void DevicePanel::rebuild_controls() {
    controls_built_ = controls_signature();

    // deleteLater, and out of the tree first: a rebuild may still be reached
    // from inside a control's own signal, and destroying the sender under it
    // is a use-after-free rather than a redraw.
    while (QLayoutItem* item = controls_layout_->takeAt(0)) {
        if (QWidget* widget = item->widget(); widget != nullptr) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }

    const int row = list_->currentRow();
    const std::vector<core::Device*> devices = session_->hart().bus().devices();
    if (row < 0 || row >= static_cast<int>(devices.size())) {
        controls_layout_->addWidget(new QLabel("Select a peripheral.", controls_));
        return;
    }

    core::Device* device = devices[static_cast<std::size_t>(row)];
    // Captured by slot rather than by pointer: a control that outlives its
    // device -- detached, or replaced by loading a board file -- then finds
    // nothing and does nothing, instead of writing through a dangling one.
    const std::size_t slot = device->slot();
    const std::vector<core::InputControl> inputs = device->inputs();
    if (inputs.empty()) {
        controls_layout_->addWidget(
            new QLabel("This peripheral has no inputs — it only reports.", controls_));
        return;
    }

    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const core::InputControl& control = inputs[index];
        const QString name = QString::fromStdString(control.name);

        switch (control.kind) {
            case core::InputKind::Toggle: {
                auto* group = new QWidget(controls_);
                auto* grid = new QGridLayout(group);
                grid->setContentsMargins(0, 0, 0, 0);
                grid->setSpacing(2);
                grid->addWidget(new QLabel(name, group), 0, 0);

                // Bit 31 on the left, so the row reads like the hex beside it.
                for (int bit = control.bit_count - 1; bit >= 0; --bit) {
                    auto* box = new QCheckBox(group);
                    box->setChecked(((control.value >> bit) & 1u) != 0);
                    box->setToolTip(QString("bit %1").arg(bit));
                    const std::size_t which = index;
                    connect(box, &QCheckBox::toggled, this, [this, slot, which, bit](bool on) {
                        core::Device* target = session_->hart().bus().device_at_slot(slot);
                        if (target == nullptr) return;
                        const std::vector<core::InputControl> current = target->inputs();
                        if (which >= current.size()) return;
                        u32 value = current[which].value;
                        value = on ? (value | (1u << bit)) : (value & ~(1u << bit));
                        target->set_input(which, value);
                        input_changed();
                    });
                    grid->addWidget(box, 0, control.bit_count - bit);
                }
                controls_layout_->addWidget(group);
                break;
            }

            case core::InputKind::Button: {
                auto* button = new QPushButton(name, controls_);
                button->setCheckable(true);
                button->setChecked(control.value != 0);
                const std::size_t which = index;
                connect(button, &QPushButton::toggled, this, [this, slot, which](bool on) {
                    core::Device* target = session_->hart().bus().device_at_slot(slot);
                    if (target == nullptr) return;
                    target->set_input(which, on ? 1u : 0u);
                    input_changed();
                });
                controls_layout_->addWidget(button);
                break;
            }

            case core::InputKind::Number: {
                auto* row_widget = new QWidget(controls_);
                auto* line = new QHBoxLayout(row_widget);
                line->setContentsMargins(0, 0, 0, 0);
                line->addWidget(new QLabel(name, row_widget));

                auto* edit = new QLineEdit(QString::number(control.value), row_widget);
                edit->setFont(mono());
                const std::size_t which = index;
                connect(edit, &QLineEdit::editingFinished, this, [this, slot, which, edit] {
                    core::Device* target = session_->hart().bus().device_at_slot(slot);
                    if (target == nullptr) return;
                    bool valid = false;
                    const QString text = edit->text().trimmed();
                    // Accept 0x…, 0b… and plain decimal, the same spellings the
                    // assembler and the debugger take.
                    const u32 value = static_cast<u32>(
                        text.startsWith("0x") ? text.mid(2).toUInt(&valid, 16)
                        : text.startsWith("0b") ? text.mid(2).toUInt(&valid, 2)
                                                : text.toUInt(&valid, 10));
                    if (!valid) {
                        edit->setStyleSheet("color: #f26d6d;");
                        return;
                    }
                    edit->setStyleSheet("");
                    target->set_input(which, value);
                    input_changed();
                });
                line->addWidget(edit, 1);
                controls_layout_->addWidget(row_widget);
                break;
            }
        }
    }
}

void DevicePanel::on_add() {
    core::Bus& bus = session_->hart().bus();
    AddDeviceDialog dialog(this, bus);
    if (dialog.exec() != QDialog::Accepted) return;

    std::unique_ptr<core::Device> device = core::make_device(
        dialog.type().toStdString(), dialog.size(), dialog.name().toStdString());
    if (device == nullptr) {
        QMessageBox::warning(this, "rv32", "Unknown device type.");
        return;
    }

    auto* ram = dynamic_cast<core::RamDevice*>(device.get());
    core::Device* attached = bus.attach(dialog.slot(), std::move(device));
    if (attached == nullptr) {
        QMessageBox::warning(this, "rv32",
                             QString("A device at 0x%1 does not fit before the end of the "
                                     "peripheral window, which ends at 0x%2.")
                                 .arg(dialog.address(), 8, 16, QLatin1Char('0'))
                                 .arg(core::kMmioBase + core::kMmioSize - 1, 8, 16,
                                      QLatin1Char('0')));
        return;
    }

    if (ram != nullptr && !dialog.load_path().isEmpty()) {
        std::vector<Word> words;
        const core::MemFileResult result =
            core::read_mem_file(dialog.load_path().toStdString(), words);
        if (!result.ok) {
            QMessageBox::warning(this, "rv32", QString::fromStdString(result.error));
        } else {
            ram->load_words(words);
        }
    }

    refresh();
    emit devicesChanged();
}

void DevicePanel::on_remove() {
    const int row = list_->currentRow();
    const std::vector<core::Device*> devices = session_->hart().bus().devices();
    if (row < 0 || row >= static_cast<int>(devices.size())) return;

    // By identity rather than by slot: two devices can share one, and the row
    // the person selected is the one they mean.
    session_->hart().bus().detach_device(devices[static_cast<std::size_t>(row)]);
    rebuild_controls();
    refresh();
    emit devicesChanged();
}

bool DevicePanel::load_config(const QString& path) {
    const core::DeviceConfig config = core::read_device_config(path.toStdString());
    if (!config.ok()) {
        QMessageBox::warning(this, "rv32", QString::fromStdString(config.error));
        return false;
    }
    std::vector<std::string> warnings;
    const std::string error = core::apply_machine_config(
        session_->hart(), config, core::directory_of(path.toStdString()), &warnings);
    if (!error.empty()) {
        QMessageBox::warning(this, "rv32", QString::fromStdString(error));
        return false;
    }
    // Not a dialog. An overlap is something this deliberately allows, and a
    // modal box for it would be a scolding rather than a warning. It goes in
    // the message line, and the list keeps saying which device stopped
    // answering for as long as it is true.
    QStringList lines;
    for (const std::string& warning : warnings) lines << QString::fromStdString(warning);
    last_warning_ = lines.join("; ");
    sync_memory_fields();
    rebuild_controls();
    refresh();
    emit devicesChanged();
    return true;
}

bool DevicePanel::save_config(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "rv32", "Cannot write " + path);
        return false;
    }
    QTextStream(&file) << QString::fromStdString(
        core::write_device_config(core::describe(session_->hart())));
    return true;
}

}  // namespace rv::gui
