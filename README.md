# rv32

[![build and test](https://github.com/A-Eren/rv32/actions/workflows/ci.yml/badge.svg)](https://github.com/A-Eren/rv32/actions/workflows/ci.yml)

An RV32IM_Zicsr emulator, assembler and debugger built for **learning and
teaching assembly**, with machine-mode interrupts. Not a fast emulator — a visible one. Every design decision
serves three questions:

1. **What exactly changed in this step?** — register and memory diff highlighting
2. **How did this instruction become 32 bits?** — a bitfield breakdown panel
3. **Why did it not assemble?** — line, column, caret, and a suggested fix

The editor suggests the instruction in a strip that folds away once it is
chosen, then greys the operands it still wants in after the caret — a form to
fill rather than a manual to consult. Both are generated from the same table
the assembler encodes with — so a hint
cannot promise something F5 will refuse. Peripherals are configurable: add a
button, a sensor, a display or a block of preloaded ROM, and edit any of it by
hand while stopped.

The centrepiece is an embedded editor: write assembly, press F5, and step
through what you wrote.

## Status

| Milestone | Scope | State |
|---|---|---|
| M0 | Build skeleton, test harness | ✅ done |
| M1 | Registers, memory, first instructions | ✅ done |
| M2 | Full ISA, traps, CSRs, disassembler, `.mem` export | ✅ done |
| M3 | Assembler + source map | ✅ done |
| M4 | CLI debugger | ✅ done |
| M5 | Front-end shell | ✅ done |
| M6 | Embedded editor | ✅ done |
| M7 | Encoding panel, diff highlighting, reverse step | ✅ done |
| M8 | Polish, examples, docs | ✅ done |

## Building

Requires a C++17 compiler, CMake 3.20+ and Ninja. Qt6 is optional: without it
everything still builds, minus the desktop window.

```bash
sudo apt install ninja-build qt6-base-dev
```

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
./build/debug/rv32 run examples/timer.s        # a timer interrupt, from scratch
./build/debug/rv32 test tests/asm/*.s          # self-checking assembly tests
./build/debug/rv32 dbg examples/fib.s          # interactive debugger
./build/debug/rv32-gui examples/fib.s          # the desktop window
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

The rule that shapes everything: **nothing below `rv_cmd` knows a user
interface exists.** The engine is a library, the window is a shell over it, and
the command-line debugger is a second shell over the same one.

```
rv_isa    instruction table, encode, decode, disassemble    (no dependencies)
 ├── rv_core   registers, memory, MMIO, execute, traps, interrupts
 └── rv_asm    lexer, parser, assembler
       └── rv_dbg    breakpoints, stepping, reverse step
             └── rv_cmd    command registry + REPL, no Qt
                   ├── rv32-gui   the desktop window
                   └── rv32       the command-line tools
```

Both front-ends are shells over `DebugSession` and `rv_cmd`. Neither implements
stepping, breakpoints or highlighting of its own, which is why they cannot
disagree about what any of those mean.

`rv_cmd` sitting *below* both front-ends is deliberate: `break`, `step` and
`x/16xw` are implemented once, and the window reaches the same registry the
command line does.

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
- **Offscreen GUI tests** — the window is created for real on Qt's offscreen
  platform, driven through the actions the toolbar calls, then asked what it
  shows. No display needed, so it runs in CI, and it exercises the real path
  rather than a parallel one.

## License

MIT. See [LICENSE](LICENSE).
