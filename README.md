# rv32

[![build and test](https://github.com/A-Eren/rv32_emulator/actions/workflows/ci.yml/badge.svg)](https://github.com/A-Eren/rv32_emulator/actions/workflows/ci.yml)

An RV32IMC_Zicsr_Zifencei emulator, assembler and debugger built for **learning
and teaching assembly**, with machine-mode interrupts. Not a fast emulator — a visible one. Every design decision
serves four questions:

1. **What exactly changed in this step?** — register and memory diff highlighting
2. **How did this instruction become 16 or 32 bits?** — a bitfield breakdown
   panel, which for a compressed instruction also shows the one it expands to
3. **Why did it not assemble?** — line, column, caret, and a suggested fix
4. **Where is each instruction right now?** — the five pipeline stages with the
   instruction in each, the space-time diagram underneath, and the same five
   stages named in the editor margin beside the code, with the line that has
   just finished in yellow and a branch in green or red once it has reached EX
   and knows whether it was taken. Stepping by cycle rather than by instruction
   is what makes a branch's cost visible

The editor suggests the instruction in a small block under the caret that folds
away once it is chosen, then greys the operands it still wants in after the caret — a form to
fill rather than a manual to consult. Both are generated from the same table
the assembler encodes with — so a hint
cannot promise something F5 will refuse. **Generate**, next to Assemble, writes both memories as `$readmemh` images,
padded to the depth the size list advertises — the same number the Block Memory Generator asks for, so
the file drops into a BRAM without arithmetic in between.

The machine is configurable too: both memory sizes, and
the peripherals — a button, a display, a block of preloaded ROM, or one you
define yourself with registers you can type into and an interrupt line you can
raise. All of it editable by hand while stopped.

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
| M9 | Pipeline view: five stages, space-time diagram | ✅ done |

The pipeline is a picture of the machine, not a re-timing of it: instructions
still execute atomically, `mcycle` still counts them, and the panel keeps its
own cycle count. Hazards, stalls, forwarding and branch prediction are not
implemented yet — the front-end fetches straight past a branch and discards
what it fetched when the branch goes the other way.

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

### Linux

Download **rv32-x86_64.AppImage** from
[Releases](https://github.com/A-Eren/rv32_emulator/releases/latest), then run
the installer once:

```bash
./packaging/install.sh ~/Downloads/rv32-x86_64.AppImage
```

rv32 is then in the applications menu with its own icon, `.s` files open with
a double-click, and `rv32` and `rv32-gui` work from a terminal. Everything
lands under `~/.local`, nothing needs root, and `install.sh --uninstall` takes
it all back out.

The installer unpacks the AppImage rather than leaving it as one file, because
mounting an AppImage needs `libfuse2` and several current distributions no
longer install it -- a double-click that silently does nothing is a poor way
to discover that. If your machine does have it, the AppImage runs on its own
after `chmod +x`.

To build it yourself: `./packaging/build-appimage.sh`. It fetches its own
tools into `build/` and needs no root.

### Windows

Nothing to build and nothing to install. Go to
[Releases](https://github.com/A-Eren/rv32_emulator/releases/latest), download
**rv32-windows-x64.zip**, unzip it anywhere and double-click `rv32-gui.exe`.
The Qt DLLs and the platform plugins travel with it, and the directory in the
zip is the one CI just ran with nothing but Windows on `PATH` — so it starts on
a machine that has never seen a compiler.

`rv32.exe`, the command-line half, is linked statically and is a single file
you can copy onto a lab machine on its own.

Every push to `main` also leaves the same directory as a build artifact on the
[Actions](https://github.com/A-Eren/rv32_emulator/actions/workflows/ci.yml)
run, which is the way to get a build that has no tag yet.

To build it yourself, install [MSYS2](https://www.msys2.org/) and, from a
**UCRT64** shell:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-tools}
cmake --preset release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

UCRT64 rather than MSVC on purpose: `cmake/CompilerWarnings.cmake` is a GCC
warning set, so Windows compiles the same code with the same compiler as Linux
and a warning cannot hide on one platform. UCRT rather than msvcrt because the
C99 `printf` formats have to work.

`src/cmd/tty.cpp` is the only file that asks the operating system anything —
whether a stream is a terminal, and whether it will render colour. On Windows
that second question involves asking the console to turn ANSI escapes on, which
conhost does not do by itself.

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

./build/debug/rv32 run examples/fib.s --devices examples/board.toml
```

`--devices` gives the run the machine a board file describes: its peripherals
and its memory sizes both. The same file the GUI loads with `File → Load
peripherals`, so a machine assembled by hand in the window can be handed
straight to a script. See [docs/memory-map.md](docs/memory-map.md).

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
  words, plus every table row against itself. The 16-bit space is small enough
  to do properly: all 65 536 compressed halfwords are swept exhaustively, holes
  included. No golden files, and it catches
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
