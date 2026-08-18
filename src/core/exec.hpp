// Instruction execution.
//
// execute() is a pure state transition: it mutates the register file, the CSRs
// and memory, and reports where the pc should go next and whether anything
// trapped. Committing the pc and entering the trap handler is the hart's job,
// which keeps this function free of control-flow policy.
#pragma once

#include "core/bus.hpp"
#include "core/cpustate.hpp"
#include "core/trap.hpp"
#include "isa/decode.hpp"

namespace rv::core {

struct ExecResult {
    Addr next_pc = 0;

    bool trapped = false;
    TrapCause cause = TrapCause::None;
    u32 tval = 0;

    HaltReason halt = HaltReason::None;

    // What this instruction touched. Feeds the execution trace and the
    // register/memory diff highlighting, and is what the reverse-step delta
    // will be built from -- the executor already knows, so nothing has to be
    // recovered later by diffing the whole machine.
    RegIdx reg_written = kNoReg;
    Addr mem_written = kNoAddr;
    u8 mem_width = 0;
};

ExecResult execute(CpuState& cpu, Bus& bus, const isa::DecodedInstr& instr,
                   EbreakBehavior ebreak_behavior);

}  // namespace rv::core
