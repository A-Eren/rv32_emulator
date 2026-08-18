#include "tui/views/panels.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace rv::tui {

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

void MemView::follow_last_store() {
    const core::StepOutcome& last = session_->last_step();
    if (last.mem_written == core::kNoAddr) return;
    highlight_ = last.mem_written;
    // Only scroll if the write landed outside what is already on screen;
    // jumping on every store would make a loop unreadable.
    if (last.mem_written < base_ || last.mem_written >= base_ + 0x80u) {
        base_ = last.mem_written & ~0xfu;
    }
}

bool MemView::on_key(const Key& key) {
    switch (key.code) {
        case KeyCode::Up: base_ = base_ >= 0x10u ? base_ - 0x10u : 0u; return true;
        case KeyCode::Down: base_ += 0x10u; return true;
        case KeyCode::PageUp: base_ = base_ >= 0x100u ? base_ - 0x100u : 0u; return true;
        case KeyCode::PageDown: base_ += 0x100u; return true;
        case KeyCode::Home: base_ = 0; return true;
        default: return false;
    }
}

void MemView::draw(Pane pane) {
    if (pane.height() <= 0) return;

    // Four words per row is 8 + 2 + 4*9 + 1 + 16 + 1 = 64 columns, which leaves
    // breathing room at 80 and reads better than cramming in eight.
    const int per_row = pane.width() >= 100 ? 8 : 4;

    for (int row = 0; row < pane.height(); ++row) {
        const Addr row_addr = base_ + static_cast<Addr>(row * per_row * 4);
        int column = 0;

        char address[16];
        std::snprintf(address, sizeof address, "%08x ", row_addr);
        pane.put_at(column, row, address, StyleId::Dim);

        std::string ascii;
        for (int index = 0; index < per_row; ++index) {
            const Addr at = row_addr + static_cast<Addr>(index * 4);
            const u32 value = session_->read_dmem_word(at);

            char cell[16];
            std::snprintf(cell, sizeof cell, "%08x ", value);
            const bool changed = at == highlight_;
            pane.put_at(column, row, cell,
                        changed ? StyleId::ChangedValue
                                : (value == 0 ? StyleId::ZeroValue : StyleId::Normal));

            for (int byte = 0; byte < 4; ++byte) {
                const auto character = static_cast<char>((value >> (8 * byte)) & 0xffu);
                ascii.push_back(character >= 0x20 && character < 0x7f ? character : '.');
            }
        }
        pane.put_at(column, row, "|" + ascii + "|", StyleId::Dim);
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

void ConsoleView::draw(Pane pane) {
    if (pane.height() <= 0 || pane.width() <= 0) return;

    const std::string& output = session_->uart_output();
    if (output.empty()) {
        pane.put(0, 0, "(nothing written to the uart yet)", StyleId::Dim);
        return;
    }

    // Wrap, then show the tail: what a program just printed matters more than
    // what it printed first.
    std::vector<std::string> rows;
    std::string current;
    for (const char character : output) {
        if (character == '\n' || static_cast<int>(current.size()) >= pane.width()) {
            rows.push_back(current);
            current.clear();
            if (character == '\n') continue;
        }
        current.push_back(character >= 0x20 || character == '\t' ? character : '.');
    }
    if (!current.empty()) rows.push_back(current);

    const int first = std::max(0, static_cast<int>(rows.size()) - pane.height());
    for (int row = 0; row + first < static_cast<int>(rows.size()) && row < pane.height(); ++row) {
        pane.put(0, row, rows[static_cast<std::size_t>(first + row)]);
    }
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

void MmioView::draw(Pane pane) {
    if (pane.height() <= 0) return;
    const core::Bus& bus = session_->hart().bus();

    int row = 0;
    const auto line = [&](const char* label, Addr addr, u32 value, const char* note) {
        if (row >= pane.height()) return;
        char text[160];
        std::snprintf(text, sizeof text, "%-10s 0x%08x  %08x  %s", label, addr, value, note);
        pane.put(0, row++, text, value == 0 ? StyleId::ZeroValue : StyleId::Normal);
    };

    line("leds", 0xffff0010u, bus.leds().value(), "output register");
    line("switches", 0xffff0020u, bus.switches().value(), "input register");
    line("cycles", 0xffff0030u, static_cast<u32>(session_->cpu().cycle), "mirrors the counter");

    if (row < pane.height()) {
        char text[128];
        std::snprintf(text, sizeof text, "uart       0xffff0000  %8zu  bytes written",
                      session_->uart_output().size());
        pane.put(0, row++, text, StyleId::Dim);
    }

    // A little binary strip, because the point of an LED register is to be
    // looked at rather than read as hex.
    if (row < pane.height()) {
        std::string bits;
        for (int index = 31; index >= 0; --index) {
            bits.push_back(((bus.leds().value() >> index) & 1u) != 0 ? '#' : '.');
        }
        pane.put(0, row, "leds  " + bits, StyleId::Number);
    }
}

// ---------------------------------------------------------------------------
// Symbols
// ---------------------------------------------------------------------------

bool SymbolsView::on_key(const Key& key) {
    switch (key.code) {
        case KeyCode::Up: top_ = std::max(0, top_ - 1); return true;
        case KeyCode::Down: ++top_; return true;
        default: return false;
    }
}

void SymbolsView::draw(Pane pane) {
    if (pane.height() <= 0) return;

    const auto& symbols = session_->symbols().all();
    if (symbols.empty()) {
        pane.put(0, 0, "(no symbols)", StyleId::Dim);
        return;
    }
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(symbols.size()) - 1));

    pane.put(0, 0, " space     value       line  name", StyleId::Dim);
    for (int row = 1; row < pane.height(); ++row) {
        const std::size_t index = static_cast<std::size_t>(top_ + row - 1);
        if (index >= symbols.size()) break;
        const as::Symbol& symbol = symbols[index];

        char text[192];
        std::snprintf(text, sizeof text, " %-8s  0x%08x  %5u  %s%s",
                      as::space_name(symbol.space), symbol.value, symbol.definition.line,
                      symbol.name.c_str(), symbol.is_global ? "  (global)" : "");
        // Colour by space: this is the distinction that explains why jumping to
        // a data symbol was rejected.
        pane.put(0, row, text,
                 symbol.space == as::Space::Data ? StyleId::Number : StyleId::Label);
    }
}

}  // namespace rv::tui
