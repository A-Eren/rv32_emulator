// The bitfield breakdown panel.
//
// The same idea as the terminal version, drawn properly: one renderer driven by
// InstrFormat, so six small field tables cover the whole instruction set. It
// answers "how did this line become 32 bits?", and it follows the editor's
// caret so a line explains itself before it has ever been run.
#pragma once

#include <QWidget>

#include "dbg/session.hpp"

namespace rv::gui {

class EncodingWidget final : public QWidget {
    Q_OBJECT

public:
    explicit EncodingWidget(const dbg::DebugSession& session, QWidget* parent = nullptr);

    /// Show a specific address rather than the pc. Used when the caret moves.
    void set_address(Addr addr);
    void follow_pc();

    QSize sizeHint() const override;
    /// The rendered field descriptions, for the tests.
    QStringList field_summary() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Addr shown_address() const;

    const dbg::DebugSession* session_;
    Addr address_ = 0;
    bool follow_pc_ = true;
};

}  // namespace rv::gui
