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
| Memory | Data memory, following the last store |
| Console | Whatever the program sent to the UART |
| I/O | LEDs, switches, `mtime`/`mtimecmp`, and which interrupt lines are pending, unmasked and takeable |
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
