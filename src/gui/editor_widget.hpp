// The assembly editor.
//
// A QPlainTextEdit with a gutter carrying the same three marks the terminal
// version has: a breakpoint dot, the execution arrow, and a severity letter for
// the last assemble's diagnostics. Keeping them identical matters -- the two
// front-ends should feel like one tool seen from different sides.
#pragma once

#include <QPlainTextEdit>
#include <QStringList>
#include <QWidget>

class QCompleter;
class QStringListModel;
#include <map>
#include <vector>

#include "asm/complete.hpp"
#include "asm/diagnostic.hpp"
#include "asm/symtab.hpp"

namespace rv::gui {

class EditorWidget;

/// The one-line hint under the caret: the signature of the instruction being
/// written, with the operand the caret is in picked out.
class HintBar final : public QWidget {
    Q_OBJECT

public:
    explicit HintBar(QWidget* parent = nullptr);
    void show_for(const as::Context& context);
    QString text() const { return text_; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    QString form_;
    int highlight_begin_ = -1;
    int highlight_end_ = -1;
};

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

    /// Symbols the completer offers as branch and load targets. Refreshed
    /// after every assemble.
    void set_symbols(const as::SymbolTable* symbols) { symbols_ = symbols; }

    /// Offer completions for the caret's position. Bound to Ctrl+Space, and
    /// triggered automatically once two characters of a word are typed.
    void request_completion();
    HintBar* hint_bar() const { return hint_; }
    /// What the completer is currently offering, for the tests.
    QStringList visible_completions() const;

signals:
    void breakpointToggled(int line);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void insert_completion(const QString& text);
    void update_hint();
    void update_gutter_width();
    void update_gutter(const QRect& rect, int dy);
    void highlight_current_line();

private:
    void apply_diagnostic_marks();

    GutterWidget* gutter_;
    QCompleter* completer_ = nullptr;
    QStringListModel* completion_model_ = nullptr;
    HintBar* hint_ = nullptr;
    const as::SymbolTable* symbols_ = nullptr;
    /// The candidates behind the model's strings, so accepting one can use its
    /// signature rather than re-deriving it.
    std::vector<as::Completion> candidates_;
    /// Line number -> the worst diagnostic reported on it.
    std::map<int, as::Diagnostic> diagnostics_;
    std::vector<int> breakpoints_;
    int execution_line_ = 0;
};

}  // namespace rv::gui
