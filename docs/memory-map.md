# Memory map

This machine is **Harvard**: instruction memory and data memory are separate
address spaces that both start at address 0. Instruction fetch reads IMEM and
never goes through the data bus; loads and stores reach DMEM and MMIO and can
never see IMEM.

## Instruction memory (IMEM)

| Range | Size | Access |
|---|---|---|
| `0x00000000` – … | 16 KB by default = 4096 instructions | fetch only |

**The size is configurable** — the two fields beside `Add…` in the Devices
panel, or `[machine]` in a board file. Between 256 bytes and 1 MB, a multiple
of four, since both spaces are exported a word per line. Growing a memory keeps
what was in it; shrinking one drops what no longer fits, so press F5 afterwards
to assemble into the machine you now have.

Exported as `imem.mem`. Not readable with `lw`, not writable at all — there is
no self-modifying code on this machine.

## Data memory (DMEM)

| Range | Size | Access |
|---|---|---|
| `0x00000000` – … | 16 KB by default | load / store |

Configurable in the same place. Exported as `dmem.mem`. Address 0 here is a *different location* from address 0
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
| `custom` | your own: a `name`, `size` registers the program can read and write, each one settable by hand, and a line it can raise an interrupt on |

A memory block takes as many slots as its size needs, so a 128-byte block
occupies eight.

Every block's first sixteen words are offered as fields to type into, in the
Devices panel, alongside the switches and buttons. A block is as much an input
as a bank of switches is: an exercise that reads a table wants the table set by
hand, and going through the memory view to do it means knowing the address the
block happened to land at. What is typed survives a reset — it was not the
program that put it there. The rest of a large block is still reachable in the
memory view; a hundred fields would not be an improvement.

`custom` is the one entry that is not a fixed part somebody else designed. It
is what an exercise reaches for when the peripheral it needs is not in the
list — and being able to raise an interrupt means a student can build the
source of one, not only handle one that was provided.

### Overlapping addresses

Two devices may be given addresses that overlap. The later arrival wins the
addresses they share; the earlier one stays attached, and the panel says
`covered by another device` or `(partly covered)` for it.

This is allowed rather than refused because a machine being built by hand
spends most of its life half-built, and stopping the work to complain is worse
than showing what the overlap did. It is a warning, at the point of making it
and afterwards in the list — never a silent one, since a device that quietly
stopped answering is the bug that costs an afternoon.

### Describing a machine

`examples/board.toml` is a worked example. The format is a deliberate subset of
TOML — arrays of tables with scalar keys — parsed in about a hundred lines
rather than taken as a dependency:

```toml
[machine]
imem = 8192             # bytes; omit either to keep what the machine has
dmem = 4096

[[device]]
type = "switches"
address = 0xffff0020    # or slot = 2, which is the same place
value = 0b1010          # a starting position

[[device]]
type = "custom"
name = "sensor"
address = 0xffff0030
size = 2                # registers, for a custom peripheral

[[device]]
type = "rom"
address = 0xffff0090
size = 256              # bytes, for a block
load = "sine_table.mem" # resolved relative to this file
```

Understood keys: `type`, `address` or `slot`, `name`, `size`, `value`, `load`,
`readonly`, `digits`, and `imem`/`dmem` under `[machine]`. Anything else is an
error naming the line and listing what was expected.

Addresses are written rather than slot numbers because an address is what the
program uses and what every panel shows; a slot number is arithmetic. An
address inside a slot names that slot, so `0xffff002c` and `0xffff0020` mean the
same device.

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
