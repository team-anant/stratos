# stratos

Flight software for the **Gimbal** project, structured as an **actor model**.

An actor is a process that interacts with other actors *purely through asynchronous
events* — in this implementation, POSIX signals. There is no shared memory and no
message queue; every actor is forked from the parent and then talks only by signalling.

The state machine that drives the mission is declared as **static data**, not built at
runtime, so the flight plan can be read as a table.

---

## Architecture

Three kinds of actor:

```
                         ┌──────────────────────────┐
                         │      Parent (main)       │
                         │  owns inputs[]           │
                         │  owns current_state      │
                         └────────────┬─────────────┘
                                      │ fork()
              ┌───────────────────────┼───────────────────────┐
              │                       │                       │
       ┌──────▼──────┐         ┌──────▼──────┐         ┌──────▼──────┐
       │ State actor │         │ State actor │         │ Reader actor│
       │  stationary │         │   rotate    │         │    mock     │
       └─────────────┘         └─────────────┘         └──────┬──────┘
              ▲                                               │
              │ SIGSTOP / SIGCONT                             │ SIGUSR1
              └───────────────────────────────────────────────┘
                             (payload: care + change)
```

| Actor      | Responsibility                                                            |
| ---------- | ------------------------------------------------------------------------- |
| **Parent** | Forks the others. Decodes reader signals, updates the input mask, evaluates the state table, and starts/stops state actors. |
| **States** | Each represents one mode in the flight plan. Only the active state is running; the rest are stopped with `SIGSTOP`. |
| **Readers**| Read a sensor/input and notify the parent *only on change*, sending the affected bits. |

Each actor type is an interface of three function pointers:

```c
// src/states/state.h
struct State  { int (*init)(void); void (*run)(void); void (*exit)(void); };

// src/readers/reader.h
struct Reader { int (*init)(void); void (*run)(void); void (*exit)(void); };
```

Concrete implementations are plain `const` objects (`state_rotate`, `state_stationary`,
`mock`) listed in a table. Adding an actor does not require touching the parent.

---

## Building

Requires `gcc` and a POSIX system (Linux). The build is strict — warnings are errors.

```bash
make          # produces ./stratos
make clean    # removes ./stratos and build/
```

Compiler flags: `-Wall -Wextra -Werror`.

---

## Running

The parent has no input of its own. Input arrives through reader actors, so to run it
you need at least one reader. The bundled **mock reader** reads characters from a FIFO
at `/tmp/stratos`, which makes the flight plan testable without hardware:

```bash
make
mkfifo /tmp/stratos
./stratos
```

Then, from another terminal:

```bash
echo 7 > /tmp/stratos   # digit >= 5  -> Rotate
echo 0 > /tmp/stratos   # digit <  5  -> Stationary
```

Expected output (each state prints once per second):

```
Stationary start
Rotate start
Mock reader started
Stationarying
Stationarying
Rotating          <- after `echo 7`
Rotating
Stationarying     <- after `echo 0`
Stationarying
```

> **Note:** state actors inherit the parent's stdout. If you redirect output to a file
> it will be block-buffered, so you may see nothing until exit — use
> `stdbuf -o0 ./stratos` when redirecting.

