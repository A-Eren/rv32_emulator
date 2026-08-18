#include "core/hart.hpp"

#include "core/csrfile.hpp"

namespace rv::core {
namespace {

constexpr CsrAddr kMstatus = 0x300;
constexpr CsrAddr kMtvec = 0x305;
constexpr CsrAddr kMepc = 0x341;
constexpr CsrAddr kMcause = 0x342;
constexpr CsrAddr kMtval = 0x343;

}  // namespace

Hart::Hart(u32 imem_size, u32 dmem_size) : imem_(0, imem_size), bus_(dmem_size) {}

void Hart::reset(Addr entry) {
    cpu_.reset(entry);
    bus_.reset();
    halt_reason_ = HaltReason::None;
}

void Hart::take_trap(TrapCause cause, u32 tval, Addr epc, StepOutcome& out) {
    out.trapped = true;
    out.cause = cause;
    out.tval = tval;

    const u32 mtvec = cpu_.csr.read(kMtvec);
    if (mtvec == 0) {
        halt_reason_ = HaltReason::UnhandledTrap;
        out.halt = halt_reason_;
        out.pc_after = cpu_.pc;  // stay put, so the UI shows the faulting instruction
        return;
    }

    cpu_.csr.raw_write(kMepc, epc);
    cpu_.csr.raw_write(kMcause, static_cast<u32>(cause));
    cpu_.csr.raw_write(kMtval, tval);

    // Save the interrupt-enable bit into MPIE, disable interrupts, and record
    // machine mode as the previous privilege level.
    const u32 mstatus = cpu_.csr.read(kMstatus);
    u32 next = mstatus & ~0x1888u;
    next |= bit(mstatus, 3) << 7;  // MPIE <- MIE
    next |= 3u << 11;              // MPP  <- machine mode
    cpu_.csr.raw_write(kMstatus, next);

    cpu_.pc = mtvec & ~3u;  // direct mode
    out.pc_after = cpu_.pc;
}

StepOutcome Hart::step() {
    StepOutcome out;
    out.pc_before = cpu_.pc;
    out.pc_after = cpu_.pc;

    if (halted()) {
        out.halt = halt_reason_;
        return out;
    }

    // Make the counter CSRs observable at their current values before the
    // instruction runs, so `csrr t0, cycle` sees the cycle it is executing in.
    cpu_.csr.set_counters(cpu_.cycle, cpu_.instret);

    // ---- fetch (from IMEM, never from the data bus) ----
    if ((cpu_.pc & 3u) != 0) {
        take_trap(TrapCause::InstructionAddressMisaligned, cpu_.pc, cpu_.pc, out);
        return out;
    }
    if (!imem_.contains(cpu_.pc) || !imem_.contains(cpu_.pc + 3)) {
        take_trap(TrapCause::InstructionAccessFault, cpu_.pc, cpu_.pc, out);
        return out;
    }
    out.raw = imem_.read_word_raw(cpu_.pc);

    // ---- decode ----
    out.instr = isa::decode(out.raw);
    if (!out.instr.valid()) {
        take_trap(TrapCause::IllegalInstruction, out.raw, cpu_.pc, out);
        ++cpu_.cycle;
        return out;
    }

    // ---- execute ----
    const ExecResult result = execute(cpu_, bus_, out.instr, ebreak_behavior_);
    out.reg_written = result.reg_written;
    out.mem_written = result.mem_written;
    out.mem_width = result.mem_width;

    ++cpu_.cycle;
    bus_.cycles().set_cycles(cpu_.cycle);

    if (result.trapped) {
        take_trap(result.cause, result.tval, cpu_.pc, out);
        return out;
    }

    if (result.halt != HaltReason::None) {
        halt_reason_ = result.halt;
        out.halt = result.halt;
        // Retire the ebreak and leave the pc past it, so that resuming after a
        // breakpoint does not re-trigger the same halt.
        ++cpu_.instret;
        cpu_.pc = result.next_pc;
        out.pc_after = cpu_.pc;
        return out;
    }

    ++cpu_.instret;
    cpu_.pc = result.next_pc;
    out.pc_after = cpu_.pc;
    return out;
}

}  // namespace rv::core
