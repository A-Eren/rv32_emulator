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

    RegIdx reg_written = kNoReg;
    Addr mem_written = kNoAddr;
    u8 mem_width = 0;
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
