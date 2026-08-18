// The encoder and the decoder must be exact inverses.
//
// This is the single strongest test in the project and it needs no golden
// files: if any field is placed at the wrong offset, extracted with the wrong
// width, or sign-extended when it should not be, the round trip breaks. It
// covers all 52 instructions at once, in both directions, and it will keep
// covering new ones for free as rows are added to instr_table.def.
#include <random>

#include "isa/decode.hpp"
#include "isa/disasm.hpp"
#include "isa/encode.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::isa;

RV_TEST(roundtrip, decode_then_encode_reproduces_the_word) {
    std::mt19937 rng(0xc0ffee);
    int checked = 0;

    for (int i = 0; i < 500000; ++i) {
        const Word word = rng();
        const DecodedInstr decoded = decode(word);
        if (!decoded.valid()) continue;

        const EncodeResult re = encode(decoded.id, operands_of(decoded));
        if (!re.ok()) {
            std::printf("  word 0x%08x (%s) failed to re-encode: %s\n", word,
                        std::string(mnemonic_of(decoded.id)).c_str(),
                        encode_error_message(re.error));
        }
        RV_CHECK(re.ok());

        // Only the bits the instruction actually defines have to match: the
        // random word may contain garbage in fields this format ignores (an
        // I-type's funct7 space, for instance). Masking by the row's own mask
        // plus the operand fields is fiddly, so instead we require that
        // re-decoding the re-encoded word yields identical operands, and that
        // the canonical form is stable under a second pass.
        const DecodedInstr again = decode(re.word);
        RV_CHECK_EQ(again.id, decoded.id);
        RV_CHECK_EQ(again.rd, decoded.rd);
        RV_CHECK_EQ(again.rs1, decoded.rs1);
        RV_CHECK_EQ(again.rs2, decoded.rs2);
        RV_CHECK_EQ(again.imm, decoded.imm);
        RV_CHECK_EQ(again.csr, decoded.csr);

        const EncodeResult twice = encode(again.id, operands_of(again));
        RV_CHECK(twice.ok());
        RV_CHECK_HEX(twice.word, re.word);  // canonical form is a fixed point

        ++checked;
    }

    // Guard against the test silently passing because nothing decoded.
    RV_CHECK(checked > 5000);
}

RV_TEST(roundtrip, canonical_words_survive_exactly) {
    // For words that are already canonical -- every ignored field zero -- the
    // round trip must be bit-exact, not merely operand-equivalent.
    std::mt19937 rng(4242);
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const auto id = static_cast<InstrId>(i);
        const InstrDesc& desc = describe(id);

        for (int attempt = 0; attempt < 200; ++attempt) {
            Operands operands;
            operands.rd = static_cast<RegIdx>(rng() % 32);
            operands.rs1 = static_cast<RegIdx>(rng() % 32);
            operands.rs2 = static_cast<RegIdx>(rng() % 32);
            operands.csr = static_cast<CsrAddr>(rng() % 4096);

            // Pick an immediate that is legal for this operand shape.
            switch (desc.syntax) {
                case OperandSyntax::RD_RS1_IMM:
                case OperandSyntax::RD_OFS_RS1:
                case OperandSyntax::RS2_OFS_RS1:
                    operands.imm = static_cast<i32>(rng() % 4096) - 2048;
                    break;
                case OperandSyntax::RD_RS1_SHAMT:
                case OperandSyntax::RD_CSR_ZIMM:
                    operands.imm = static_cast<i32>(rng() % 32);
                    break;
                case OperandSyntax::RS1_RS2_LBL:
                    operands.imm = (static_cast<i32>(rng() % 8192) - 4096) & ~1;
                    break;
                case OperandSyntax::RD_LBL:
                    operands.imm = (static_cast<i32>(rng() % 2097152) - 1048576) & ~1;
                    break;
                case OperandSyntax::RD_UIMM:
                    operands.imm = static_cast<i32>(rng() & 0xfffff000u);
                    break;
                case OperandSyntax::NONE:
                case OperandSyntax::RD_RS1_RS2:
                case OperandSyntax::RD_CSR_RS1:
                    operands.imm = 0;
                    break;
            }

            const EncodeResult encoded = encode(id, operands);
            RV_CHECK(encoded.ok());
            if (!encoded.ok()) continue;

            const DecodedInstr decoded = decode(encoded.word);
            RV_CHECK_EQ(mnemonic_of(decoded.id), desc.mnemonic);

            const EncodeResult again = encode(decoded.id, operands_of(decoded));
            RV_CHECK_HEX(again.word, encoded.word);
        }
    }
}

RV_TEST(roundtrip, disassembly_is_never_empty_and_names_the_instruction) {
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const auto id = static_cast<InstrId>(i);
        const InstrDesc& desc = describe(id);
        const EncodeResult encoded = encode(id, Operands{});
        RV_CHECK(encoded.ok());

        const std::string text = disassemble_word(encoded.word, 0);
        RV_CHECK(!text.empty());
        // The mnemonic must be the first token of the printed form.
        RV_CHECK_EQ(text.compare(0, desc.mnemonic.size(), desc.mnemonic), 0);
    }
}
