// The peripherals panel.
//
// Lists what is attached, lets devices be added and removed, and renders each
// device's own controls. The controls come from Device::inputs(), so this
// panel can operate a peripheral it has never heard of -- adding a device type
// to the catalogue is enough to make it usable here.
#pragma once

#include <QWidget>

#include <string>

#include "dbg/session.hpp"

class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace rv::gui {

class DevicePanel final : public QWidget {
    Q_OBJECT

public:
    explicit DevicePanel(dbg::DebugSession& session, QWidget* parent = nullptr);

    void refresh();

    /// Load and apply a device configuration file.
    bool load_config(const QString& path);
    bool save_config(const QString& path);

signals:
    /// The set of devices changed, so anything showing addresses needs redrawing.
    void devicesChanged();
    /// A control was operated. Distinct from devicesChanged because it does not
    /// invalidate addresses, only values.
    void inputChanged();

private slots:
    void on_add();
    void on_remove();
    void on_selection_changed();

private:
    /// Redraw the table of what is attached, leaving the controls alone. This
    /// is what a control's own handler calls: rebuilding the controls from
    /// inside one of their signals would delete the widget that is mid-signal.
    void refresh_list();
    void rebuild_controls();
    /// Record the change, redraw the list, and tell the window. The one path
    /// every control takes when it is operated.
    void input_changed();
    /// What the controls currently on screen were built from -- the selected
    /// device and its input values. Rebuilding only when this changes keeps a
    /// control the user is operating from being torn down underneath them.
    std::string controls_signature() const;

    dbg::DebugSession* session_;
    QTableWidget* list_ = nullptr;
    QWidget* controls_ = nullptr;
    QVBoxLayout* controls_layout_ = nullptr;
    QPushButton* remove_ = nullptr;
    std::string controls_built_;
};

}  // namespace rv::gui
