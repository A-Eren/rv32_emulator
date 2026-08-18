#include "tui/views/regs_view.hpp"

#include <cstdio>

#include "isa/regnames.hpp"

namespace rv::tui {

void RegsView::note_step() {
    have_history_ = true;
    for (const dbg::RegChange& change : session_->last_changes()) {
        changed_at_[change.index] = session_->cpu().instret;
    }
}

void RegsView::forget_history() {
    changed_at_.fill(0);
    have_history_ = false;
}

StyleId RegsView::style_for_value(RegIdx reg) const {
    if (have_history_ && changed_at_[reg] != 0) {
        const u64 now = session_->cpu().instret;
        const u64 age = now >= changed_at_[reg] ? now - changed_at_[reg] : 0;
        if (age == 0) return StyleId::ChangedValue;
        if (age < kFadeSteps) return StyleId::RecentValue;
    }
    // Dimming the zeros is what makes the two or three registers a small
    // program actually uses stand out of a wall of 32.
    return session_->cpu().x[reg] == 0 ? StyleId::ZeroValue : StyleId::Normal;
}

void RegsView::draw(Pane pane) {
    if (pane.height() <= 0) return;

    // Cell: marker, numeric name, ABI name, value. 18 columns, so four fit in
    // 80 and three in 60. The ABI name gets five columns rather than four
    // because "zero" would otherwise run straight into its own value.
    constexpr int kCellWidth = 18;
    const int per_row = std::max(1, pane.width() / kCellWidth);
    const int value_rows = (kNumRegs + per_row - 1) / per_row;
    const bool room_for_summary = pane.height() > value_rows;

    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        const int row = reg / per_row;
        if (row >= pane.height()) break;
        int column = (reg % per_row) * kCellWidth;

        const StyleId value_style = style_for_value(reg);
        const bool just_changed = value_style == StyleId::ChangedValue;

        pane.put_at(column, row, just_changed ? "*" : " ",
                    just_changed ? StyleId::ChangedValue : StyleId::Normal);

        char names[16];
        std::snprintf(names, sizeof names, "%-3s %-5s", isa::numeric_name(reg).data(),
                      isa::abi_name(reg).data());
        pane.put_at(column, row, names, StyleId::Dim);

        char value[16];
        std::snprintf(value, sizeof value, "%08x", session_->cpu().x[reg]);
        pane.put_at(column, row, value, value_style);
    }

    if (!room_for_summary) return;

    const core::CpuState& cpu = session_->cpu();
    char summary[128];
    std::snprintf(summary, sizeof summary, " pc %08x   cycle %llu   instret %llu", cpu.pc,
                  static_cast<unsigned long long>(cpu.cycle),
                  static_cast<unsigned long long>(cpu.instret));
    pane.put(0, value_rows, summary, StyleId::Dim);
}

}  // namespace rv::tui
