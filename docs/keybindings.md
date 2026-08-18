# Keys

`rv32 tui <file.s>` opens the debugger; add `--edit` to start in the editor.
`F2` moves between the two.

## Debugging

| Key | | What it does |
|---|---|---|
| `F8` | `s` | Step one **source line**, entering calls |
| `F7` | `i` | Step one **machine instruction** — this is where a pseudo-instruction shows its two halves |
| `F10` | `n` | Step one source line, running calls to completion |
| `F5` | `c` | Run until something stops it. Any key interrupts |
| `F6` | `b` | Step **backwards** one source line |
| `F4` | | Reset the machine, keeping breakpoints |
| `F9` | | Toggle a breakpoint on the selected line |

The status bar shows `[1/2]` when the pc is inside a multi-instruction
pseudo — that is how `li a0, 0x12345` stops being a mystery.

## Moving around

| Key | What it does |
|---|---|
| `↑` `↓` `PgUp` `PgDn` `Home` `End` | Move the selection |
| `g` | Jump back to wherever execution is |
| `Tab` | Move focus between the code pane and the dock |
| `1`–`6` | Choose a dock panel |
| `F11` | Zoom the dock to full screen |

Moving the selection moves what the encoding panel shows, so browsing the
program explains it as you go.

## Dock panels

| Key | Panel | |
|---|---|---|
| `1` | Regs | Both spellings (`x10 a0`), zeros dimmed, the register the last step wrote highlighted and fading |
| `2` | Mem | Data memory; follows a store rather than making you hunt for it |
| `3` | Encode | The instruction as labelled bitfields, and what it did |
| `4` | Out | Whatever the program sent to the UART |
| `5` | I/O | LEDs, switches and the cycle counter as state |
| `6` | Sym | Every symbol with the **space** it lives in — the thing that explains why `call msg` was rejected |

## Editing

| Key | What it does |
|---|---|
| `F5` | Assemble and, if it builds, switch to debugging |
| `F4` | Jump to the first error |
| `F9` | Toggle a breakpoint on the current line |
| `^S` | Save |
| `^Z` / `^Y` | Undo / redo |
| `^A` `^E` `^K` | Start of line, end of line, kill to end of line |
| `Tab` | Insert one indent level (spaces) |
| `^D` / `F2` | Back to the debugger without assembling |
| `^Q` | Quit |

Enter copies the previous line's indentation, and adds a level after a label.
Backspace through leading whitespace removes a whole indent level.

A failed `F5` keeps the buffer, marks the line in the gutter, underlines the
exact span and puts the caret on it. It assembles a detached copy first, so a
broken edit never destroys the program you are already stepping.

## Everything else

| Key | What it does |
|---|---|
| `:` | Type a debugger command — the same set the CLI has |
| `?` / `F1` | Key help |
| `q` | Quit |

The `:` prompt runs the identical code `rv32 dbg` does, so anything documented
for the CLI works here: `break 12`, `x/16xw msg`, `print sp`, `info symbols`,
`watch counter`. `↑` recalls history.
