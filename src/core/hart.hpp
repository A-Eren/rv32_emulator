// The machine: one hart, its instruction memory, and its data bus.
#pragma once

#include "core/bus.hpp"
#include "core/cpustate.hpp"
#include "core/exec.hpp"
#include "core/memory.hpp"
#include "core/trap.hpp"
#include "isa/decode.hpp"

namespace rv::core {

inline constexpr u32 kDefaultImemSize = 16 * 1024;
inline constexpr u32 kDefaultDmemSize = 16 * 1024;

/// Everything that happened in one instruction. Rich enough that the debugger
/// never has to ask a second question about the step it just took.
struct StepOutcome {
    Addr pc_before = 0;
    Addr pc_after = 0;
    Word raw = 0;
    isa::DecodedInstr instr;

    bool trapped = false;
    TrapCause cause = TrapCause::None;
    u32 tval = 0;

    HaltReason halt = HaltReason::None;

    // What changed, and what was there before -- see ExecResult for why this
    // is cheap to collect and what it is for.
    RegIdx reg_written = kNoReg;
    u32 reg_old = 0;

    CsrAddr csr_written = 0;
    bool wrote_csr = false;
    u32 csr_old = 0;

    Addr mem_written = kNoAddr;
    u8 mem_width = 0;
    u32 mem_old = 0;

    /// Set when the step entered a trap handler or returned from one. Those
    /// touch several CSRs at once, which does not fit the single-CSR record;
    /// the history takes a whole CsrFile copy instead. Traps are rare enough
    /// that widening the common case to accommodate them would be the wrong
    /// trade.
    bool touched_many_csrs = false;

    u64 cycle_before = 0;
    u64 instret_before = 0;
    /// UART output length before the step, so undo can drop bytes that a
    /// buffered console has not really sent anywhere.
    std::size_t uart_length_before = 0;
    u32 leds_before = 0;
};

class Hart {
public:
    Hart(u32 imem_size = kDefaultImemSize, u32 dmem_size = kDefaultDmemSize);

    CpuState& cpu() { return cpu_; }
    const CpuState& cpu() const { return cpu_; }
    Memory& imem() { return imem_; }
    const Memory& imem() const { return imem_; }
    Bus& bus() { return bus_; }
    const Bus& bus() const { return bus_; }

    /// Clear architectural state and start at `entry`. Instruction memory is
    /// left alone -- resetting a core does not erase its ROM.
    void reset(Addr entry = 0);

    /// Fetch, decode, execute, and commit. Stepping a halted hart is a no-op
    /// that reports the existing halt reason.
    StepOutcome step();

    bool halted() const { return halt_reason_ != HaltReason::None; }
    HaltReason halt_reason() const { return halt_reason_; }
    void clear_halt() { halt_reason_ = HaltReason::None; }

    EbreakBehavior ebreak_behavior() const { return ebreak_behavior_; }
    void set_ebreak_behavior(EbreakBehavior behavior) { ebreak_behavior_ = behavior; }

private:
    /// Enter the machine trap handler. If mtvec is still zero there is nowhere
    /// to go, so the hart halts instead of jumping to address zero and
    /// executing whatever happens to be there -- silently re-running the
    /// program from the top is the single most confusing failure mode for a
    /// beginner.
    void take_trap(TrapCause cause, u32 tval, Addr epc, StepOutcome& out);

    CpuState cpu_;
    Memory imem_;
    Bus bus_;
    EbreakBehavior ebreak_behavior_ = EbreakBehavior::HaltToDebugger;
    HaltReason halt_reason_ = HaltReason::None;
};

}  // namespace rv::core
