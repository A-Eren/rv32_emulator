// Register naming. Both spellings are accepted on input and both are shown in
// the UI: `x10` is what the encoding contains, `a0` is what real assembly is
// written in, and a learner needs to see the correspondence rather than pick
// one and lose the other.
#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "types.hpp"

namespace rv::isa {

/// ABI name for each register, indexed by register number.
inline constexpr std::array<std::string_view, kNumRegs> kAbiNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

/// Numeric name (`x0`..`x31`), indexed by register number.
inline constexpr std::array<std::string_view, kNumRegs> kNumericNames = {
    "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10",
    "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21",
    "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31",
};

constexpr std::string_view abi_name(RegIdx reg) {
    return reg < kNumRegs ? kAbiNames[reg] : "?";
}

constexpr std::string_view numeric_name(RegIdx reg) {
    return reg < kNumRegs ? kNumericNames[reg] : "?";
}

/// Parse either spelling. `s0` and its alias `fp` both map to x8.
///
/// Lives here rather than in the lexer so that the assembler, the disassembler
/// and the debugger's `info reg` command all agree on what a register name is.
std::optional<RegIdx> parse_register(std::string_view name);

}  // namespace rv::isa
