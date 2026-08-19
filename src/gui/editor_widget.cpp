#include "gui/editor_widget.hpp"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>

#include "gui/highlighter.hpp"

namespace rv::gui {
namespace {

constexpr int kMarkerColumns = 2;  // breakpoint dot, execution arrow
constexpr int kGutterPadding = 8;

QColor colour_for(as::Severity severity) {
    switch (severity) {
        case as::Severity::Error: return QColor(0xf2, 0x6d, 0x6d);
        case as::Severity::Warning: return QColor(0xe8, 0xc0, 0x4e);
        case as::Severity::Note:
        case as::Severity::Help: return QColor(0x5b, 0xd6, 0xc8);
    }
    return QColor(0xd6, 0xd6, 0xd6);
}

}  // namespace

// ---------------------------------------------------------------------------
// Hint bar
// ---------------------------------------------------------------------------

HintBar::HintBar(QWidget* parent) : QWidget(parent) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(10);
    setFont(font);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize HintBar::sizeHint() const { return QSize(0, fontMetrics().height() + 6); }

void HintBar::show_for(const as::Context& context) {
    if (context.where != as::Context::Where::Operand) {
        text_.clear();
        form_.clear();
        highlight_begin_ = highlight_end_ = -1;
        update();
        return;
    }

    const std::vector<as::OperandHint> hints = as::operand_hints(context.mnemonic);
    if (hints.empty()) {
        text_.clear();
        form_.clear();
        update();
        return;
    }

    // Rebuild the signature here rather than calling signature_of, so the
    // span of the operand under the caret is known while it is assembled.
    text_ = QString::fromStdString(context.mnemonic);
    highlight_begin_ = highlight_end_ = -1;
    for (std::size_t index = 0; index < hints.size(); ++index) {
        text_ += index == 0 ? " " : ", ";
        if (index == context.operand_index) highlight_begin_ = text_.size();
        text_ += QString::fromStdString(hints[index].name);
        if (index == context.operand_index) highlight_end_ = text_.size();
    }

    form_ = context.operand_index < hints.size()
                ? QString::fromStdString(hints[context.operand_index].form)
                : QString();
    update();
}

void HintBar::paintEvent(QPaintEvent* /*event*/) {
    if (text_.isEmpty()) return;

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x1a, 0x1d, 0x22));

    const QFontMetrics metrics(font());
    const int baseline = (height() + metrics.ascent() - metrics.descent()) / 2;
    int x = 6;

    // The operand the caret is in is picked out; the rest is context.
    const auto draw = [&](const QString& part, const QColor& colour, bool bold) {
        QFont f = font();
        f.setBold(bold);
        painter.setFont(f);
        painter.setPen(colour);
        painter.drawText(x, baseline, part);
        x += QFontMetrics(f).horizontalAdvance(part);
    };

    if (highlight_begin_ < 0) {
        draw(text_, QColor(0x9a, 0xa2, 0xad), false);
    } else {
        draw(text_.left(highlight_begin_), QColor(0x6a, 0x71, 0x7c), false);
        draw(text_.mid(highlight_begin_, highlight_end_ - highlight_begin_),
             QColor(0xe8, 0xc0, 0x4e), true);
        draw(text_.mid(highlight_end_), QColor(0x6a, 0x71, 0x7c), false);
    }

    if (!form_.isEmpty()) {
        x += 12;
        draw(form_, QColor(0x5b, 0xd6, 0xc8), false);
    }
}

// ---------------------------------------------------------------------------
// Gutter
// ---------------------------------------------------------------------------

GutterWidget::GutterWidget(EditorWidget* editor) : QWidget(editor), editor_(editor) {}

QSize GutterWidget::sizeHint() const { return QSize(editor_->gutter_width(), 0); }

void GutterWidget::paintEvent(QPaintEvent* event) { editor_->paint_gutter(event); }

void GutterWidget::mousePressEvent(QMouseEvent* event) {
    // Clicking the margin toggles a breakpoint, which is the one gesture
    // everybody already knows from every other editor.
    const int line = editor_->line_at(static_cast<int>(event->position().y()));
    if (line > 0) emit editor_->breakpointToggled(line);
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

EditorWidget::EditorWidget(QWidget* parent) : QPlainTextEdit(parent), gutter_(new GutterWidget(this)) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);
    // Assembly is column-sensitive to read, so tabs must not vary in width.
    setTabStopDistance(4 * QFontMetricsF(font).horizontalAdvance(' '));
    setLineWrapMode(QPlainTextEdit::NoWrap);

    new AsmHighlighter(document());

    // A small popup rather than a full-width list: it appears while typing, so
    // it must not cover the code being written.
    completion_model_ = new QStringListModel(this);
    completer_ = new QCompleter(completion_model_, this);
    completer_->setWidget(this);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->popup()->setFont(font);
    completer_->popup()->setMaximumHeight(6 * QFontMetrics(font).height() + 8);
    connect(completer_, QOverload<const QString&>::of(&QCompleter::activated), this,
            &EditorWidget::insert_completion);

    hint_ = new HintBar(this);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &EditorWidget::update_hint);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &EditorWidget::update_gutter_width);
    connect(this, &QPlainTextEdit::updateRequest, this, &EditorWidget::update_gutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            &EditorWidget::highlight_current_line);

    update_gutter_width();
    highlight_current_line();
}

