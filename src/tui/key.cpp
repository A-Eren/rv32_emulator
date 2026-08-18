#include "tui/key.hpp"

#include <array>
#include <cctype>
#include <string_view>

namespace rv::tui {
namespace {

struct NamedKey {
    std::string_view name;
    KeyCode code;
};

constexpr std::array<NamedKey, 29> kNamedKeys = {{
    {"Enter", KeyCode::Enter},       {"Esc", KeyCode::Escape},
    {"Backspace", KeyCode::Backspace}, {"Delete", KeyCode::Delete},
    {"Tab", KeyCode::Tab},           {"BackTab", KeyCode::BackTab},
    {"Up", KeyCode::Up},             {"Down", KeyCode::Down},
    {"Left", KeyCode::Left},         {"Right", KeyCode::Right},
    {"Home", KeyCode::Home},         {"End", KeyCode::End},
    {"PageUp", KeyCode::PageUp},     {"PageDown", KeyCode::PageDown},
    {"Insert", KeyCode::Insert},     {"Resize", KeyCode::Resize},
    {"F1", KeyCode::F1},             {"F2", KeyCode::F2},
    {"F3", KeyCode::F3},             {"F4", KeyCode::F4},
    {"F5", KeyCode::F5},             {"F6", KeyCode::F6},
    {"F7", KeyCode::F7},             {"F8", KeyCode::F8},
    {"F9", KeyCode::F9},             {"F10", KeyCode::F10},
    {"F11", KeyCode::F11},           {"F12", KeyCode::F12},
    {"Space", KeyCode::Char},
}};

}  // namespace

std::string key_name(const Key& key) {
    if (key.code == KeyCode::Char) {
        std::string out;
        if (key.alt) out += "M-";
        if (key.ctrl) {
            out += "^";
            out += static_cast<char>(std::toupper(static_cast<int>(key.ch)));
            return out;
        }
        if (key.ch == ' ') return out + "Space";
        out += static_cast<char>(key.ch);
        return out;
    }
    for (const NamedKey& named : kNamedKeys) {
        if (named.code == key.code && named.name != "Space") return std::string(named.name);
    }
    return "?";
}

bool parse_key(std::string_view text, Key& out) {
    if (text.empty()) return false;

    if (text == "Space") {
        out = Key::character(' ');
        return true;
    }
    if (text.size() >= 2 && text[0] == '^') {
        out = Key::control(static_cast<u32>(text[1]));
        return true;
    }
    if (text.size() > 2 && text.compare(0, 2, "M-") == 0) {
        Key inner;
        if (!parse_key(text.substr(2), inner)) return false;
        inner.alt = true;
        out = inner;
        return true;
    }
    for (const NamedKey& named : kNamedKeys) {
        if (named.name == text && named.name != "Space") {
            out = Key::special(named.code);
            return true;
        }
    }
    if (text.size() == 1) {
        out = Key::character(static_cast<u32>(static_cast<unsigned char>(text[0])));
        return true;
    }
    return false;
}

}  // namespace rv::tui
