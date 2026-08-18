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

## Memory-mapped I/O

Sixteen slots of sixteen bytes at `0xFFFF0000`–`0xFFFF00FF`, dispatched by
`(addr >> 4) & 0xF`.

| Address | Access | Meaning |
|---|---|---|
| `0xFFFF0000` | W | UART transmit — the low byte is appended to the console |
| `0xFFFF0004` | R | UART status; bit 0 = transmitter ready (always 1) |
| `0xFFFF0010` | RW | LED output register, 32 bits |
| `0xFFFF0020` | R | Switch input register, 32 bits (writes ignored) |
| `0xFFFF0030` | R | Cycle counter, low word |
| `0xFFFF0034` | R | Cycle counter, high word |

Switch inputs survive a reset, since physical switches on a board do not move
when the core is reset.

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
