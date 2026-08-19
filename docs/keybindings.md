# Keys

`rv32-gui <file.s>` opens the desktop window. Everything below is also on the
toolbar and in the **Run** menu, so nothing here has to be memorised first.

## Running

| Key | Button | What it does |
|---|---|---|
| `F5` | Assemble | Assemble the buffer and load it |
| `F8` | Step | One **source line**, entering calls |
| `F7` | Step Instr | One **machine instruction** — this is where a pseudo-instruction shows its two halves |
| `F10` | Step Over | One source line, running calls to completion |
| `F9` | Run | Run until something stops it. Pressing it again interrupts |
| `F6` | Back | Step **backwards** one source line |
| `F4` | Reset | Restart the machine, keeping breakpoints |

The status bar shows `[1/2]` when the pc is inside a multi-instruction
pseudo-instruction — that is how `li a0, 0x12345` stops being a mystery.

`Back` greys out when there is no history left to undo.

## Writing assembly

Type two characters of a word and a small list appears with what fits there.
`Ctrl+Space` asks for it explicitly, including with nothing typed — which is
how to answer "what can go here?"

```
        ad│
        ┌──────┐
        │ add  │
        │ addi │
        └──────┘
```

The list knows where the caret is. In the leading position it offers
instructions, pseudo-instructions and directives; in an operand position it
offers only what belongs there — registers, CSR names, or the program's own
labels.

Once the instruction is chosen, the operands it still wants appear in grey
after the caret, the way a form shows what a field is for. They are not text —
typing goes straight through them, and each one disappears as it is filled:

```
        addi rd, rs1, imm          nothing written yet
        addi a0, rs1, imm          rd is done
        addi a0, a1, imm(-2048..2047)
        addi a0, a1, 4             nothing left to say
```

A field shows its **range** only when its name does not already answer the
question. `rd` and `label` say everything; `imm` does not tell you that 5000
will be rejected, so it shows `imm(-2048..2047)` while it is the field being
asked for.

Both come from `instr_table.def`, the same rows the assembler encodes with, so
a hint cannot promise something F5 will refuse.

| Key | What it does |
|---|---|
| `Ctrl+Space` | Offer completions here |
| `Enter` / `Tab` | Accept the highlighted one, leaving the caret at its operands |
| `Esc` | Dismiss the list |

## Editing

Standard editing keys, plus:

| Key | What it does |
|---|---|
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | New, open, save |
| Click the margin | Toggle a breakpoint on that line |
| Click a diagnostic | Jump to the line it points at |

A failed `F5` keeps the buffer, marks the line in the margin, underlines the
exact span and puts the caret on it. It assembles a detached copy first, so a
broken edit never destroys the program you are already stepping.

## What the panels show

| Panel | |
|---|---|
| Registers | Both spellings (`x10  a0`), zeros dimmed, and the register the last step wrote highlighted **and scrolled into view** |
| Encoding | The instruction as labelled bitfields, and what it did. Follows the caret, so a line explains itself before it has ever run |
| Memory | Data memory and the peripherals, following the last store. **Editable** — type a hex value into a cell to write it |
| Devices | What is attached, with buttons to add and remove, and each device's own controls: switch toggles, buttons, value fields |
| Console | Whatever the program sent to the UART |
| Symbols | Every symbol with the **space** it lives in — the thing that explains why `call msg` was rejected |

## The command-line debugger

`rv32 dbg <file.s>` is the same engine without a window, for scripting or over
ssh:

```
(rv32) break 12
(rv32) run
(rv32) info reg
(rv32) x/16xw msg
(rv32) si
(rv32) back
```

`help` lists the commands; an empty line repeats the last one, so Enter keeps
stepping.
