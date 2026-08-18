// Decoding, and the encode/decode/disassemble round trip.
#include <random>

#include "isa/decode.hpp"
#include "isa/disasm.hpp"
#include "isa/fields.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::isa;

RV_TEST(decode, known_encodings) {
    // Hand-checked against the RISC-V unprivileged spec.
    {
        const DecodedInstr d = decode(0x00500093);  // addi x1, x0, 5
        RV_CHECK(d.valid());
        RV_CHECK_EQ(mnemonic_of(d.id), "addi");
        RV_CHECK_EQ(d.rd, 1);
        RV_CHECK_EQ(d.rs1, 0);
        RV_CHECK_EQ(d.imm, 5);
    }
    {
        const DecodedInstr d = decode(0x002081b3);  // add x3, x1, x2
        RV_CHECK_EQ(mnemonic_of(d.id), "add");
        RV_CHECK_EQ(d.rd, 3);
        RV_CHECK_EQ(d.rs1, 1);
        RV_CHECK_EQ(d.rs2, 2);
    }
    {
        const DecodedInstr d = decode(0x40208233);  // sub x4, x1, x2
        RV_CHECK_EQ(mnemonic_of(d.id), "sub");
    }
    {
        const DecodedInstr d = decode(0x0000a303);  // lw x6, 0(x1)
        RV_CHECK_EQ(mnemonic_of(d.id), "lw");
        RV_CHECK_EQ(d.rd, 6);
        RV_CHECK_EQ(d.rs1, 1);
        RV_CHECK_EQ(d.imm, 0);
    }
    {
        const DecodedInstr d = decode(0x00112023);  // sw x1, 0(x2)
        RV_CHECK_EQ(mnemonic_of(d.id), "sw");
        RV_CHECK_EQ(d.rs1, 2);
        RV_CHECK_EQ(d.rs2, 1);
        RV_CHECK_EQ(d.imm, 0);
    }
    {
        const DecodedInstr d = decode(0x00100073);  // ebreak
        RV_CHECK_EQ(mnemonic_of(d.id), "ebreak");
    }
    {
        const DecodedInstr d = decode(0x00000073);  // ecall
        RV_CHECK_EQ(mnemonic_of(d.id), "ecall");
    }
    {
        const DecodedInstr d = decode(0x02000033);  // mul x0, x0, x0
        RV_CHECK_EQ(mnemonic_of(d.id), "mul");
    }
}

RV_TEST(decode, shift_immediates_are_distinguished_by_funct7) {
    // srli and srai share funct3; only bit 30 tells them apart. Getting the
    // mask wrong here makes arithmetic right shifts silently logical.
    const DecodedInstr srli = decode(0x0020d093);  // srli x1, x1, 2
    const DecodedInstr srai = decode(0x4020d093);  // srai x1, x1, 2
    RV_CHECK_EQ(mnemonic_of(srli.id), "srli");
    RV_CHECK_EQ(mnemonic_of(srai.id), "srai");
    RV_CHECK_EQ(srli.imm, 2);
    RV_CHECK_EQ(srai.imm, 2);
}

RV_TEST(decode, exact_match_rows_win_over_broad_ones) {
    // ecall/ebreak/mret/wfi have a full-word mask and share opcode 0x73 with
    // the CSR instructions. Each must decode to itself.
    RV_CHECK_EQ(mnemonic_of(decode(0x30200073).id), "mret");
    RV_CHECK_EQ(mnemonic_of(decode(0x10500073).id), "wfi");
    RV_CHECK_EQ(mnemonic_of(decode(0x30029073).id), "csrrw");  // csrrw x0, mstatus, x5
}

RV_TEST(decode, invalid_words_are_rejected) {
    RV_CHECK(!decode(0x00000000).valid());  // opcode 0x00 is not assigned
    RV_CHECK(!decode(0xffffffff).valid());  // opcode 0x7f is not assigned
    // A real opcode and funct3, but a funct7 that no row claims: 0x33/f3=3
    // exists as sltu only with funct7 0x00.
    RV_CHECK(!decode(0x20003033).valid());
    // Zicsr shares opcode 0x73, but funct3 = 4 is unassigned.
    RV_CHECK(!decode(0x00004073).valid());
}

RV_TEST(decode, disassembly_round_trip_over_random_words) {
    // For any word that decodes, re-encoding what the disassembler printed must
    // reproduce the original bits. We approximate "re-encode" by checking that
    // decoding is stable and that every field survives a print/parse of the
    // numeric operands -- the full text round trip lands with the assembler.
    std::mt19937 rng(987654321);
    int decoded_count = 0;
    for (int i = 0; i < 200000; ++i) {
        const Word word = rng();
        const DecodedInstr d = decode(word);
        if (!d.valid()) continue;
        ++decoded_count;

        // Decoding must be deterministic and idempotent.
        const DecodedInstr again = decode(word);
        RV_CHECK_EQ(again.id, d.id);
        RV_CHECK_EQ(again.imm, d.imm);

        // The disassembler must never produce an empty line or crash.
        const std::string text = disassemble(d, 0x1000);
        RV_CHECK(!text.empty());

        // Every register index must be in range -- a field extracted from the
        // wrong bits would eventually produce something >= 32.
        RV_CHECK(d.rd < 32);
        RV_CHECK(d.rs1 < 32);
        RV_CHECK(d.rs2 < 32);
    }
    // Sanity: a meaningful fraction of random words should be valid, otherwise
    // this test is silently passing without exercising anything.
    RV_CHECK(decoded_count > 1000);
}

RV_TEST(decode, every_table_row_decodes_to_itself) {
    // Build each instruction from its own match pattern and check it comes back
    // as the same id. This catches two rows whose match/mask overlap, which
    // would otherwise show up as a rare and baffling misexecution.
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const InstrDesc& desc = kInstrTable[i];
        const DecodedInstr d = decode(desc.match);
        RV_CHECK_EQ(mnemonic_of(d.id), desc.mnemonic);
    }
}
