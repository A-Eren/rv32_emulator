// The peripherals panel.
//
// Lists what is attached, lets devices be added and removed, and renders each
// device's own controls. The controls come from Device::inputs(), so this
// panel can operate a peripheral it has never heard of -- adding a device type
// to the catalogue is enough to make it usable here.
#pragma once

#include <QWidget>

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
    void rebuild_controls();

    dbg::DebugSession* session_;
    QTableWidget* list_ = nullptr;
    QWidget* controls_ = nullptr;
    QVBoxLayout* controls_layout_ = nullptr;
    QPushButton* remove_ = nullptr;
};

}  // namespace rv::gui
