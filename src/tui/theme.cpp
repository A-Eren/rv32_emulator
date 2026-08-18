#include "tui/theme.hpp"

#include <array>

namespace rv::tui {
namespace {

// 256-colour indices. Chosen to stay legible on the common dark terminal
// palettes rather than to be pretty on one of them.
constexpr short kGrey = 244;
constexpr short kBlue = 75;
constexpr short kCyan = 80;
constexpr short kGreen = 114;
constexpr short kYellow = 221;
constexpr short kOrange = 215;
constexpr short kRed = 203;
constexpr short kMagenta = 176;
constexpr short kWhite = 253;
constexpr short kPanel = 236;

using Palette = std::array<Style, static_cast<std::size_t>(StyleId::Count)>;

Palette make_dark() {
    Palette palette{};
    const auto set = [&palette](StyleId id, Style style) {
        palette[static_cast<std::size_t>(id)] = style;
    };

    set(StyleId::Normal, Style{-1, -1, Attr::None});
    set(StyleId::Dim, Style{kGrey, -1, Attr::None});
    set(StyleId::Frame, Style{kGrey, -1, Attr::None});
    set(StyleId::Title, Style{kCyan, -1, Attr::Bold});
    set(StyleId::StatusBar, Style{kWhite, kPanel, Attr::None});
    set(StyleId::StatusAccent, Style{kYellow, kPanel, Attr::Bold});
    set(StyleId::Hint, Style{kGrey, -1, Attr::None});

    set(StyleId::LineNumber, Style{kGrey, -1, Attr::None});
    set(StyleId::Mnemonic, Style{kBlue, -1, Attr::Bold});
    set(StyleId::Register, Style{kCyan, -1, Attr::None});
    set(StyleId::Number, Style{kOrange, -1, Attr::None});
    set(StyleId::Label, Style{kGreen, -1, Attr::Bold});
    set(StyleId::Directive, Style{kMagenta, -1, Attr::None});
    set(StyleId::Comment, Style{kGrey, -1, Attr::None});
    set(StyleId::StringLit, Style{kGreen, -1, Attr::None});
    set(StyleId::Punctuation, Style{kWhite, -1, Attr::None});

    set(StyleId::CurrentLine, Style{-1, 237, Attr::None});
    set(StyleId::Arrow, Style{kYellow, -1, Attr::Bold});
    set(StyleId::Breakpoint, Style{kRed, -1, Attr::Bold});
    // The whole point of the tool: a value that just changed has to be the
    // first thing the eye lands on.
    set(StyleId::ChangedValue, Style{kYellow, -1, Attr::Bold});
    set(StyleId::RecentValue, Style{kOrange, -1, Attr::None});
    set(StyleId::ZeroValue, Style{kGrey, -1, Attr::None});

    set(StyleId::Error, Style{kRed, -1, Attr::Bold});
    set(StyleId::Warning, Style{kYellow, -1, Attr::None});
    set(StyleId::Note, Style{kCyan, -1, Attr::None});
    set(StyleId::Caret, Style{kRed, -1, Attr::Bold});
    set(StyleId::ErrorSpan, Style{kRed, -1, Attr::Underline});
    set(StyleId::WarningSpan, Style{kYellow, -1, Attr::Underline});
    return palette;
}

Palette make_light() {
    Palette palette = make_dark();
    const auto set = [&palette](StyleId id, Style style) {
        palette[static_cast<std::size_t>(id)] = style;
    };
    // Mid-grey and pale yellows vanish on white, so the roles that carry
    // meaning get darker inks rather than a uniform shift.
    set(StyleId::Dim, Style{242, -1, Attr::None});
    set(StyleId::Frame, Style{246, -1, Attr::None});
    set(StyleId::LineNumber, Style{245, -1, Attr::None});
    set(StyleId::Comment, Style{28, -1, Attr::None});
    set(StyleId::Mnemonic, Style{26, -1, Attr::Bold});
    set(StyleId::Register, Style{30, -1, Attr::None});
    set(StyleId::Number, Style{130, -1, Attr::None});
    set(StyleId::Label, Style{22, -1, Attr::Bold});
    set(StyleId::Directive, Style{90, -1, Attr::None});
    set(StyleId::StringLit, Style{22, -1, Attr::None});
    set(StyleId::StatusBar, Style{234, 253, Attr::None});
    set(StyleId::StatusAccent, Style{88, 253, Attr::Bold});
    set(StyleId::CurrentLine, Style{-1, 254, Attr::None});
    set(StyleId::ChangedValue, Style{124, -1, Attr::Bold});
    set(StyleId::RecentValue, Style{130, -1, Attr::None});
    set(StyleId::ZeroValue, Style{247, -1, Attr::None});
    set(StyleId::ErrorSpan, Style{124, -1, Attr::Underline});
    set(StyleId::WarningSpan, Style{130, -1, Attr::Underline});
    return palette;
}

Palette& active() {
    static Palette palette = make_dark();
    return palette;
}

}  // namespace

const Style& style_for(StyleId id) { return active()[static_cast<std::size_t>(id)]; }

void use_light_theme(bool light) { active() = light ? make_light() : make_dark(); }

}  // namespace rv::tui
