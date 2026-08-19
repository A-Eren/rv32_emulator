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

QFont mono(int size = 10) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(size);
    return font;
}

/// Ask for a device to add: type, slot, and the extras the type needs.
class AddDeviceDialog final : public QDialog {
public:
    explicit AddDeviceDialog(QWidget* parent) : QDialog(parent) {
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

        slot_ = new QSpinBox(this);
        slot_->setRange(0, static_cast<int>(core::kMmioSlots) - 1);
        form->addRow("Slot", slot_);

        address_ = new QLabel(this);
        address_->setFont(mono());
        form->addRow("Address", address_);

        size_ = new QSpinBox(this);
        size_->setRange(16, 4096);
        size_->setSingleStep(16);
        size_->setValue(256);
        size_->setSuffix(" bytes");
        size_row_ = form->rowCount();
        form->addRow("Size", size_);

        load_ = new QLineEdit(this);
        load_->setPlaceholderText("optional .mem image to preload");
        form->addRow("Load", load_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);

        connect(type_, &QComboBox::currentIndexChanged, this, &AddDeviceDialog::update_fields);
        connect(slot_, &QSpinBox::valueChanged, this, &AddDeviceDialog::update_fields);
        update_fields();
    }

    QString type() const { return type_->currentData().toString(); }
    std::size_t slot() const { return static_cast<std::size_t>(slot_->value()); }
    u32 size() const { return static_cast<u32>(size_->value()); }
    QString load_path() const { return load_->text(); }

private:
    void update_fields() {
        const core::DeviceType* selected = core::find_device_type(type().toStdString());
        const bool memory = selected != nullptr && selected->takes_size;
        // Only a memory block has a size or an image to load; showing those for
        // a button would be noise.
        size_->setVisible(memory);
        load_->setVisible(memory);
        if (auto* form = qobject_cast<QFormLayout*>(layout())) {
            form->setRowVisible(size_row_, memory);
            form->setRowVisible(size_row_ + 1, memory);
        }
        address_->setText(QString::asprintf("0x%08x", core::slot_address(slot())));
    }

    QComboBox* type_ = nullptr;
    QSpinBox* slot_ = nullptr;
    QSpinBox* size_ = nullptr;
    QLineEdit* load_ = nullptr;
    QLabel* address_ = nullptr;
    int size_row_ = 0;
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
    buttons->addStretch();
    layout->addLayout(buttons);

    controls_ = new QGroupBox("Controls", this);
    controls_layout_ = new QVBoxLayout(controls_);
    layout->addWidget(controls_);

    refresh();
}

void DevicePanel::refresh() {
    const std::vector<core::Device*> devices = session_->hart().bus().devices();

    // Keep the selection across a refresh, which happens on every step.
    const int selected = list_->currentRow();
    list_->setRowCount(static_cast<int>(devices.size()));

    for (int row = 0; row < static_cast<int>(devices.size()); ++row) {
        core::Device* device = devices[static_cast<std::size_t>(row)];
        const QStringList cells = {
            QString::number(device->slot()),
            QString::asprintf("0x%08x", device->base_address()),
            QString::fromUtf8(device->type_name().data(),
                              static_cast<int>(device->type_name().size())),
            QString::fromStdString(device->state_summary()),
        };
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

    rebuild_controls();
}

void DevicePanel::on_selection_changed() {
    remove_->setEnabled(list_->currentRow() >= 0);
    rebuild_controls();
}

void DevicePanel::rebuild_controls() {
    while (QLayoutItem* item = controls_layout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const int row = list_->currentRow();
    const std::vector<core::Device*> devices = session_->hart().bus().devices();
    if (row < 0 || row >= static_cast<int>(devices.size())) {
        controls_layout_->addWidget(new QLabel("Select a peripheral.", controls_));
        return;
    }

    core::Device* device = devices[static_cast<std::size_t>(row)];
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
                    connect(box, &QCheckBox::toggled, this, [this, device, which, bit](bool on) {
                        const std::vector<core::InputControl> current = device->inputs();
                        u32 value = current[which].value;
                        value = on ? (value | (1u << bit)) : (value & ~(1u << bit));
                        device->set_input(which, value);
                        refresh();
                        emit inputChanged();
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
                connect(button, &QPushButton::toggled, this, [this, device, which](bool on) {
                    device->set_input(which, on ? 1u : 0u);
                    refresh();
                    emit inputChanged();
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
                connect(edit, &QLineEdit::editingFinished, this, [this, device, which, edit] {
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
                    device->set_input(which, value);
                    refresh();
                    emit inputChanged();
                });
                line->addWidget(edit, 1);
                controls_layout_->addWidget(row_widget);
                break;
            }
        }
    }
}

void DevicePanel::on_add() {
    AddDeviceDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    core::Bus& bus = session_->hart().bus();
    std::unique_ptr<core::Device> device =
        core::make_device(dialog.type().toStdString(), dialog.size());
    if (device == nullptr) {
        QMessageBox::warning(this, "rv32", "Unknown device type.");
        return;
    }

    auto* ram = dynamic_cast<core::RamDevice*>(device.get());
    core::Device* attached = bus.attach(dialog.slot(), std::move(device));
    if (attached == nullptr) {
        QMessageBox::warning(this, "rv32",
                             QString("Cannot attach at slot %1: it is occupied, or the device "
                                     "does not fit before the end of the window.")
                                 .arg(dialog.slot()));
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

    session_->hart().bus().detach(devices[static_cast<std::size_t>(row)]->slot());
    refresh();
    emit devicesChanged();
}

bool DevicePanel::load_config(const QString& path) {
    const core::DeviceConfig config = core::read_device_config(path.toStdString());
    if (!config.ok()) {
        QMessageBox::warning(this, "rv32", QString::fromStdString(config.error));
        return false;
    }
    const std::string error = core::apply_device_config(
        session_->hart().bus(), config, core::directory_of(path.toStdString()));
    if (!error.empty()) {
        QMessageBox::warning(this, "rv32", QString::fromStdString(error));
        return false;
    }
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
        core::write_device_config(core::describe(session_->hart().bus())));
    return true;
}

}  // namespace rv::gui
