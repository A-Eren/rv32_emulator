#include "core/memfile.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "isa/disasm.hpp"

namespace rv::core {
namespace {

MemFileResult failure(std::string message) { return MemFileResult{false, std::move(message)}; }
MemFileResult success() { return MemFileResult{true, {}}; }

/// Strip `//` and `#` comments and surrounding whitespace.
std::string clean_line(const std::string& line) {
    std::string text = line;
    const std::size_t comment = text.find_first_of("#");
    const std::size_t slashes = text.find("//");
    const std::size_t cut = std::min(comment, slashes);
    if (cut != std::string::npos) text.erase(cut);

    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

std::string render_mem_file(const std::vector<Word>& words, const MemFileOptions& options) {
    std::size_t count = words.size();
    if (options.pad_to_full && options.full_size > count) count = options.full_size;

    std::string out;
    out.reserve(count * (options.annotate ? 40 : 9));

    char buffer[128];
    for (std::size_t i = 0; i < count; ++i) {
        const Word word = i < words.size() ? words[i] : 0u;
        if (options.annotate) {
            const std::string text =
                isa::disassemble_word(word, static_cast<Addr>(i * 4));
            std::snprintf(buffer, sizeof buffer, "%08x  // %04zx: %s\n", word, i * 4, text.c_str());
        } else {
            std::snprintf(buffer, sizeof buffer, "%08x\n", word);
        }
        out += buffer;
    }
    return out;
}

MemFileResult write_mem_file(const std::string& path, const std::vector<Word>& words,
                             const MemFileOptions& options) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return failure("cannot open '" + path + "' for writing");
    file << render_mem_file(words, options);
    if (!file) return failure("write to '" + path + "' failed");
    return success();
}

MemFileResult parse_mem_file(const std::string& text, const std::string& source_name,
                             std::vector<Word>& out) {
    out.clear();
    std::istringstream stream(text);
    std::string line;
    std::size_t line_number = 0;
    std::size_t cursor = 0;

    while (std::getline(stream, line)) {
        ++line_number;
        const std::string cleaned = clean_line(line);
        if (cleaned.empty()) continue;

        std::istringstream tokens(cleaned);
        std::string token;
        while (tokens >> token) {
            if (token[0] == '@') {
                // An address directive is a *word* index in $readmemh, matching
                // how the array is declared.
                char* end = nullptr;
                const unsigned long long index = std::strtoull(token.c_str() + 1, &end, 16);
                if (end == token.c_str() + 1 || *end != '\0') {
                    return failure(source_name + ":" + std::to_string(line_number) +
                                   ": malformed address directive '" + token + "'");
                }
                cursor = static_cast<std::size_t>(index);
                continue;
            }

            char* end = nullptr;
            const unsigned long long value = std::strtoull(token.c_str(), &end, 16);
            if (end == token.c_str() || *end != '\0') {
                return failure(source_name + ":" + std::to_string(line_number) +
                               ": '" + token + "' is not a hex word");
            }
            if (value > 0xffff'ffffull) {
                return failure(source_name + ":" + std::to_string(line_number) + ": '" + token +
                               "' does not fit in 32 bits");
            }
            if (out.size() <= cursor) out.resize(cursor + 1, 0u);
            out[cursor] = static_cast<Word>(value);
            ++cursor;
        }
    }
    return success();
}

MemFileResult read_mem_file(const std::string& path, std::vector<Word>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return failure("cannot open '" + path + "'");
    std::ostringstream contents;
    contents << file.rdbuf();
    return parse_mem_file(contents.str(), path, out);
}

std::vector<Word> pack_bytes_to_words(const std::vector<u8>& bytes) {
    std::vector<Word> words((bytes.size() + 3) / 4, 0u);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        words[i / 4] |= static_cast<Word>(bytes[i]) << (8 * (i % 4));  // little-endian
    }
    return words;
}

}  // namespace rv::core
