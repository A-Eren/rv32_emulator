// The instruction table, generated three ways from instr_table.def.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "types.hpp"

namespace rv::isa {

enum class InstrFormat : u8 { R, I, S, B, U, J };

/// How an instruction's operands are written in assembly and printed back out.
/// The assembler's parser and the disassembler's printer both switch over this,
/// which is why neither needs a per-instruction special case.
enum class OperandSyntax : u8 {
    NONE,          // ecall, ebreak, mret, wfi, fence
    RD_RS1_RS2,    // add   rd, rs1, rs2
    RD_RS1_IMM,    // addi  rd, rs1, imm
    RD_RS1_SHAMT,  // slli  rd, rs1, shamt
    RD_OFS_RS1,    // lw    rd, offset(rs1)   -- also jalr
    RS2_OFS_RS1,   // sw    rs2, offset(rs1)
    RS1_RS2_LBL,   // beq   rs1, rs2, label
    RD_UIMM,       // lui   rd, imm20
    RD_LBL,        // jal   rd, label
    RD_CSR_RS1,    // csrrw rd, csr, rs1
    RD_CSR_ZIMM,   // csrrwi rd, csr, zimm5
};

/// Coarse category, used for syntax highlighting colours and for grouping the
/// in-app instruction help.
enum class InstrGroup : u8 {
    ArithR,
    ArithI,
    Shift,
    Load,
    Store,
    Branch,
    Jump,
    Upper,
    MulDiv,
    Csr,
    System,
    Fence,
};

enum class InstrId : u16 {
#define RV_INSTR(id, ...) id,
#include "instr_table.def"
#undef RV_INSTR
    Count
};

inline constexpr std::size_t kInstrCount = static_cast<std::size_t>(InstrId::Count);

/// Sentinel meaning "not a valid instruction". Shares the value of `Count` so
/// that it can never collide with a real row.
inline constexpr InstrId kInvalidInstr = InstrId::Count;

struct InstrDesc {
    std::string_view mnemonic;
    InstrFormat format;
    Word match;
    Word mask;
    OperandSyntax syntax;
    InstrGroup group;
};

inline constexpr std::array<InstrDesc, kInstrCount> kInstrTable = {{
#define RV_INSTR(id, mnem, fmt, match, mask, syn, grp)                                    \
    InstrDesc{mnem, InstrFormat::fmt, (match), (mask), OperandSyntax::syn, InstrGroup::grp},
#include "instr_table.def"
#undef RV_INSTR
}};

/// Table row for a known-valid id. Passing kInvalidInstr is a programming
/// error; callers check validity first.
const InstrDesc& describe(InstrId id);

std::string_view mnemonic_of(InstrId id);

/// Look a mnemonic up by name, case-sensitive. Returns kInvalidInstr if the
/// name is not an instruction (it may still be a pseudo-instruction).
InstrId lookup_mnemonic(std::string_view name);

std::string_view format_name(InstrFormat format);
std::string_view group_name(InstrGroup group);

}  // namespace rv::isa
