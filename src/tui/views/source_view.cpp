#include "tui/views/source_view.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "tui/highlight.hpp"

namespace rv::tui {
namespace {

/// Rows kept between the selection and the edge of the viewport, so context is
/// always visible rather than the cursor riding the boundary.
constexpr int kScrollOff = 3;

}  // namespace

int SourceView::gutter_width() const {
    const auto lines = static_cast<u32>(session_->source().line_count());
    int digits = 1;
    for (u32 value = lines; value >= 10; value /= 10) ++digits;
    // marker + arrow + digits + separator
    return 2 + std::max(digits, 2) + 1;
}

void SourceView::select(u32 line) {
    const auto total = static_cast<u32>(session_->source().line_count());
    selected_ = std::clamp<u32>(line, 1, std::max<u32>(total, 1));
    follow_ = false;
}

void SourceView::follow_pc() {
    follow_ = true;
    on_execution_moved();
}

void SourceView::on_execution_moved() {
    if (!follow_) return;
    if (const auto line = session_->current_line()) selected_ = *line;
}

void SourceView::ensure_visible(int height) {
    if (height <= 0) return;
    const auto total = std::max<u32>(static_cast<u32>(session_->source().line_count()), 1);
    const int usable = std::max(1, height);
    const int margin = std::min(kScrollOff, (usable - 1) / 2);

    if (selected_ < top_ + static_cast<u32>(margin)) {
        top_ = selected_ > static_cast<u32>(margin) ? selected_ - static_cast<u32>(margin) : 1;
    }
    const u32 bottom = top_ + static_cast<u32>(usable) - 1;
    if (selected_ + static_cast<u32>(margin) > bottom) {
        const u32 wanted = selected_ + static_cast<u32>(margin);
        top_ = wanted > static_cast<u32>(usable) ? wanted - static_cast<u32>(usable) + 1 : 1;
    }
    const u32 max_top = total > static_cast<u32>(usable) ? total - static_cast<u32>(usable) + 1 : 1;
    top_ = std::clamp<u32>(top_, 1, max_top);
}

bool SourceView::on_key(const Key& key) {
    const auto total = std::max<u32>(static_cast<u32>(session_->source().line_count()), 1);
    const int page = std::max(1, last_height_ - 1);

    switch (key.code) {
        case KeyCode::Up: select(selected_ > 1 ? selected_ - 1 : 1); return true;
        case KeyCode::Down: select(std::min<u32>(selected_ + 1, total)); return true;
        case KeyCode::PageUp:
            select(selected_ > static_cast<u32>(page) ? selected_ - static_cast<u32>(page) : 1);
            return true;
        case KeyCode::PageDown:
            select(std::min<u32>(selected_ + static_cast<u32>(page), total));
            return true;
        case KeyCode::Home: select(1); return true;
        case KeyCode::End: select(total); return true;
        default: break;
    }
    // A plain 'g' snaps back to wherever execution is, which is the thing you
    // want after scrolling around.
    if (key.is_char('g')) {
        follow_pc();
        return true;
    }
    return false;
}

void SourceView::draw(Pane pane) {
    if (pane.height() <= 0 || pane.width() <= 0) return;
    last_height_ = pane.height();

    const as::SourceFile& source = session_->source();
    const auto total = static_cast<u32>(source.line_count());
    if (total == 0) {
        pane.put(0, 0, "(no source loaded)", StyleId::Dim);
        return;
    }

    ensure_visible(pane.height());

    const auto current = session_->current_line();
    const int gutter = gutter_width();
    const int text_width = std::max(0, pane.width() - gutter);

    for (int row = 0; row < pane.height(); ++row) {
        const u32 line = top_ + static_cast<u32>(row);
        if (line > total) break;

        const bool is_current = current.has_value() && *current == line;
        const bool has_breakpoint = session_->breakpoints().has_breakpoint_on_line(line);

        // The current line gets a background wash first; text drawn afterwards
        // keeps its own colours on top of it.
        if (is_current) pane.fill_row(row, StyleId::CurrentLine);

        int column = 0;
        pane.put_at(column, row, has_breakpoint ? "●" : " ",
                    has_breakpoint ? StyleId::Breakpoint : StyleId::Normal);
        pane.put_at(column, row, is_current ? "▶" : " ", StyleId::Arrow);

        char number[16];
        std::snprintf(number, sizeof number, "%*u", gutter - 3, line);
        pane.put_at(column, row, number,
                    line == selected_ ? StyleId::Normal : StyleId::LineNumber);
        pane.put_at(column, row, "│", StyleId::Frame);

        const std::string_view text = source.line(line);
        if (text.empty() || text_width <= 0) continue;

        // Plain text first, then the styled runs over the top. Simpler than
        // stitching spans together, and the cost is a few dozen cells.
        pane.put(gutter, row, text.substr(0, static_cast<std::size_t>(text_width)));
        for (const HighlightSpan& span : highlight_line(text, line)) {
            if (span.begin >= text.size()) continue;
            const std::size_t length =
                std::min<std::size_t>(span.end, text.size()) - span.begin;
            pane.put(gutter + span.begin, row, text.substr(span.begin, length), span.style);
        }
    }
}

}  // namespace rv::tui
