// Turning an instruction and its operands into a 32-bit word.
//
// The exact inverse of decode(): `encode(id, operands_from(decode(w))) == w`
// for every valid w, which unit_decode.cpp checks over hundreds of thousands
// of random words. That property is what keeps the assembler and the
// disassembler from drifting apart.
#pragma once

#include "isa/decode.hpp"
#include "isa/isa.hpp"

namespace rv::isa {

struct Operands {
    RegIdx rd = 0;
    RegIdx rs1 = 0;
    RegIdx rs2 = 0;
    /// For U-type this is the *already shifted* value (low 12 bits zero), to
    /// match DecodedInstr::imm. The assembler shifts the written operand --
    /// `lui a0, 0x12345` -- into place before calling.
    i32 imm = 0;
    CsrAddr csr = 0;
};

enum class EncodeError : u8 {
    None,
    ImmediateOutOfRange,
    ImmediateMisaligned,  // branch and jump targets must be 2-byte aligned
    ImmediateNotShifted,  // U-type operand had non-zero low 12 bits
    ShiftAmountOutOfRange,
    CsrOutOfRange,
    RegisterOutOfRange,
};

/// On failure, `field` names the offending operand and [min, max] is its legal
/// range, so the assembler can render a diagnostic without knowing the encoding
/// rules itself.
struct EncodeResult {
    Word word = 0;
    EncodeError error = EncodeError::None;
    const char* field = "";
    i64 value = 0;
    i64 min = 0;
    i64 max = 0;

    bool ok() const { return error == EncodeError::None; }
};

EncodeResult encode(InstrId id, const Operands& operands);

/// Recover the operands of an already-decoded instruction, for round-tripping.
Operands operands_of(const DecodedInstr& instr);

const char* encode_error_message(EncodeError error);

}  // namespace rv::isa
