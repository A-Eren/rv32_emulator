// Branches, jumps and the link register.
#include "machine_fixture.hpp"

using namespace fixture;
using rv::isa::InstrId;

RV_TEST(exec_branch, beq_taken_and_not_taken) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 5),   // 0x00
        I(InstrId::ADDI, 2, 0, 5),   // 0x04
        B(InstrId::BEQ, 1, 2, 8),    // 0x08: equal -> skip the next instruction
        I(InstrId::ADDI, 3, 0, 99),  // 0x0c: must not execute
        I(InstrId::ADDI, 4, 0, 7),   // 0x10
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0u);
    RV_CHECK_HEX(m.reg(4), 7u);
}

RV_TEST(exec_branch, signed_and_unsigned_comparisons_differ) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -1),  // 0xffffffff
        I(InstrId::ADDI, 2, 0, 1),
        B(InstrId::BLT, 1, 2, 8),    // signed: -1 < 1, taken
        I(InstrId::ADDI, 3, 0, 1),   // skipped
        B(InstrId::BLTU, 1, 2, 8),   // unsigned: huge < 1 is false, not taken
        I(InstrId::ADDI, 4, 0, 1),   // executed
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0u);
    RV_CHECK_HEX(m.reg(4), 1u);
}

RV_TEST(exec_branch, backward_branch_forms_a_loop) {
    // Count down from 3, proving negative branch offsets work. A wrong sign bit
    // in the B-type immediate turns this into an infinite loop or a wild jump.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 3),    // 0x00: counter
        I(InstrId::ADDI, 2, 0, 0),    // 0x04: accumulator
        R(InstrId::ADD, 2, 2, 1),     // 0x08: acc += counter
        I(InstrId::ADDI, 1, 1, -1),   // 0x0c: counter -= 1
        B(InstrId::BNE, 1, 0, -8),    // 0x10: loop while counter != 0
        EBREAK(),                     // 0x14
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0u);
    RV_CHECK_HEX(m.reg(2), 6u);  // 3 + 2 + 1
}

RV_TEST(exec_branch, jal_links_and_jumps) {
    Machine m;
    m.load({
        J(InstrId::JAL, 1, 8),       // 0x00: jump to 0x08, ra = 0x04
        I(InstrId::ADDI, 2, 0, 99),  // 0x04: skipped
        EBREAK(),                    // 0x08
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x04u);
    RV_CHECK_HEX(m.reg(2), 0u);
}

RV_TEST(exec_branch, jalr_clears_the_low_bit_of_the_target) {
    // The spec requires the computed address to have its least significant bit
    // cleared. Without that, an odd base makes the target misaligned and the
    // jump faults instead of landing where the programmer meant.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 9),   // 0x00: x1 = 9 (odd)
        I(InstrId::JALR, 0, 1, -1),  // 0x04: (9 + -1) & ~1 = 8
        I(InstrId::ADDI, 2, 0, 99),  // 0x08: this is where we land
        EBREAK(),                    // 0x0c
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 99u);
}

RV_TEST(exec_branch, jalr_computes_the_target_before_linking) {
    // `jalr ra, ra, 0` must jump to the *old* ra. Writing the link register
    // first would make it jump to the instruction after itself.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 12),  // 0x00: x1 = 0x0c
        I(InstrId::JALR, 1, 1, 0),   // 0x04: jump to 0x0c, then x1 = 0x08
        I(InstrId::ADDI, 2, 0, 1),   // 0x08: skipped
        I(InstrId::ADDI, 3, 0, 2),   // 0x0c: landing site
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 2u);
    RV_CHECK_HEX(m.reg(2), 0u);
    RV_CHECK_HEX(m.reg(1), 0x08u);
}

RV_TEST(exec_branch, misaligned_jump_target_traps) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 6),   // not a multiple of 4
        I(InstrId::JALR, 0, 1, 0),
        EBREAK(),
    });
    m.step();
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause),
                static_cast<int>(rv::core::TrapCause::InstructionAddressMisaligned));
}

RV_TEST(exec_branch, branch_with_rd_x0_does_not_link) {
    // `jal x0, offset` is a plain jump; the link must be discarded.
    Machine m;
    m.load({
        J(InstrId::JAL, 0, 8),
        I(InstrId::ADDI, 1, 0, 99),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(0), 0u);
    RV_CHECK_HEX(m.reg(1), 0u);
}
