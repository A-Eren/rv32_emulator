// CSR access rules and the trap mechanism.
#include "machine_fixture.hpp"

using namespace fixture;
using rv::isa::InstrId;
using rv::core::HaltReason;
using rv::core::TrapCause;

namespace {

constexpr rv::CsrAddr kMstatus = 0x300;
constexpr rv::CsrAddr kMtvec = 0x305;
constexpr rv::CsrAddr kMscratch = 0x340;
constexpr rv::CsrAddr kMepc = 0x341;
constexpr rv::CsrAddr kMcause = 0x342;
constexpr rv::CsrAddr kMtval = 0x343;
constexpr rv::CsrAddr kCycle = 0xc00;

rv::u32 read_csr(Machine& m, rv::CsrAddr addr) { return m.hart().cpu().csr.read(addr); }
void poke_csr(Machine& m, rv::CsrAddr addr, rv::u32 value) {
    m.hart().cpu().csr.raw_write(addr, value);
}

}  // namespace

RV_TEST(csr_trap, csrrw_swaps_the_value) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x55),
        CSR(InstrId::CSRRW, 2, kMscratch, 1),  // x2 = old mscratch, mscratch = x1
        CSR(InstrId::CSRRW, 3, kMscratch, 0),  // x3 = 0x55, mscratch = 0
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0u);
    RV_CHECK_HEX(m.reg(3), 0x55u);
    RV_CHECK_HEX(read_csr(m, kMscratch), 0u);
}

RV_TEST(csr_trap, csrrs_with_x0_source_does_not_write) {
    // `csrr rd, csr` is spelled `csrrs rd, csr, x0`, and it must be a pure
    // read -- the condition is the register *number*, not its value.
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, kMscratch, 0),  // pure read
        EBREAK(),
    });
    poke_csr(m, kMscratch, 0xabcd'0000u);
    m.run();
    RV_CHECK_HEX(m.reg(1), 0xabcd'0000u);
    RV_CHECK_HEX(read_csr(m, kMscratch), 0xabcd'0000u);
}

RV_TEST(csr_trap, csrrs_and_csrrc_set_and_clear_bits) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x0f),
        CSR(InstrId::CSRRS, 0, kMscratch, 1),  // mscratch |= 0x0f
        I(InstrId::ADDI, 2, 0, 0x03),
        CSR(InstrId::CSRRC, 0, kMscratch, 2),  // mscratch &= ~0x03
        CSR(InstrId::CSRRS, 3, kMscratch, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0x0cu);
}

RV_TEST(csr_trap, immediate_forms_zero_extend) {
    // csrrwi takes a 5-bit *zero*-extended immediate. Sign-extending it would
    // make csrrwi with 0x1f write 0xffffffff.
    Machine m;
    m.load({
        CSRI(InstrId::CSRRWI, 0, kMscratch, 31),
        CSR(InstrId::CSRRS, 1, kMscratch, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 31u);
}

RV_TEST(csr_trap, csrrsi_with_zero_immediate_does_not_write) {
    Machine m;
    m.load({
        CSRI(InstrId::CSRRSI, 1, kMscratch, 0),  // pure read
        EBREAK(),
    });
    poke_csr(m, kMscratch, 0x1234u);
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x1234u);
    RV_CHECK_HEX(read_csr(m, kMscratch), 0x1234u);
}

RV_TEST(csr_trap, write_mask_protects_reserved_bits) {
    // mstatus has only MIE, MPIE and MPP writable; everything else is held at
    // its reset value.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -1),
        CSR(InstrId::CSRRW, 0, kMstatus, 1),  // try to set every bit
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(read_csr(m, kMstatus), 0x1888u);
}

RV_TEST(csr_trap, writing_a_read_only_csr_is_illegal) {
    // `cycle` has address bits [11:10] == 0b11, marking it read-only.
    Machine m;
    m.load({
        CSR(InstrId::CSRRW, 0, kCycle, 0),
        EBREAK(),
    });
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::IllegalInstruction));
}

RV_TEST(csr_trap, reading_a_read_only_csr_is_fine) {
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, kCycle, 0),
        CSR(InstrId::CSRRS, 2, kCycle, 0),
        EBREAK(),
    });
    m.run();
    // The counter is refreshed before each instruction, so the second read sees
    // a later cycle than the first.
    RV_CHECK(m.reg(2) > m.reg(1));
}

RV_TEST(csr_trap, accessing_an_unimplemented_csr_is_illegal) {
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, 0x7c0, 0),  // not in the table
        EBREAK(),
    });
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::IllegalInstruction));
}

RV_TEST(csr_trap, ecall_enters_the_handler_and_records_state) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),  // 0x00
        ECALL(),                    // 0x04
        I(InstrId::ADDI, 2, 0, 2),  // 0x08 -- skipped, we trap first
        I(InstrId::ADDI, 3, 0, 3),  // 0x0c -- the handler
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x0c);
    m.run();

    RV_CHECK_HEX(read_csr(m, kMepc), 0x04u);  // the faulting instruction, not pc+4
    RV_CHECK_HEX(read_csr(m, kMcause),
                 static_cast<rv::u32>(TrapCause::EnvironmentCallFromMMode));
    RV_CHECK_HEX(m.reg(2), 0u);  // handler ran instead
    RV_CHECK_HEX(m.reg(3), 3u);
}