Press `Ctrl-C` / kill the process when done; there is no graceful shutdown yet (see
[Limitations](#limitations)).

---

## Signal protocol

Linux lets a signal carry 32 bits of payload (`si_value.sival_int`). The parent splits
it into two 16-bit halves:

| Bits   | Field    | Meaning                                              |
| ------ | -------- | ---------------------------------------------------- |
| 31..16 | `care`   | which of the sender's `scope[]` entries this message updates |
| 15..0  | `change` | the new bit values, one bit per `scope[]` entry       |

A reader declares up to `MAX_AFFECTED_BITS` (16) input bits it is allowed to affect:

```c
// src/reader_table.h
#define MAX_AFFECTED_BITS 16   // 32-bit payload split in half

struct ReaderEntry {
  pid_t pid;
  const Reader *reader;
  const size_t scope_len;                  // how many entries of scope are valid
  const uint8_t scope[MAX_AFFECTED_BITS];  // the indices of bits the reader affects
};
```

Position `j` in `scope[]` maps to:

- `care` bit `0x8000 >> j`
- `change` bit `MAX_AFFECTED_BITS - 1 - j`

and `scope[j]` names the input bit that gets written.

### Worked example

The mock reader has `scope_len = 2`, `scope = {0, 1}` — it owns input bits 0 and 1.

| Payload      | `care`   | `change` | Effect                                    |
| ------------ | -------- | -------- | ----------------------------------------- |
| `0xFFFF8000` | `0xFFFF` | `0x8000` | bit 0 → 1, bit 1 → 0  ⇒ `inputs = 0x80`   |
| `0xFFFF4000` | `0xFFFF` | `0x4000` | bit 0 → 0, bit 1 → 1  ⇒ `inputs = 0x40`   |
| `0xFFFFC000` | `0xFFFF` | `0xC000` | both → 1             ⇒ `inputs = 0xC0`     |

Because `care` is `0xFFFF`, both scope entries are updated every message. A reader can
send a narrower `care` mask to update only some of its bits.

The parent identifies the sender by `info->si_pid`, matches it against
`reader_table[i].pid`, and ignores signals from unknown processes.

> The 16-bit ceiling is a hard property of the 32-bit payload, not a tunable — hence the
> `DO NOT CHANGE` note at the definition. Supporting more than 16 affected bits per
> reader requires a different transport.

---

## State table

Input bits are stored as **words of 8**, with bit index `n` living at
`inputs[n / 8]` as mask `0x80 >> (n % 8)` — i.e. index 0 is the most significant bit of
`inputs[0]`.

```c
// src/state_table.h
#define MAX_INPUT_FIELDS 2
#define WORDS ((MAX_INPUT_FIELDS + 7) / 8)   // currently 1 -> 8 input bits

typedef struct {
  const uint8_t care[WORDS];   // 1 = must match, 0 = don't care ("X")
  const uint8_t expect[WORDS]; // required value for the cared bits
  const size_t next;
} Transition;
```

A transition matches when:

```c
(inputs[i] & care[i]) == (expect[i] & care[i])     // for every word
```

So `care = 0x00` is fully don't-care and `care = 0xFF` is an exact match. There is no
separate "don't care" symbol — it is expressed by clearing the bit in `care`.

### Current flight plan

`NO_OF_STATES = 2`, at most `MAX_TRANSITIONS = 2` transitions per state:

| # | State        | `expect` (care `0xFF`) | `next` | Meaning               |
| - | ------------ | ---------------------- | ------ | --------------------- |
| 0 | `stationary` | `0x40`                 | 1      | input bit 1 ⇒ Rotate  |
|   |              | `0x80`                 | 0      | input bit 0 ⇒ stay    |
| 1 | `rotate`     | `0x80`                 | 0      | input bit 0 ⇒ Stationary |
|   |              | `0x40`                 | 1      | input bit 1 ⇒ stay    |

Transitions are evaluated **in order** and the first match wins; having `next` equal the
current state means "stay", and the parent will not issue `SIGSTOP`/`SIGCONT`.

The initial state is index `0` (`static int current_state = 0` in `src/main.c`).

---

## Repository layout

```
Makefile
src/
├── main.c              Parent actor: forks children, decodes signals, switches state
├── reader_table.{c,h}  Reader registry + scope declaration
├── state_table.{c,h}   State table + transition encoding
├── utils.{c,h}         signals_install(), compare_input_mask()
├── readers/
│   ├── reader.h        Reader interface
│   └── mock.c          Mock reader (FIFO at /tmp/stratos) — stands in for SPI
└── states/
    ├── state.h         State interface
    ├── stationary.c    Stationary mode
    └── rotate.c        Rotate mode
```

---

## Extending

### Add a state

1. Create `src/states/mymode.c` with `init` / `run` / `exit` functions and a
   `const State state_mymode = { ... }`.
2. Bump `NO_OF_STATES` in `src/state_table.h`.
3. Add its `extern` declaration and a `StateEntry` (with its transitions) in
   `src/state_table.c`.

### Add a reader

1. Create `src/readers/myreader.c` with `const Reader myreader = { ... }`.
2. Bump `NO_OF_READERS` in `src/reader_table.h`.
3. Declare the input bits it owns in `scope[]` and add its `ReaderEntry` in
   `src/reader_table.c`.
4. Send `SIGUSR1` to `getppid()` with `sigqueue(...)` and a `care | change` payload.

The `Makefile` picks up new files automatically via `wildcard`, so no build edits are
needed.

---

## Limitations

Tracked, not yet addressed:

- **No graceful shutdown.** Children are never reaped or signalled on exit; `main()`
  ends in `while (1) pause();`.
- **No error cleanup.** A failed `fork()` returns immediately without killing
  previously forked children.
- **No SPI reader.** `mock.c` is a stand-in; the real SPI reader from `flight_plan.c`
  has not been ported to the `Reader` interface.
- **`State.exit` is never called** — nothing invokes it on transition.
- **`pid` / `pwm` / `kalmanfilter` are not implemented** in this tree; only the state
  and reader scaffolding plus the mock exists.
- **`running` is overloaded** — the same flag drives stop/continue logic in state and
  reader actors, but only state actors install a `SIGTSTP` handler.
- **`MAX_INPUT_FIELDS = 2`** yields a single 8-bit word, so multi-word input handling
  is not yet exercised.
