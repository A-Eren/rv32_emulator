// The assembly editor.
//
// A QPlainTextEdit with a gutter carrying the same three marks the terminal
// version has: a breakpoint dot, the execution arrow, and a severity letter for
// the last assemble's diagnostics. Keeping them identical matters -- the two
// front-ends should feel like one tool seen from different sides.
#pragma once

#include <QPlainTextEdit>
#include <QWidget>
#include <map>
#include <vector>

#include "asm/diagnostic.hpp"

namespace rv::gui {

class EditorWidget;

/// The margin down the left. A separate widget because QPlainTextEdit reserves
/// the space but does not paint it.
class GutterWidget final : public QWidget {
    Q_OBJECT

public:
    explicit GutterWidget(EditorWidget* editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    EditorWidget* editor_;
};

class EditorWidget final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit EditorWidget(QWidget* parent = nullptr);

    int gutter_width() const;
    void paint_gutter(QPaintEvent* event);
    /// 1-based line under a y coordinate in gutter space, or 0.
    int line_at(int y) const;

    /// Diagnostics from the last assemble, shown in the gutter and underlined.
    void set_diagnostics(const as::DiagBag& diagnostics);
    void clear_diagnostics();
    const as::Diagnostic* diagnostic_on_line(int line) const;

    void set_execution_line(int line);
    void set_breakpoint_lines(std::vector<int> lines);

    int current_line() const;
    void go_to_line(int line);

signals:
    void breakpointToggled(int line);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void update_gutter_width();
    void update_gutter(const QRect& rect, int dy);
    void highlight_current_line();

private:
    void apply_diagnostic_marks();

    GutterWidget* gutter_;
    /// Line number -> the worst diagnostic reported on it.
    std::map<int, as::Diagnostic> diagnostics_;
    std::vector<int> breakpoints_;
    int execution_line_ = 0;
};

}  // namespace rv::gui
