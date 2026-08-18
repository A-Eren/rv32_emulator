# rv32

An RV32IM_Zicsr emulator, assembler and debugger built for **learning and
teaching assembly**. Not a fast emulator — a visible one. Every design decision
serves three questions:

1. **What exactly changed in this step?** — register and memory diff highlighting
2. **How did this instruction become 32 bits?** — a bitfield breakdown panel
3. **Why did it not assemble?** — line, column, caret, and a suggested fix

The centrepiece is an embedded editor: write assembly in the app, press F5,
and step through what you wrote.

```
  EDIT  fib.s*   Ln 3, Col 5  1 error
 ▶ 1│_start:
   2│    li   a0, 5
E  3│    addii a1, a0, 3
   4│    ebreak

unknown instruction 'addii'
F5 assemble+debug  ^S save  ^Z undo  F9 bp  F4 next error  ? help  ^Q quit
```

## Status

| Milestone | Scope | State |
|---|---|---|
| M0 | Build skeleton, test harness | ✅ done |
| M1 | Registers, memory, first instructions | ✅ done |
| M2 | Full ISA, traps, CSRs, disassembler, `.mem` export | ✅ done |
| M3 | Assembler + source map | ✅ done |
| M4 | CLI debugger | ✅ done |
| M5 | TUI shell | ✅ done |
| M6 | Embedded editor | ✅ done |
| M7 | Encoding panel, diff highlighting, reverse step | ✅ done |
| M8 | Polish, examples, docs | ✅ done |

## Building

Requires a C++17 compiler, CMake 3.20+, Ninja, and `ncursesw` (for the TUI, from M5).
No other dependencies: the test harness is 150 lines in `tests/rv_test.hpp`, and
there is no external RISC-V toolchain — this project assembles its own code.

```bash
cmake --preset debug
cmake --build build/debug
ctest --preset debug
```

Presets: `debug` (warnings as errors), `asan` (+ASan/UBSan), `release`.

## Trying it

```bash
./build/debug/rv32 run examples/fib.s          # assemble and run
./build/debug/rv32 asm examples/fib.s          # write imem.mem and dmem.mem
./build/debug/rv32 asm examples/fib.s --annotate   # ... with disassembly comments
./build/debug/rv32 run examples/fib.s --trace  # one line per instruction
./build/debug/rv32 test tests/asm/*.s          # self-checking assembly tests
./build/debug/rv32 dbg examples/fib.s          # interactive debugger
./build/debug/rv32 tui examples/fib.s          # full-screen debugger
./build/debug/rv32 tui examples/fib.s --edit   # ... starting in the editor
```

The debugger steps by source line by default, so a `li` that expands to two
instructions counts as one step — and `si` shows the seam:

```
(rv32) break done
breakpoint 1 at line 32
(rv32) run
breakpoint 1 at 0x00000028 (line 32)
=> 00000028  lui a0, 0x0        [line 32, 1/2]
   la      a0, result
(rv32) back
=> 00000024  jal zero, 0x10        [line 27]
   j       loop
```

`back` is not a typo: every step records what it changed, so execution runs
backwards as easily as forwards.

The error messages are the part worth looking at:

```
error[E0310]: 'call' needs a code address, but this is a data symbol
  --> bad.s:9:17
   |
 9 |         call    msg
   |                 ^^^ lives in data memory
 2 | msg:    .asciz "hello"
   | ^^^^ defined in .data here
   |
help: instruction memory and data memory are separate address spaces on this
      machine; use 'la' to load a data address into a register
```

```
error[E0201]: undefined symbol 'lop'
  --> bad.s:8:25
   |
 8 |         beq     a0, a1, lop
   |                         ^^^ not defined in this file
   |
help: did you mean 'loop'? (defined at line 11)
```

## Documentation

- [`docs/keybindings.md`](docs/keybindings.md) — every key, and what the panels show
- [`docs/memory-map.md`](docs/memory-map.md) — the two address spaces, MMIO, and the `.mem` format
- [`docs/isa-support.md`](docs/isa-support.md) — what is implemented, what is not, and why

## Architecture

The rule that shapes everything: **ncurses appears in exactly one library and
two translation units.** Beyond testability, `<curses.h>` `#define`s ordinary
words — including `instr`, `move`, `clear`, `line` and `timeout` — so letting it
leak into a codebase full of `DecodedInstr` would produce unreadable errors.

```
rv_isa    instruction table, encode, decode, disassemble    (no dependencies)
 ├── rv_core   registers, memory, MMIO, execute, traps
 └── rv_asm    lexer, parser, assembler                     (M3)
       └── rv_dbg    breakpoints, stepping, reverse step    (M4)
             └── rv_cmd    command registry + REPL, no ncurses
                   ├── rv_tui    the TUI                    (M5)
                   └── rv32      the binary
```

`rv_cmd` sitting *below* both front-ends is deliberate: `break`, `step` and
`x/16xw` are implemented once, and the TUI's `:` prompt runs the same code the
CLI does.

### The ISA is described once

`src/isa/instr_table.def` is an X-macro table with one row per instruction. It
generates the `InstrId` enum, the descriptor table, and the mnemonic lookup —
so the decoder, the disassembler, the encoder and the assembler all read from
one description and cannot drift apart.

Execution semantics deliberately stay out of it. `src/core/exec.cpp` is a plain
`switch` over `InstrId` with **no `default:` label**, compiled under
`-Werror=switch`: adding a row to the table breaks the build until the executor
handles it. That is the same "you cannot forget one" guarantee a generated
dispatch table gives, without putting semantics inside a macro — and semantics
are exactly what this project exists to make readable.

### Harvard memory — and what it costs

IMEM (16 KB) and DMEM (16 KB) are **separate address spaces**, both starting at
0. Instruction fetch never touches the data bus. See `docs/memory-map.md` for
the consequence that shapes the assembler: `la a0, some_data_label` cannot use
`auipc`, because that would compute an address in the wrong space.

## Testing

```bash
ctest --preset debug                    # everything
./build/debug/rv_tests --filter=exec_m  # one suite
./build/debug/rv_tests --list           # what exists
```

With no reference toolchain available, correctness comes from three sources:

- **Round-trip properties** — `encode(decode(w)) == w` over 500 000 random
  words, plus every table row against itself. No golden files, and it catches
  any misplaced bit mechanically.
- **A semantic checklist** — each test cites the spec rule it enforces
  (`jalr` clearing the low bit, `div` by zero yielding −1, `srai` sign
  behaviour, shift amounts masked to 5 bits).
- **Self-checking `.s` programs** — riscv-tests style, written in the language
  the tool is *for*. One of them exercises the assembler, encoder, decoder,
  executor and traps in a single run.
- **Headless rendering** — every TUI panel draws into an in-memory canvas, so
  the interface is tested by pressing keys and looking at the resulting frame.
  That is the same path `--render-once` takes, so the tests cover the real code
  rather than a parallel one.