int EditorWidget::gutter_width() const {
    int digits = 2;
    for (int lines = blockCount(); lines >= 100; lines /= 10) ++digits;
    const int digit_width = fontMetrics().horizontalAdvance(QLatin1Char('9'));
    const int marker_width = fontMetrics().horizontalAdvance(QLatin1Char('M'));
    return kGutterPadding + kMarkerColumns * marker_width + digits * digit_width + kGutterPadding;
}

void EditorWidget::update_gutter_width() { setViewportMargins(gutter_width(), 0, 0, 0); }

void EditorWidget::update_gutter(const QRect& rect, int dy) {
    if (dy != 0) {
        gutter_->scroll(0, dy);
    } else {
        gutter_->update(0, rect.y(), gutter_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) update_gutter_width();
}

void EditorWidget::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    const int hint_height = hint_->sizeHint().height();
    gutter_->setGeometry(
        QRect(area.left(), area.top(), gutter_width(), area.height() - hint_height));
    hint_->setGeometry(
        QRect(area.left(), area.bottom() - hint_height + 1, area.width(), hint_height));
    setViewportMargins(gutter_width(), 0, 0, hint_height);
}

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------

QStringList EditorWidget::visible_completions() const { return completion_model_->stringList(); }

void EditorWidget::update_hint() {
    const QTextCursor cursor = textCursor();
    const QByteArray line = cursor.block().text().toUtf8();
    hint_->show_for(as::context_at(
        std::string_view(line.constData(), static_cast<std::size_t>(line.size())),
        static_cast<u16>(cursor.positionInBlock())));
}

void EditorWidget::request_completion() {
    const QTextCursor cursor = textCursor();
    const QByteArray line = cursor.block().text().toUtf8();
    const std::string_view view(line.constData(), static_cast<std::size_t>(line.size()));
    const auto column = static_cast<u16>(cursor.positionInBlock());

    candidates_ = as::complete(view, column, symbols_);
    if (candidates_.empty()) {
        completer_->popup()->hide();
        return;
    }

    // The list shows the signature, because knowing that `addi` takes three
    // operands is the useful half of the suggestion.
    QStringList entries;
    entries.reserve(static_cast<int>(candidates_.size()));
    for (const as::Completion& candidate : candidates_) {
        entries << QString::fromStdString(candidate.text);
    }
    completion_model_->setStringList(entries);

    const as::Context context = as::context_at(view, column);
    completer_->setCompletionPrefix(QString::fromStdString(context.prefix));
    completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));

    QRect where = cursorRect();
    where.setWidth(completer_->popup()->sizeHintForColumn(0) +
                   completer_->popup()->verticalScrollBar()->sizeHint().width() + 24);
    completer_->complete(where);
}

void EditorWidget::insert_completion(const QString& text) {
    QTextCursor cursor = textCursor();
    const int prefix_length = completer_->completionPrefix().length();
    cursor.setPosition(cursor.position() - prefix_length, QTextCursor::KeepAnchor);
    cursor.insertText(text);
    setTextCursor(cursor);
    update_hint();
}

void EditorWidget::keyPressEvent(QKeyEvent* event) {
    if (completer_->popup()->isVisible()) {
        // While the popup is up these keys belong to it, not to the editor.
        switch (event->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Tab:
            case Qt::Key_Escape:
            case Qt::Key_Up:
            case Qt::Key_Down:
                event->ignore();
                return;
            default:
                break;
        }
    }

    const bool explicit_request =
        event->key() == Qt::Key_Space && (event->modifiers() & Qt::ControlModifier) != 0;
    if (explicit_request) {
        request_completion();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);

    // Offer suggestions once there is enough of a word to narrow them, and keep
    // them current while it grows. Fewer than two characters would put a list
    // of everything in front of someone who has not asked for one.
    const QTextCursor cursor = textCursor();
    const QByteArray line = cursor.block().text().toUtf8();
    const as::Context context = as::context_at(
        std::string_view(line.constData(), static_cast<std::size_t>(line.size())),
        static_cast<u16>(cursor.positionInBlock()));

    if (context.prefix.size() >= 2) {
        request_completion();
    } else if (completer_->popup()->isVisible()) {
        completer_->popup()->hide();
    }
}

int EditorWidget::line_at(int y) const {
    QTextBlock block = firstVisibleBlock();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());

    while (block.isValid()) {
        const int bottom = top + static_cast<int>(blockBoundingRect(block).height());
        if (y >= top && y <= bottom) return block.blockNumber() + 1;
        block = block.next();
        top = bottom;
    }
    return 0;
}

