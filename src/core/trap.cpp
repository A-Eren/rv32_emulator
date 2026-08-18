#include "core/trap.hpp"

namespace rv::core {

const char* trap_cause_name(TrapCause cause) {
    switch (cause) {
        case TrapCause::None: return "none";
        case TrapCause::InstructionAddressMisaligned: return "instruction address misaligned";
        case TrapCause::InstructionAccessFault: return "instruction access fault";
        case TrapCause::IllegalInstruction: return "illegal instruction";
        case TrapCause::Breakpoint: return "breakpoint";
        case TrapCause::LoadAddressMisaligned: return "load address misaligned";
        case TrapCause::LoadAccessFault: return "load access fault";
        case TrapCause::StoreAddressMisaligned: return "store address misaligned";
        case TrapCause::StoreAccessFault: return "store access fault";
        case TrapCause::EnvironmentCallFromMMode: return "environment call from M-mode";
    }
    return "unknown";
}

const char* halt_reason_name(HaltReason reason) {
    switch (reason) {
        case HaltReason::None: return "running";
        case HaltReason::Ebreak: return "ebreak";
        case HaltReason::Ecall: return "ecall";
        case HaltReason::Watchdog: return "watchdog (instruction budget exhausted)";
        case HaltReason::UnhandledTrap: return "unhandled trap (mtvec is zero)";
    }
    return "unknown";
}

}  // namespace rv::core
