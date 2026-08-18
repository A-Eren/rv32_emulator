// Trap causes and halt reasons.
#pragma once

#include "isa/types.hpp"

namespace rv::core {

/// Exception codes as written to mcause. Values are fixed by the privileged
/// spec; do not renumber.
enum class TrapCause : u32 {
    None = 0xffff'ffff,
    InstructionAddressMisaligned = 0,
    InstructionAccessFault = 1,
    IllegalInstruction = 2,
    Breakpoint = 3,
    LoadAddressMisaligned = 4,
    LoadAccessFault = 5,
    StoreAddressMisaligned = 6,
    StoreAccessFault = 7,
    EnvironmentCallFromMMode = 11,
};

/// Why the machine stopped. Distinct from a trap: a trap is handled by the
/// program (control transfers to mtvec), a halt returns control to the user.
enum class HaltReason : u8 {
    None,
    Ebreak,        // ebreak with EbreakBehavior::HaltToDebugger -- normal program end
    Ecall,         // ecall with no handler installed
    Watchdog,      // instruction budget exhausted; almost always an infinite loop
    UnhandledTrap, // a trap fired but mtvec is 0, so there is nowhere to go
};

/// What `ebreak` does. The self-checking .s test programs and the natural
/// "end of program" idiom both need HaltToDebugger, but the trap path must
/// exist and be testable for the Zicsr lessons.
enum class EbreakBehavior : u8 { HaltToDebugger, TrapToMtvec };

const char* trap_cause_name(TrapCause cause);
const char* halt_reason_name(HaltReason reason);

}  // namespace rv::core
