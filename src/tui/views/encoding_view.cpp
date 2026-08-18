#include "tui/views/encoding_view.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "isa/decode.hpp"
#include "isa/disasm.hpp"
#include "isa/fields.hpp"
#include "isa/regnames.hpp"

namespace rv::tui {
namespace {

struct BitField {
    std::string label;   // what the field is called in the spec
    std::string detail;  // what it means for this particular instruction
    int hi = 0;
    int lo = 0;
    StyleId style = StyleId::Normal;

    int bits() const { return hi - lo + 1; }
};

std::string hex(u32 value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%x", value);
    return buffer;
}

std::string register_detail(RegIdx reg) {
    return std::string(isa::numeric_name(reg)) + " (" + std::string(isa::abi_name(reg)) + ")";
}

/// The fields of an instruction, left to right, in the layout its format
/// actually uses. This is the whole of the format knowledge in this file --
/// everything else is generic drawing.
std::vector<BitField> fields_of(const isa::DecodedInstr& instr) {
    const isa::InstrDesc& desc = instr.desc();
    const u32 word = instr.raw;

    const BitField opcode{"opcode", hex(bits(word, 6, 0)) + " " + std::string(desc.mnemonic), 6, 0,
                          StyleId::Mnemonic};
    const BitField funct3{"funct3", hex(bits(word, 14, 12)), 14, 12, StyleId::Dim};
    const BitField funct7{"funct7", hex(bits(word, 31, 25)), 31, 25, StyleId::Dim};
    const BitField rd{"rd", register_detail(instr.rd), 11, 7, StyleId::Register};
    const BitField rs1{"rs1", register_detail(instr.rs1), 19, 15, StyleId::Register};
    const BitField rs2{"rs2", register_detail(instr.rs2), 24, 20, StyleId::Register};

    switch (desc.format) {
        case isa::InstrFormat::R:
            return {funct7, rs2, rs1, funct3, rd, opcode};

        case isa::InstrFormat::I: {
            // The shift-immediates and the CSR instructions reuse the I-type
            // immediate slot for something else, so they get their own labels.
            if (desc.syntax == isa::OperandSyntax::RD_RS1_SHAMT) {
                return {{"funct7", hex(bits(word, 31, 25)), 31, 25, StyleId::Dim},
                        {"shamt", std::to_string(instr.imm), 24, 20, StyleId::Number},
                        rs1,
                        funct3,
                        rd,
                        opcode};
            }
            if (desc.syntax == isa::OperandSyntax::RD_CSR_RS1 ||
                desc.syntax == isa::OperandSyntax::RD_CSR_ZIMM) {
                const bool immediate = desc.syntax == isa::OperandSyntax::RD_CSR_ZIMM;
                return {{"csr", hex(instr.csr), 31, 20, StyleId::Number},
                        immediate ? BitField{"zimm", std::to_string(instr.imm), 19, 15,
                                             StyleId::Number}
                                  : rs1,
                        funct3,
                        rd,
                        opcode};
            }
            return {{"imm[11:0]", std::to_string(instr.imm), 31, 20, StyleId::Number},
                    rs1,
                    funct3,
                    rd,
                    opcode};
        }

        case isa::InstrFormat::S:
            return {{"imm[11:5]", "", 31, 25, StyleId::Number},
                    rs2,
                    rs1,
                    funct3,
                    {"imm[4:0]", std::to_string(instr.imm), 11, 7, StyleId::Number},
                    opcode};

        case isa::InstrFormat::B:
            // The scrambled one. Naming the pieces is the point: bit 12 and
            // bit 11 do not sit where anyone expects, and seeing that is worth
            // more than any amount of prose about it.
            return {{"imm[12|10:5]", "", 31, 25, StyleId::Number},
                    rs2,
                    rs1,
                    funct3,
                    {"imm[4:1|11]", std::to_string(instr.imm), 11, 7, StyleId::Number},
                    opcode};

        case isa::InstrFormat::U:
            return {{"imm[31:12]", hex(static_cast<u32>(instr.imm) >> 12), 31, 12,
                     StyleId::Number},
                    rd,
                    opcode};

        case isa::InstrFormat::J:
            return {{"imm[20|10:1|11|19:12]", std::to_string(instr.imm), 31, 12, StyleId::Number},
                    rd,
                    opcode};
    }
    return {opcode};
}

std::string binary_of(u32 word, int hi, int lo) {
    std::string out;
    for (int index = hi; index >= lo; --index) out.push_back(bit(word, index) != 0 ? '1' : '0');
    return out;
}

/// Centre `text` in `width`, truncating rather than overflowing.
std::string centred(const std::string& text, int width) {
    if (static_cast<int>(text.size()) >= width) return text.substr(0, static_cast<std::size_t>(width));
    const int left = (width - static_cast<int>(text.size())) / 2;
    return std::string(static_cast<std::size_t>(left), ' ') + text +
           std::string(static_cast<std::size_t>(width - left) - text.size(), ' ');
}

/// "31       12" -- the bit range, pushed to the two ends of the field.
std::string range_label(int hi, int lo, int width) {
    const std::string high = std::to_string(hi);
    const std::string low = std::to_string(lo);
    if (static_cast<int>(high.size() + low.size()) >= width) return centred(high, width);
    const std::size_t gap = static_cast<std::size_t>(width) - high.size() - low.size();
    return high + std::string(gap, ' ') + low;
}

}  // namespace

void EncodingView::draw(Pane pane) {
    if (pane.height() <= 0) return;

    const Addr address = shown_address();
    const Word word = session_->read_imem_word(address);
    const isa::DecodedInstr instr = isa::decode(word);

    int row = 0;

    // Heading: where, what bits, and what it disassembles to.
    {
        int column = 0;
        char heading[64];
        std::snprintf(heading, sizeof heading, "0x%08x  ", address);
        pane.put_at(column, row, heading, StyleId::Dim);
        std::snprintf(heading, sizeof heading, "%08x  ", word);
        pane.put_at(column, row, heading, StyleId::Number);
        pane.put_at(column, row, isa::disassemble_word(word, address), StyleId::Mnemonic);
    }
    ++row;

    if (!instr.valid()) {
        pane.put(1, row, "not a valid instruction encoding", StyleId::Error);
        return;
    }

    const std::vector<BitField> fields = fields_of(instr);

    // Each field is at least as wide as its bit count; the separators between
    // them are what makes the boundaries visible.
    std::vector<int> widths;
    widths.reserve(fields.size());
    int total = 1;
    for (const BitField& field : fields) {
        // Five columns minimum, so that a range label like "14 12" fits even
        // on a three-bit field. Showing only one end of the range would be
        // worse than showing none.
        const int width = std::max(field.bits(), 5);
        widths.push_back(width);
        total += width + 1;
    }
    const bool room_for_box = pane.width() >= total && pane.height() - row >= 4;

    if (room_for_box) {
        // ┌────┬────┐ / │31 12│ / │0101│ / └────┴────┘
        std::string top = "┌";
        std::string bottom = "└";
        for (std::size_t i = 0; i < fields.size(); ++i) {
            for (int j = 0; j < widths[i]; ++j) {
                top += "─";
                bottom += "─";
            }
            top += (i + 1 == fields.size()) ? "┐" : "┬";
            bottom += (i + 1 == fields.size()) ? "┘" : "┴";
        }
        pane.put(0, row++, top, StyleId::Frame);

        int column = 0;
        pane.put_at(column, row, "│", StyleId::Frame);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            pane.put_at(column, row, range_label(fields[i].hi, fields[i].lo, widths[i]),
                        StyleId::Dim);
            pane.put_at(column, row, "│", StyleId::Frame);
        }
        ++row;

        column = 0;
        pane.put_at(column, row, "│", StyleId::Frame);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            pane.put_at(column, row,
                        centred(binary_of(word, fields[i].hi, fields[i].lo), widths[i]),
                        fields[i].style);
            pane.put_at(column, row, "│", StyleId::Frame);
        }
        ++row;

