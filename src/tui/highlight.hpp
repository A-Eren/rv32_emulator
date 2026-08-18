// Syntax highlighting for RISC-V assembly.
//
// It runs the assembler's own lexer in tolerant mode. That is the whole design:
// because only one thing decides what a token is, the colours on screen and the
// assembler's reading of the text cannot disagree. "Green in the editor but a
// syntax error on F5" is structurally impossible rather than merely unlikely.
//
// Only the visible rows are ever highlighted -- at most a couple of dozen lines
// per frame -- so there is no cache and no invalidation to get wrong.
#pragma once

#include <string_view>
#include <vector>

#include "asm/token.hpp"
#include "tui/theme.hpp"

namespace rv::tui {

/// A styled run of text within one line, in byte offsets.
struct HighlightSpan {
    u16 begin = 0;
    u16 end = 0;
    StyleId style = StyleId::Normal;
};

/// Highlight one line. `line_number` only affects the spans' provenance, not
/// the result, so a detached line can be highlighted with any number.
std::vector<HighlightSpan> highlight_line(std::string_view text, u32 line_number = 1);

/// The style a token kind is drawn in. Instruction mnemonics are distinguished
/// from other identifiers by looking them up, so a typo stops looking like a
/// keyword the moment it stops being one.
StyleId style_for_token(const as::Token& token);

}  // namespace rv::tui
