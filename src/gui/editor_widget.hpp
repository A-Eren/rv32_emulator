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

#include <map>
#include <vector>

#include "asm/complete.hpp"
#include "asm/diagnostic.hpp"
#include "asm/symtab.hpp"

namespace rv::gui {

class EditorWidget;

/// The strip along the bottom of the editor listing the instructions that
/// match what is being typed. It is only up while a mnemonic is being written:
/// once the instruction is settled the operands take over as ghost text, and
/// the strip folds away so the editor gets its line back.
class SuggestionBar final : public QWidget {
    Q_OBJECT

public:
    explicit SuggestionBar(QWidget* parent = nullptr);

    /// Open on these candidates, or fold away if there are none.
    void show_for(std::vector<as::Completion> candidates);
    void fold();
    /// Open in the sense of holding candidates -- which is not the same as
    /// isVisible(), since a child of a window that has not been shown yet is
    /// never visible, and the tests run without one.
    bool is_open() const { return !candidates_.empty(); }

    /// Move the highlight, wrapping at both ends.
    void advance(int delta);
    const as::Completion* highlighted() const;
    QStringList entries() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<as::Completion> candidates_;
    int current_ = 0;
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

    /// Offer the instructions matching what is being typed. Bound to
    /// Ctrl+Space, and triggered automatically at two characters.
    void request_completion();
    /// Replace the partial mnemonic with `text`. `settle` closes the bar and
    /// leaves the caret at the operands; without it the bar stays up so Tab
    /// can keep cycling.
    void insert_completion(const QString& text, bool settle = true);
    /// Walk along the bar, putting each candidate in the buffer as it goes.
    void cycle_suggestion(int delta);
    void close_bar();
    SuggestionBar* suggestion_bar() const { return bar_; }
    /// The operands greyed in after the caret. Empty when there is nothing to
    /// fill in. Exposed for the tests, which check the text and not the pixels.
    QString ghost_text() const { return ghost_; }
    /// What the completer is currently offering, for the tests.
    QStringList visible_completions() const;

signals:
    void breakpointToggled(int line);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void update_ghost();
    void update_gutter_width();
    void update_gutter(const QRect& rect, int dy);
    void highlight_current_line();

private:
    void apply_diagnostic_marks();
    /// Re-measure the viewport around the bar, which claims a line when open.
    void layout_bar();

    GutterWidget* gutter_;
    SuggestionBar* bar_ = nullptr;
    QString ghost_;
    /// Set while the widget is editing the buffer itself, so its own writes do
    /// not count as the user typing and re-open the bar underneath them.
    bool inserting_ = false;
    /// Whether Tab has already put a candidate in the buffer, which decides
    /// whether the next Tab takes the first one or the one after it.
    bool previewing_ = false;
    /// The word the bar opened on, so cycling past the end can put it back.
    QString typed_prefix_;
    const as::SymbolTable* symbols_ = nullptr;
    /// Line number -> the worst diagnostic reported on it.
    std::map<int, as::Diagnostic> diagnostics_;
    std::vector<int> breakpoints_;
    int execution_line_ = 0;
};

}  // namespace rv::gui
