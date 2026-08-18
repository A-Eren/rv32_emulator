// Bit-field extraction and insertion.
//
// The round-trip tests here are the reason a scrambled B-type or J-type
// immediate cannot survive: any bit placed in the wrong position shows up
// immediately as an asymmetry between the encoder and the decoder.
#include <random>

#include "isa/fields.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::isa;

RV_TEST(fields, sign_extend_boundaries) {
    RV_CHECK_EQ(sign_extend(0x7ff, 12), 2047);
    RV_CHECK_EQ(sign_extend(0x800, 12), -2048);
    RV_CHECK_EQ(sign_extend(0xfff, 12), -1);
    RV_CHECK_EQ(sign_extend(0x000, 12), 0);
    RV_CHECK_EQ(sign_extend(0x7f, 8), 127);
    RV_CHECK_EQ(sign_extend(0x80, 8), -128);
    // Width 32 must be a no-op rather than an undefined shift.
    RV_CHECK_EQ(sign_extend(0xffff'ffffu, 32), -1);
    RV_CHECK_EQ(sign_extend(0x8000'0000u, 32), -2147483647 - 1);
}

RV_TEST(fields, shifts_mask_the_amount) {
    // RISC-V uses only the low 5 bits of a shift amount. In C++ a shift of 32
    // or more is undefined behaviour, so this must be masked, not merely
    // clamped after the fact.
    RV_CHECK_HEX(shift_left(1u, 32), 1u);
    RV_CHECK_HEX(shift_left(1u, 33), 2u);
    RV_CHECK_HEX(shift_right_logical(0x8000'0000u, 32), 0x8000'0000u);
    RV_CHECK_HEX(shift_right_arith(0x8000'0000u, 1), 0xc000'0000u);
    RV_CHECK_HEX(shift_right_arith(0x8000'0000u, 31), 0xffff'ffffu);
    RV_CHECK_HEX(shift_right_arith(0x4000'0000u, 30), 0x0000'0001u);
    // A zero shift must not paste sign bits in.
    RV_CHECK_HEX(shift_right_arith(0x8000'0000u, 0), 0x8000'0000u);
}

RV_TEST(fields, bit_extraction) {
    RV_CHECK_HEX(bits(0xdead'beefu, 31, 28), 0xdu);
    RV_CHECK_HEX(bits(0xdead'beefu, 3, 0), 0xfu);
    RV_CHECK_HEX(bits(0xdead'beefu, 31, 0), 0xdead'beefu);  // full width, no UB
    RV_CHECK_HEX(bit(0x8000'0000u, 31), 1u);
    RV_CHECK_HEX(bit(0x8000'0000u, 30), 0u);
}

RV_TEST(fields, immediate_round_trip) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<i32> dist(-(1 << 20), (1 << 20) - 1);

    for (int iteration = 0; iteration < 20000; ++iteration) {
        const i32 raw = dist(rng);

        // I-type and S-type: 12-bit signed.
        const i32 imm12 = sign_extend(static_cast<u32>(raw), 12);
        RV_CHECK_EQ(imm_i(put_imm_i(imm12)), imm12);
        RV_CHECK_EQ(imm_s(put_imm_s(imm12)), imm12);

        // B-type: 13-bit signed, always even.
        const i32 imm13 = sign_extend(static_cast<u32>(raw) & ~1u, 13);
        RV_CHECK_EQ(imm_b(put_imm_b(imm13)), imm13);

        // J-type: 21-bit signed, always even.
        const i32 imm21 = sign_extend(static_cast<u32>(raw) & ~1u, 21);
        RV_CHECK_EQ(imm_j(put_imm_j(imm21)), imm21);

        // U-type: the top 20 bits, low 12 always zero.
        const i32 imm_upper = static_cast<i32>(static_cast<u32>(raw) << 12);
        RV_CHECK_EQ(imm_u(put_imm_u(imm_upper)), imm_upper);
    }
}

RV_TEST(fields, immediates_do_not_disturb_other_fields) {
    // Each inserter must contribute only the bits its own format owns, so a
    // caller can OR the pieces of an instruction together in any order.
    // Note that S-type and B-type immediates legitimately occupy [11:7] -- the
    // slot an R-type or I-type would use for rd -- which is exactly why those
    // formats have no rd operand.
    constexpr Word kIFieldMask = 0xfff0'0000u;  // [31:20]
    constexpr Word kSFieldMask = 0xfe00'0f80u;  // [31:25] and [11:7]
    constexpr Word kUFieldMask = 0xffff'f000u;  // [31:12]

    RV_CHECK_HEX(put_imm_i(-1) & ~kIFieldMask, 0u);
    RV_CHECK_HEX(put_imm_s(-1) & ~kSFieldMask, 0u);
    RV_CHECK_HEX(put_imm_b(-2) & ~kSFieldMask, 0u);
    RV_CHECK_HEX(put_imm_j(-2) & ~kUFieldMask, 0u);
    RV_CHECK_HEX(put_imm_u(-4096) & ~kUFieldMask, 0u);
    RV_CHECK_HEX(put_rd(31) & ~0x0000'0f80u, 0u);
    RV_CHECK_HEX(put_rs1(31) & ~0x000f'8000u, 0u);
    RV_CHECK_HEX(put_rs2(31) & ~0x01f0'0000u, 0u);
}

RV_TEST(fields, register_field_positions) {
    // rd=1, rs1=2, rs2=3 in an otherwise empty word.
    const Word word = put_rd(1) | put_rs1(2) | put_rs2(3);
    RV_CHECK_EQ(rd_of(word), 1);
    RV_CHECK_EQ(rs1_of(word), 2);
    RV_CHECK_EQ(rs2_of(word), 3);
}
