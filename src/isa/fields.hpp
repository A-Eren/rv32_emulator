// Bit-field layout of a 32-bit RISC-V instruction word.
//
// Every immediate has an extractor and a matching inserter, and
// unit_fields.cpp round-trips each pair over random values. That round-trip is
// the only thing standing between us and a scrambled B-type immediate, which is
// the single most common bug in a hand-written RISC-V implementation (bit 11
// and bit 12 swap places relative to every other format).
#pragma once

#include "types.hpp"

namespace rv::isa {

// ---- register and opcode fields -------------------------------------------

constexpr u32 opcode_of(Word w) { return bits(w, 6, 0); }
constexpr RegIdx rd_of(Word w) { return static_cast<RegIdx>(bits(w, 11, 7)); }
constexpr u32 funct3_of(Word w) { return bits(w, 14, 12); }
constexpr RegIdx rs1_of(Word w) { return static_cast<RegIdx>(bits(w, 19, 15)); }
constexpr RegIdx rs2_of(Word w) { return static_cast<RegIdx>(bits(w, 24, 20)); }
constexpr u32 funct7_of(Word w) { return bits(w, 31, 25); }

/// Shift amount for slli/srli/srai: the low 5 bits of the I-immediate field.
constexpr u32 shamt_of(Word w) { return bits(w, 24, 20); }
/// CSR address for the Zicsr instructions: the whole I-immediate field.
constexpr CsrAddr csr_of(Word w) { return static_cast<CsrAddr>(bits(w, 31, 20)); }
/// 5-bit *zero*-extended immediate for csrrwi/csrrsi/csrrci, in the rs1 slot.
constexpr u32 zimm_of(Word w) { return bits(w, 19, 15); }

// ---- immediate extraction --------------------------------------------------

constexpr i32 imm_i(Word w) { return sign_extend(bits(w, 31, 20), 12); }

constexpr i32 imm_s(Word w) {
    return sign_extend((bits(w, 31, 25) << 5) | bits(w, 11, 7), 12);
}

constexpr i32 imm_b(Word w) {
    // imm[12|10:5] live in [31:25], imm[4:1|11] in [11:7], and imm[0] is
    // implicitly zero (branch targets are 2-byte aligned).
    const u32 value = (bit(w, 31) << 12) | (bit(w, 7) << 11) | (bits(w, 30, 25) << 5) |
                      (bits(w, 11, 8) << 1);
    return sign_extend(value, 13);
}

constexpr i32 imm_u(Word w) {
    // Already positioned at bits 31:12; no shift and no sign extension.
    return static_cast<i32>(w & 0xffff'f000u);
}

constexpr i32 imm_j(Word w) {
    const u32 value = (bit(w, 31) << 20) | (bits(w, 19, 12) << 12) | (bit(w, 20) << 11) |
                      (bits(w, 30, 21) << 1);
    return sign_extend(value, 21);
}

// ---- immediate insertion ---------------------------------------------------
// Each inserter returns only its own bits, to be OR-ed into a word alongside
// the opcode and register fields. Range checking is the caller's job (the
// assembler reports it as a diagnostic pointing at the operand).

constexpr Word put_rd(RegIdx r) { return static_cast<Word>(r & 31u) << 7; }
constexpr Word put_rs1(RegIdx r) { return static_cast<Word>(r & 31u) << 15; }
constexpr Word put_rs2(RegIdx r) { return static_cast<Word>(r & 31u) << 20; }
constexpr Word put_shamt(u32 amount) { return (amount & 31u) << 20; }
constexpr Word put_csr(CsrAddr csr) { return (static_cast<Word>(csr) & 0xfffu) << 20; }
constexpr Word put_zimm(u32 value) { return (value & 31u) << 15; }

constexpr Word put_imm_i(i32 imm) {
    return (static_cast<Word>(imm) & 0xfffu) << 20;
}

constexpr Word put_imm_s(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bits(v, 11, 5) << 25) | (bits(v, 4, 0) << 7);
}

constexpr Word put_imm_b(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 12) << 31) | (bits(v, 10, 5) << 25) | (bits(v, 4, 1) << 8) | (bit(v, 11) << 7);
}

constexpr Word put_imm_u(i32 imm) { return static_cast<Word>(imm) & 0xffff'f000u; }

constexpr Word put_imm_j(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 20) << 31) | (bits(v, 10, 1) << 21) | (bit(v, 11) << 20) |
           (bits(v, 19, 12) << 12);
}

}  // namespace rv::isa
