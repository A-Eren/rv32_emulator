#include "tui/highlight.hpp"

#include "asm/lexer.hpp"
#include "asm/pseudo.hpp"
#include "isa/isa.hpp"

namespace rv::tui {

StyleId style_for_token(const as::Token& token) {
    switch (token.kind) {
        case as::TokenKind::Comment: return StyleId::Comment;
        case as::TokenKind::Label: return StyleId::Label;
        case as::TokenKind::Directive: return StyleId::Directive;
        case as::TokenKind::Register: return StyleId::Register;
        case as::TokenKind::Number: return StyleId::Number;
        case as::TokenKind::StringLit: return StyleId::StringLit;
        case as::TokenKind::Error: return StyleId::Error;

        case as::TokenKind::Ident:
            // An identifier is only a mnemonic if the tables say so. That means
            // `addii` stops looking like an instruction the instant the second
            // 'i' is typed, which is a better signal than any squiggle.
            if (isa::lookup_mnemonic(token.text) != isa::kInvalidInstr) return StyleId::Mnemonic;
            if (as::is_pseudo_spelling(token.text)) return StyleId::Mnemonic;
            return StyleId::Normal;

        case as::TokenKind::End:
            return StyleId::Normal;

        default:
            // Punctuation and operators.
            return StyleId::Punctuation;
    }
}

std::vector<HighlightSpan> highlight_line(std::string_view text, u32 line_number) {
    std::vector<HighlightSpan> spans;
    for (const as::Token& token : as::tokenize_for_highlight(text, line_number)) {
        if (token.kind == as::TokenKind::End) break;
        const StyleId style = style_for_token(token);
        if (style == StyleId::Normal) continue;  // leave plain text unstyled
        spans.push_back(HighlightSpan{token.span.col, token.span.end_col(), style});
    }
    return spans;
}

}  // namespace rv::tui
