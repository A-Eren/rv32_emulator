#include "decode.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "fields.hpp"

namespace rv::isa {
namespace {

/// All RV32 instructions are 32 bits wide, so bits [1:0] are always 0b11 and
/// carry no information. Bucketing on opcode[6:2] gives 32 buckets holding at
/// most a handful of rows each.
constexpr std::size_t kBucketCount = 32;

using Buckets = std::array<std::vector<InstrId>, kBucketCount>;

Buckets build_buckets() {
    Buckets buckets;
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const std::size_t bucket = bits(kInstrTable[i].match, 6, 2);
        buckets[bucket].push_back(static_cast<InstrId>(i));
    }
    // Try the most specific rows first. `ecall` (mask 0xffffffff) and `slli`
    // (mask 0xfe00707f) must be tested before any row whose mask is a subset of
    // theirs, otherwise a broad row would swallow a narrow one. Nothing in the
    // current table actually overlaps, but sorting makes that a property of the
    // decoder rather than a property of the order rows happen to be listed in.
    for (auto& bucket : buckets) {
        std::sort(bucket.begin(), bucket.end(), [](InstrId a, InstrId b) {
            const auto popcount = [](Word m) {
                int n = 0;
                for (; m != 0; m &= m - 1) ++n;
                return n;
            };
            return popcount(describe(a).mask) > popcount(describe(b).mask);
        });
    }
    return buckets;
}

const Buckets& buckets() {
    static const Buckets table = build_buckets();
    return table;
}

}  // namespace

DecodedInstr decode(Word word) {
    DecodedInstr out;
    out.raw = word;

    for (const InstrId candidate : buckets()[bits(word, 6, 2)]) {
        const InstrDesc& desc = describe(candidate);
        if ((word & desc.mask) != desc.match) continue;
        out.id = candidate;

        switch (desc.syntax) {
            case OperandSyntax::NONE:
                break;
            case OperandSyntax::RD_RS1_RS2:
                out.rd = rd_of(word);
                out.rs1 = rs1_of(word);
                out.rs2 = rs2_of(word);
                break;
            case OperandSyntax::RD_RS1_IMM:
            case OperandSyntax::RD_OFS_RS1:
                out.rd = rd_of(word);
                out.rs1 = rs1_of(word);
                out.imm = imm_i(word);
                break;
            case OperandSyntax::RD_RS1_SHAMT:
                out.rd = rd_of(word);
                out.rs1 = rs1_of(word);
                out.imm = static_cast<i32>(shamt_of(word));
                break;
            case OperandSyntax::RS2_OFS_RS1:
                out.rs1 = rs1_of(word);
                out.rs2 = rs2_of(word);
                out.imm = imm_s(word);
                break;
            case OperandSyntax::RS1_RS2_LBL:
                out.rs1 = rs1_of(word);
                out.rs2 = rs2_of(word);
                out.imm = imm_b(word);
                break;
            case OperandSyntax::RD_UIMM:
                out.rd = rd_of(word);
                out.imm = imm_u(word);
                break;
            case OperandSyntax::RD_LBL:
                out.rd = rd_of(word);
                out.imm = imm_j(word);
                break;
            case OperandSyntax::RD_CSR_RS1:
                out.rd = rd_of(word);
                out.rs1 = rs1_of(word);
                out.csr = csr_of(word);
                break;
            case OperandSyntax::RD_CSR_ZIMM:
                out.rd = rd_of(word);
                out.rs1 = rs1_of(word);
                out.csr = csr_of(word);
                out.imm = static_cast<i32>(zimm_of(word));  // zero-extended, never signed
                break;
        }
        return out;
    }

    return out;  // id stays kInvalidInstr
}

}  // namespace rv::isa
