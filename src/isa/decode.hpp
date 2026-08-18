// Instruction decoding: a 32-bit word in, a DecodedInstr out.
//
// This lives in rv_isa rather than rv_core because both siblings need it: the
// executor decodes to run, and the disassembler decodes to print.
#pragma once

#include "isa.hpp"
#include "types.hpp"

namespace rv::isa {

/// A decoded instruction with every field already extracted and sign-extended.
///
/// Which fields are meaningful depends on `desc().syntax`; the rest are zero.
/// For RD_CSR_ZIMM (`csrrwi` and friends) `imm` holds the zero-extended 5-bit
/// immediate, and `rs1` holds the same bits raw — the encoding panel shows the
/// field in its rs1 slot, while the executor wants it as a value.
struct DecodedInstr {
    Word raw = 0;
    InstrId id = kInvalidInstr;
    RegIdx rd = 0;
    RegIdx rs1 = 0;
    RegIdx rs2 = 0;
    i32 imm = 0;
    CsrAddr csr = 0;

    bool valid() const { return id != kInvalidInstr; }
    const InstrDesc& desc() const { return describe(id); }
};

/// Decode a word. An unrecognised encoding yields `id == kInvalidInstr` with
/// `raw` still populated, so the caller can raise IllegalInstruction with the
/// offending bits in mtval.
DecodedInstr decode(Word word);

}  // namespace rv::isa