        pane.put(0, row++, bottom, StyleId::Frame);
    }

    // Field meanings, packed as many to a line as fit.
    {
        int column = 1;
        for (const BitField& field : fields) {
            // A field with no detail still gets named. The halves of a
            // B-type or J-type immediate carry no value of their own, and
            // seeing that `imm[12|10:5]` is a *thing* is exactly the point.
            const std::string text =
                field.detail.empty() ? field.label : field.label + "=" + field.detail;
            if (column + static_cast<int>(text.size()) + 2 > pane.width()) {
                ++row;
                column = 1;
            }
            if (row >= pane.height()) return;
            pane.put_at(column, row, text, field.style);
            column += 3;
        }
        ++row;
    }

    if (row >= pane.height()) return;

    // What it did, or would do. This is the line that connects the bits to the
    // machine.
    const core::StepOutcome& last = session_->last_step();
    if (last.pc_before == address && last.reg_written != core::kNoReg) {
        char effect[128];
        std::snprintf(effect, sizeof effect, "→ %s = 0x%08x   (was 0x%08x)",
                      isa::abi_name(last.reg_written).data(),
                      session_->cpu().x[last.reg_written], last.reg_old);
        pane.put(1, row, effect, StyleId::ChangedValue);
    } else if (last.pc_before == address && last.mem_written != core::kNoAddr) {
        char effect[128];
        std::snprintf(effect, sizeof effect, "→ [0x%08x] = 0x%08x   (was 0x%08x)",
                      last.mem_written, session_->read_dmem_word(last.mem_written), last.mem_old);
        pane.put(1, row, effect, StyleId::ChangedValue);
    } else if (const auto entry = session_->map().at(address)) {
        // Not executed yet: say where it came from instead, which is what makes
        // this useful while still editing.
        char origin[128];
        if (entry->count > 1) {
            std::snprintf(origin, sizeof origin, "from line %u, word %u of %u", entry->line,
                          entry->slot + 1, entry->count);
        } else {
            std::snprintf(origin, sizeof origin, "from line %u", entry->line);
        }
        pane.put(1, row, origin, StyleId::Dim);
    }
}

}  // namespace rv::tui