void EditorWidget::paint_gutter(QPaintEvent* event) {
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), QColor(0x1e, 0x21, 0x27));

    QTextBlock block = firstVisibleBlock();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    const int marker_width = fontMetrics().horizontalAdvance(QLatin1Char('M'));

    while (block.isValid() && top <= event->rect().bottom()) {
        const int bottom = top + static_cast<int>(blockBoundingRect(block).height());
        const int line = block.blockNumber() + 1;

        if (block.isVisible() && bottom >= event->rect().top()) {
            const int height = static_cast<int>(blockBoundingRect(block).height());
            int x = kGutterPadding;

            // Marker precedence matches the terminal version: a breakpoint is a
            // decision the user made, so it outranks a diagnostic the tool
            // inferred.
            const bool has_breakpoint =
                std::find(breakpoints_.begin(), breakpoints_.end(), line) != breakpoints_.end();
            const auto diagnostic = diagnostics_.find(line);

            if (has_breakpoint) {
                painter.setBrush(QColor(0xf2, 0x6d, 0x6d));
                painter.setPen(Qt::NoPen);
                const int radius = std::min(height, marker_width) / 3;
                painter.drawEllipse(QPoint(x + marker_width / 2, top + height / 2), radius, radius);
            } else if (diagnostic != diagnostics_.end()) {
                painter.setPen(colour_for(diagnostic->second.severity));
                painter.drawText(QRect(x, top, marker_width, height), Qt::AlignCenter,
                                 diagnostic->second.severity == as::Severity::Error ? "E" : "W");
            }
            x += marker_width;

            if (line == execution_line_) {
                painter.setPen(QColor(0xe8, 0xc0, 0x4e));
                painter.drawText(QRect(x, top, marker_width, height), Qt::AlignCenter, "▶");
            }
            x += marker_width;

            painter.setPen(line == current_line() ? QColor(0xd6, 0xd6, 0xd6)
                                                  : QColor(0x6a, 0x71, 0x7c));
            painter.drawText(QRect(x, top, gutter_->width() - x - kGutterPadding, height),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(line));
        }

        block = block.next();
        top = bottom;
    }
}

void EditorWidget::highlight_current_line() {
    QList<QTextEdit::ExtraSelection> selections;

    QTextEdit::ExtraSelection line;
    line.format.setBackground(QColor(0x26, 0x2a, 0x33));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor();
    line.cursor.clearSelection();
    selections.append(line);

    // The span each diagnostic points at, restyled rather than covered -- the
    // offending word has to stay readable.
    for (const auto& [number, diagnostic] : diagnostics_) {
        QTextBlock block = document()->findBlockByNumber(number - 1);
        if (!block.isValid()) continue;

        QTextEdit::ExtraSelection mark;
        mark.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        mark.format.setUnderlineColor(colour_for(diagnostic.severity));
        mark.cursor = QTextCursor(block);
        mark.cursor.setPosition(block.position() + diagnostic.primary.col);
        const int length = std::max<int>(diagnostic.primary.len, 1);
        mark.cursor.setPosition(
            std::min(block.position() + diagnostic.primary.col + length,
                     block.position() + block.length() - 1),
            QTextCursor::KeepAnchor);
        selections.append(mark);
    }

    setExtraSelections(selections);
}

void EditorWidget::set_diagnostics(const as::DiagBag& diagnostics) {
    diagnostics_.clear();
    for (const as::Diagnostic& diagnostic : diagnostics.items()) {
        if (diagnostic.primary.line == 0) continue;
        const int line = static_cast<int>(diagnostic.primary.line);
        // Keep the worst one per line: an error outranks a warning outranks a
        // note, so the gutter letter says how bad the line is.
        const auto existing = diagnostics_.find(line);
        if (existing == diagnostics_.end() ||
            static_cast<int>(diagnostic.severity) < static_cast<int>(existing->second.severity)) {
            diagnostics_[line] = diagnostic;
        }
    }
    apply_diagnostic_marks();
}

void EditorWidget::clear_diagnostics() {
    diagnostics_.clear();
    apply_diagnostic_marks();
}

void EditorWidget::apply_diagnostic_marks() {
    highlight_current_line();
    gutter_->update();
}

const as::Diagnostic* EditorWidget::diagnostic_on_line(int line) const {
    const auto found = diagnostics_.find(line);
    return found == diagnostics_.end() ? nullptr : &found->second;
}

void EditorWidget::set_execution_line(int line) {
    if (execution_line_ == line) return;
    execution_line_ = line;
    gutter_->update();
}

void EditorWidget::set_breakpoint_lines(std::vector<int> lines) {
    breakpoints_ = std::move(lines);
    gutter_->update();
}

int EditorWidget::current_line() const { return textCursor().blockNumber() + 1; }

void EditorWidget::go_to_line(int line) {
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
}

}  // namespace rv::gui
