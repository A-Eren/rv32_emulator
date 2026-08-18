# What is implemented

**RV32IM_Zicsr**, machine mode only. The `misa` register reads `0x40001100`.

The authoritative list is `src/isa/instr_table.def` — this document describes
it, but that file *is* it: the decoder, the disassembler, the encoder and the
assembler are all generated from those rows.

## Instructions

**RV32I (37)** — `lui auipc jal jalr`, `beq bne blt bge bltu bgeu`,
`lb lh lw lbu lhu`, `sb sh sw`,
`addi slti sltiu xori ori andi slli srli srai`,
`add sub sll slt sltu xor srl sra or and`.

**M (8)** — `mul mulh mulhsu mulhu div divu rem remu`.

**Zicsr (6)** — `csrrw csrrs csrrc csrrwi csrrsi csrrci`.

**System (5)** — `fence ecall ebreak mret wfi`. `fence` is architecturally a
no-op on a single in-order hart, and `wfi` retires rather than hanging, since
there are no interrupts to wait for.

## Pseudo-instructions

`nop mv not neg seqz snez sltz sgtz`,
`beqz bnez blez bgez bltz bgtz bgt ble bgtu bleu`,
`j jal jr jalr ret call tail`,
`li la`,
`csrr csrw csrs csrc csrwi csrsi csrci`.

Sizes are fixed at parse time — there is no branch relaxation. `la` and `call`
are therefore always two words, even when the target would fit in one; the
assembler says so once per file, because the disassembly will honestly show a
`lui rd, 0` that otherwise looks like a bug. `li` is the exception: its operand
is a literal the parser has already read, so it is sized exactly.

## Directives

`.text .data`, `.word .half .byte`, `.ascii .asciz .string`, `.space .zero`,
`.align .org`, `.equ .set`, `.globl .global`.

A directive name is only a directive in the leading position, so `.zero` and
`.align` remain usable as local label names.

## CSRs

`mstatus misa mie mtvec mscratch mepc mcause mtval mip`,
`mvendorid marchid mimpid mhartid`,
`cycle cycleh time instret instreth timeh`.

Each has a WARL write mask, so reserved bits read back at their reset value
rather than accepting anything. The counters are read-only: `csrrw` targeting
one raises IllegalInstruction rather than silently doing nothing. `time`
mirrors `cycle`, which is the honest thing to report for a machine with no real
clock.

## Deliberately absent

Not oversights — each was ruled out because it costs a milestone and buys
nothing for learning assembly on a small FPGA core:

- **C (compressed)** — 16-bit instructions. Everything here assumes 4-byte
  alignment, including `mepc` and the misaligned-fetch trap.
- **F/D (floating point)** — implementable in the emulator, heavy to synthesise.
- **A (atomics)** — one hart, so nothing to be atomic against.
- **User and supervisor mode, PMP, `satp`, interrupt delegation** — most of the
  privileged spec's complexity for none of its teaching value here.
- **Interrupts and a timer** — these add an asynchronous dimension to both the
  executor and reverse stepping.
- **Branch relaxation** — see above.
- **ELF input or output** — no RISC-V toolchain is assumed; this project
  assembles its own code.

## Semantics worth stating

With no reference toolchain available, these are the rules the tests pin down
explicitly, each citing the spec:

- Shift amounts use the low 5 bits only. `x << 32` is `x << 0`.
- `srl` is logical, `sra` arithmetic.
- `sltiu` sign-extends its immediate and *then* compares unsigned, which is
  what makes `sltiu rd, rs, 1` mean "rs == 0".
- `jalr` clears the low bit of the computed target, and computes it before
  writing the link register, so `jalr ra, ra, 0` jumps to the old `ra`.
- `auipc` is relative to its own address, not `pc + 4`.
- Branch and jump targets off a 4-byte boundary raise
  InstructionAddressMisaligned.
- Misaligned loads and stores trap rather than being emulated — on the FPGA
  core this is a reference for, a misaligned access is a real bug.
- Division never traps: `x / 0` is all ones, `x % 0` is `x`,
  `INT32_MIN / -1` is `INT32_MIN` and `INT32_MIN % -1` is 0.
- Division truncates toward zero, and the remainder takes the dividend's sign.
- `csrrs`/`csrrc` with `x0` as source do not write; `csrrw` with `x0` as
  destination does not read. The condition is the register *number*, not its
  value.
- `csrrwi` and friends take a 5-bit zero-extended immediate.
- Writes to `x0` are discarded.
- An `ebreak` halts to the debugger by default; `EbreakBehavior::TrapToMtvec`
  makes it trap instead, which the Zicsr tests use.
- A trap taken while `mtvec` is still zero halts and says so, rather than
  jumping to address 0 and silently restarting the program.