RV_TEST(csr_trap, trap_entry_saves_the_interrupt_enable_bit) {
    Machine m;
    m.load({ECALL(), EBREAK()});
    poke_csr(m, kMtvec, 0x04);
    poke_csr(m, kMstatus, 0x8);  // MIE set
    m.step();

    const rv::u32 mstatus = read_csr(m, kMstatus);
    RV_CHECK_HEX(rv::bit(mstatus, 7), 1u);   // MPIE <- old MIE
    RV_CHECK_HEX(rv::bit(mstatus, 3), 0u);   // MIE cleared
    RV_CHECK_HEX(rv::bits(mstatus, 12, 11), 3u);  // MPP = machine mode
}

RV_TEST(csr_trap, a_complete_trap_handler_round_trip) {
    // The full shape of a real handler: save mepc, step it past the faulting
    // instruction, and mret back. Returning without advancing mepc would
    // re-execute the ecall forever, which is the mistake this test pins down.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),             // 0x00
        ECALL(),                               // 0x04 -> handler at 0x10
        I(InstrId::ADDI, 2, 0, 2),             // 0x08 <- mret lands here
        EBREAK(),                              // 0x0c
        CSR(InstrId::CSRRS, 5, kMepc, 0),      // 0x10 handler: x5 = mepc
        I(InstrId::ADDI, 5, 5, 4),             // 0x14 skip the ecall
        CSR(InstrId::CSRRW, 0, kMepc, 5),      // 0x18 mepc = x5
        I(InstrId::ADDI, 3, 0, 3),             // 0x1c mark that we got here
        enc(InstrId::MRET, {}),                // 0x20 return
    });
    poke_csr(m, kMtvec, 0x10);
    m.run();

    RV_CHECK_HEX(m.reg(1), 1u);  // before the trap
    RV_CHECK_HEX(m.reg(3), 3u);  // inside the handler
    RV_CHECK_HEX(m.reg(2), 2u);  // after returning
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()), static_cast<int>(HaltReason::Ebreak));
}

RV_TEST(csr_trap, mret_restores_the_interrupt_enable_bit) {
    Machine m;
    m.load({
        enc(InstrId::MRET, {}),
        EBREAK(),
    });
    poke_csr(m, kMepc, 0x04);
    poke_csr(m, kMstatus, 0x80);  // MPIE set, MIE clear
    m.step();

    const rv::u32 mstatus = read_csr(m, kMstatus);
    RV_CHECK_HEX(rv::bit(mstatus, 3), 1u);  // MIE <- MPIE
    RV_CHECK_HEX(rv::bit(mstatus, 7), 1u);  // MPIE set to 1
}

RV_TEST(csr_trap, a_trap_with_no_handler_halts_instead_of_jumping_to_zero) {
    // mtvec is zero by default. Jumping there would silently restart the
    // program from the top, which is the single most confusing failure mode a
    // beginner can hit. Halt and say so instead.
    Machine m;
    m.load({ECALL(), EBREAK()});
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.halt), static_cast<int>(HaltReason::UnhandledTrap));
    RV_CHECK(m.hart().halted());
    RV_CHECK_HEX(m.pc(), 0u);  // stays on the faulting instruction for the UI
}

RV_TEST(csr_trap, illegal_instruction_reports_the_offending_word_in_mtval) {
    Machine m;
    m.load({0x2000'3033u, EBREAK()});  // reserved funct7 for opcode 0x33
    poke_csr(m, kMtvec, 0x04);
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::IllegalInstruction));
    RV_CHECK_HEX(read_csr(m, kMtval), 0x2000'3033u);
}

RV_TEST(csr_trap, ebreak_can_trap_instead_of_halting) {
    Machine m;
    m.load({EBREAK(), I(InstrId::ADDI, 1, 0, 7)});
    m.hart().set_ebreak_behavior(rv::core::EbreakBehavior::TrapToMtvec);
    poke_csr(m, kMtvec, 0x04);
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::Breakpoint));
    RV_CHECK(!m.hart().halted());
}

RV_TEST(csr_trap, running_off_the_end_of_a_program_stops) {
    // Instruction memory past the program is zero, and an all-zero word is not
    // a valid encoding. Forgetting the final ebreak therefore stops with a
    // named illegal-instruction trap rather than executing garbage.
    Machine m;
    m.load({I(InstrId::ADDI, 1, 0, 1)});  // no ebreak
    m.run(20);
    RV_CHECK(m.hart().halted());
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()),
                static_cast<int>(HaltReason::UnhandledTrap));
    RV_CHECK_HEX(m.pc(), 0x04u);  // stopped at the zero word, not somewhere wild
}
