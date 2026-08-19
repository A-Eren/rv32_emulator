# Memory map

This machine is **Harvard**: instruction memory and data memory are separate
address spaces that both start at address 0. Instruction fetch reads IMEM and
never goes through the data bus; loads and stores reach DMEM and MMIO and can
never see IMEM.

## Instruction memory (IMEM)

| Range | Size | Access |
|---|---|---|
| `0x00000000` – `0x00003FFF` | 16 KB = 4096 instructions | fetch only |

Exported as `imem.mem`. Not readable with `lw`, not writable at all — there is
no self-modifying code on this machine.

## Data memory (DMEM)

| Range | Size | Access |
|---|---|---|
| `0x00000000` – `0x00003FFF` | 16 KB | load / store |

Exported as `dmem.mem`. Address 0 here is a *different location* from address 0
in IMEM.

## Peripherals

A one-kilobyte window at `0xFFFF0000`–`0xFFFF03FF`, divided into 64 slots of 16
bytes. Devices are attached to slots at runtime rather than being fixed, so a
machine can be given the peripherals an exercise needs.

An address in the window with nothing attached **faults**. Reading as zero
would let a program touch a peripheral that is not there and never find out.

The default machine, if nothing says otherwise:

| Slot | Address | Type | |
|---|---|---|---|
| 0 | `0xFFFF0000` | `uart` | `+0` W transmit a byte; `+4` R status, bit 0 = ready |
| 1 | `0xFFFF0010` | `leds` | 32-bit output register |
| 2 | `0xFFFF0020` | `switches` | 32-bit input register |
| 3 | `0xFFFF0030` | `mtime` | cycle counter, low and high words |
| 4 | `0xFFFF0040` | `mtimecmp` | timer compare; an interrupt is pending while `mtime >= mtimecmp` |
| 5 | `0xFFFF0050` | `irq` | `+0` software request, `+4` external request |

And the types that can be added:

| Type | | |
|---|---|---|
| `button` | momentary input; raises the external interrupt line while held |
| `value` | a number the user types in, standing in for a sensor or an ADC |
| `display` | 7-segment digits, one byte each |
| `ram` | a memory block, `size` bytes, optionally preloaded with `load` |
| `rom` | the same, but stores from the program are ignored |

A memory block takes as many slots as its size needs, so a 128-byte block
occupies eight.

### Describing a machine

`examples/board.toml` is a worked example. The format is a deliberate subset of
TOML — arrays of tables with scalar keys — parsed in about a hundred lines
rather than taken as a dependency:

```toml
[[device]]
type = "switches"
slot = 2
value = 0b1010          # a starting position

[[device]]
type = "rom"
slot = 9
size = 256
load = "sine_table.mem" # resolved relative to this file
```

Understood keys: `type`, `slot`, `size`, `value`, `load`, `readonly`, `digits`.
Anything else is an error naming the line and listing what was expected.

Applying a configuration **replaces** the devices rather than adding to them, so
a file describes a machine reproducibly. `File → Save peripherals` writes the
current set back out.

### Editing and undoing

Every device implements a side-effect-free `peek`/`poke` pair, and three things
follow from that:

- The memory view shows a peripheral's registers rather than a hole, and they
  can be edited by typing into it.
- A ROM can be edited that way even though a program cannot write it. Being
  unable to would be an obstacle, not a safeguard — the assembler is where a
  write to read-only memory should be caught.
- Reverse stepping undoes a write to any device with no per-device history at
  all: the debugger's ordinary one-word memory delta plus `poke` covers it. Only
  the UART needs more, since a growing output string is not a register, and its
  length rides in a single extra word.

Peeking cannot consume anything, which matters because a memory view redraws
constantly — merely looking at the machine must not change it.

Switch inputs survive a reset, since physical switches on a board do not move
when the core is reset. A preloaded memory block survives one too: a lookup
table has to still be there after the machine restarts.

### Time is the cycle counter

There is no wall clock. `mtime` is the retired-cycle count, which is the choice
that makes everything else work: the same program takes the same interrupt on
the same cycle every run, so the self-checking test programs stay meaningful and
reverse stepping is exact rather than approximate.

`mtimecmp` is 32 bits here, where a real machine has a 64-bit one that must be
written high half first to avoid firing spuriously in between. That hazard is
worth knowing about; it is not worth inflicting on someone learning assembly,
and no program on a 4096-instruction machine reaches four billion cycles.

`wfi` follows from the same choice. Rather than spinning, it advances the cycle
counter to the armed timer's deadline — the machine has nothing else to do, and
burning instructions would make the cycle count meaningless. With nothing armed
and nothing pending, it halts and says so instead of waiting forever.

## Alignment

Word and halfword accesses must be naturally aligned. A misaligned access raises
`LoadAddressMisaligned` or `StoreAddressMisaligned` rather than being quietly
emulated. This is deliberate: on the FPGA core this emulator is a reference for,
a misaligned access is a real bug, and a named trap teaches alignment where
silent success would hide it.

## Why `auipc` cannot address data

This is the one consequence of the Harvard model that reaches all the way into
the assembler, so it is worth stating plainly.

`auipc` computes an address relative to the program counter — that is, relative
to a position in **IMEM**. A data symbol lives in **DMEM**. Adding an offset to
the pc can never produce a valid DMEM address, because the two spaces are
unrelated.

Therefore:

- `la rd, data_symbol` expands to **absolute** `lui` + `addi`, never
  `auipc` + `addi`.
- `call`, `j` and `tail` target text symbols and expand **pc-relative**.
- Every symbol carries a `Space` tag (`Text` or `Data`), and mixing them is a
  compile error rather than a runtime mystery:

  ```
  error[E0303]: 'msg' is a data symbol, but 'call' requires a text symbol
    --> hello.s:12:10
     |
  12 |     call msg
     |          ^^^ defined in .data at line 3
  ```

- `.word` inside `.text` emits into instruction memory and cannot be read with
  `lw`, so the assembler warns about it.

Handled this way the restriction teaches the architecture. Ignored, it produces
programs that assemble cleanly and behave insanely.

## `.mem` export format

One 32-bit word per line, lowercase `%08x`, no `@` address directives, `\n` line
endings — directly consumable by `$readmemh`:

```verilog
reg [31:0] imem [0:4095];
initial $readmemh("imem.mem", imem);
```

The byte address of line *n* is `n * 4`. `dmem.mem` packs the byte image
little-endian, so `.word 0x12345678` appears as the single line `12345678`.

By default the file is truncated at the high-water mark; `--pad` emits the full
array, which some `$readmemh` uses want in order to avoid a partial-fill warning.

If the target Verilog memory is word-addressed without byte enables, `sb` and
`sh` cannot be implemented in hardware. Pass `--strict-word-mem` to have the
assembler warn on every sub-word access, which catches "works in the emulator,
breaks on the FPGA" before synthesis.
