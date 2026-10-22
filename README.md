This document explains the working of the flight plan v2.0 for the Gimbal project. This flight plan is structured in according to an actor model. An Actor is an entity that interacts with other actors purely through asynchronous events (like signals). In our program, an actor is materialized through a process that is forked from an initial process.

### Code Principles
---
#### Types of Actors
---
The three types of actors in our system are the Parent, States and Readers. Readers are actors that concurrently read multiple inputs across the satellite and report changes to the parent. States are actors that represent various modes in the state table and Parent is the Actor that controls all the States.

Each actor type is described by an interface of three function pointers (`init`, `run`, `exit`), so a concrete actor is a plain `const` object held in a table rather than a special case in the parent. Adding a reader or a state therefore means adding a file and a table entry — the parent's logic does not change. The interfaces and table rows are listed under [State Machine Structures](#state-machine-structures).

##### Parent
This process controls all the other processes. Communication between other processes is strictly through signals. The reader actors signal the parent process a change in the input mask, along with the modifications to be done to the input mask. The parent process then updates the input mask and uses it to decide the next state (based on a state table).

The parent is `src/main.c`. It forks every state actor and every reader actor, records their PIDs in the tables, installs the signal handlers, and then blocks in `pause()` — all decision making happens inside the signal handler.

##### Readers
A reader actor is a process that reads a specific input. The reader sends signals to the parent whenever there is a change in the input (based on certain thresholds). In the Gimbal project we have only a single reader, which checks the input from the antenna using the SPI communication protocol.

A reader declares, in advance, which input bits it is allowed to affect. It does this with a `scope[]` array of input bit indices, of which only the first `scope_len` entries are valid. The parent uses that declaration to map a 32-bit signal onto the input mask, so the reader never needs to know the absolute position of its bits in the global mask.

> The SPI reader has not yet been ported to this interface. `src/readers/mock.c` currently stands in for it: it reads characters from a FIFO at `/tmp/stratos` and signals the parent, so that the state machine can be exercised without hardware.

##### States
A state actor represents a mode in the flight plan. A mode represents the set of instructions that the satellite must perform under certain conditions. In the Gimbal project we have two states, which represent the stationary and rotatory modes. The modes are described as follows:
- Stationary mode: This mode indicates that the satellite must have a fixed orientation.
- Rotatory mode: This mode indicates that the satellite must have a constant angular velocity.

Only one state actor runs at a time. Every state actor pauses itself with `raise(SIGSTOP)` immediately after `init()`. The parent starts the initial state with `SIGCONT`, and switches states by sending `SIGSTOP` to the outgoing actor and `SIGCONT` to the incoming one.

#### State Table
---
The decision to switch to the next mode is taken by the parent based on the state table. The state table consists of multiple rows containing the current state, inputs (represented through an input bitmask) and the next state.

The table is declared as static data in `src/state_table.c` rather than built at runtime, so the flight plan can be read directly instead of being traced through a sequence of setup calls. Each row of a state carries its own list of transitions, and that list is evaluated **in order** — the first match wins. If the matched row's `next` equals the current state, the parent leaves the processes alone, which is how a self-transition ("stay") is expressed.

##### Input Bitmask
The input bitmask consists of an array of cells. Each cell can either be a 0, 1 or X (Don't care). This array is used by the parent to switch to the next state. For example, let a machine be in state A, which will switch to state B if its input mask is of the form 1X0X. Now, if our current inputs were represented by 1100, 1101, 1001 or 1000, then we would switch to state B.

Previously this was implemented with two binary bitmasks named `bitmask1` and `bitmask0`, and an equality test using the formula `(AB + A'C')'`, which returned an inverted result and made call sites read backwards. That pair has been replaced by `care` and `expect`:

- **`expect`** holds the value the cared bits must have.
- **`care`** holds a `1` where the cell is `0` or `1` (must match), and a `0` where the cell is `X` (don't care). There is no longer a separate `X` symbol — "don't care" is just a cleared bit in `care`.

The equality test is now a single mask-and-compare per word: `(inputs[i] & care[i]) == (expect[i] & care[i])`.

In the example given above, 1X0X is represented by

- `care` = 1010
- `expect` = 1000

Now if our input is A = 1100, 1101, 1001 or 1000, then `A & care` is always `1000`, which equals `expect & care`, so the row matches. An input such as 1110 gives `1010` and does not match.

The following table represents the state table for the Gimbal project:

| Current State | Input Bitmask (care / expect) | Next State |
| ------------- | ----------------------------- | ---------- |
| Stationary    | `0xFF` / `0x80`               | Stationary |
| Stationary    | `0xFF` / `0x40`               | Rotatory   |
| Rotatory      | `0xFF` / `0x80`               | Stationary |
| Rotatory      | `0xFF` / `0x40`               | Rotatory   |

Here input bit index 0 is the mask `0x80` and index 1 is the mask `0x40` — input bit index `n` lives at `inputs[n / 8]` under mask `0x80 >> (n % 8)`, i.e. index 0 is the **most** significant bit. So bit 0 selects the stationary mode and bit 1 selects the rotatory mode, which is the same pair of conditions the previous three-cell rows (`xxx0` / `xxx1`) expressed, now written over one word with an exact `care` mask.

#### Mode switching
---
Mode switching occurs when a reader sends a signal to the parent (along with the changes in the inputs). The signal API of the Linux Kernel allows us to send a maximum of 32 bits of information.

The mechanism used instead divides the 32 bits into two halves that are read in parallel rather than as a mask plus an index:

- **`care`** (upper 16 bits) — one bit per scope entry, saying whether that entry is being written in this message.
- **`change`** (lower 16 bits) — one bit per scope entry, giving the value to write.

The position within these halves is what selects the scope entry, so the j-th bit from the **MSB** of `care` and the j-th bit from the **MSB** of `change` both refer to `scope[j]`:

```
 31                              16 15                               0
┌─────────────────────────────────┬───────────────────────────────────┐
│             care                │              change               │
│  1 = write scope[j]             │  1 = that bit becomes 1           │
│  0 = leave scope[j] untouched   │  0 = that bit becomes 0           │
└─────────────────────────────────┴───────────────────────────────────┘
        bit 0x8000 >> j                       bit 0x8000 >> j
                 │                                     │
                 └──────────────► scope[j] ◄───────────┘
                                    │
                                    ▼
        inputs[scope[j] / 8] : clear 0x80 >> (scope[j] % 8)
                               then set it if the change bit is 1
```
- **Bits not named in `care` are untouched.** A payload of `0x80008000` writes only `scope[0]`, leaving `scope[1]` at whatever value it already held.

As a concrete example, the mock reader declares `scope_len = 2`, `scope = {0, 1}`, and sends:

| Payload      | `care`   | `change` | Resulting input mask              |
| ------------ | -------- | -------- | --------------------------------- |
| `0xFFFF8000` | `0xFFFF` | `0x8000` | `0x80` — bit 0 written 1, bit 1 written 0 |
| `0xFFFF4000` | `0xFFFF` | `0x4000` | `0x40` — bit 0 written 0, bit 1 written 1 |
| `0xFFFFC000` | `0xFFFF` | `0xC000` | `0xC0` — both written 1           |
| `0x80008000` | `0x8000` | `0x8000` | `0x80` — only bit 0 written       |

### State Machine Structures
---

#### Actor Interfaces
```c
// src/readers/reader.h
typedef struct Reader Reader;
struct Reader {
  int (*init)(void);
  void (*run)(void);
  void (*exit)(void);
};

// src/states/state.h
typedef struct State State;
struct State {
  int (*init)(void);
  void (*run)(void);
  void (*exit)(void);
};
```
- **Reader** / **State**: The actor interfaces. A concrete actor is a `const` object holding three function pointers — `mock` for the reader, `state_stationary` and `state_rotate` for the states.
- Both interfaces are identical in shape; they are kept as separate types so that a reader cannot be installed in the state table, or vice versa.

#### Table Rows
```c
// src/reader_table.h
struct ReaderEntry {
  pid_t pid;
  const Reader *reader;
  const size_t scope_len;                  // how many entries of scope are valid
  const uint8_t scope[MAX_AFFECTED_BITS];  // the index of the bits the reader affects
};

// src/state_table.h
typedef struct {
  const uint8_t care[WORDS];   // bits that must match, ie aren't X. 0 = don't care / are X
  const uint8_t expect[WORDS]; // value those bits must have
  const size_t next;
} Transition;

typedef struct {
  pid_t pid;
  const State *state;
  const Transition transitions[MAX_TRANSITIONS];
  const int transition_count;
} StateEntry;
```
- **ReaderEntry**: One row of the reader table, with:
  - **pid**: The reader's process ID, filled in by the parent after `fork`. Used to identify the sender of a signal.
  - **reader**: Pointer to the actor's `init` / `run` / `exit` table.
  - **scope_len**: How many entries of `scope` are valid.
  - **scope**: The input bit indices this reader is allowed to write.

- **Transition**: One row of a state's transition list, with:
  - **care**: Which input bits must match; `0` means don't care (`X`).
  - **expect**: The value those bits must have.
  - **next**: Index into `state_table` of the next state. Equal to the current index means "stay".

- **StateEntry**: One row of the state table, with:
  - **pid**: The state actor's process ID, filled in by the parent after `fork`.
  - **state**: Pointer to the actor's `init` / `run` / `exit` table.
  - **transitions**: The transition rows for this state, evaluated in order.
  - **transition_count**: How many of `transitions` are valid.

#### Tables
```c
// src/reader_table.c
struct ReaderEntry reader_table[NO_OF_READERS] = {
    {.reader = &mock, .scope_len = 2, .scope = {0, 1}},
};

// src/state_table.c
StateEntry state_table[NO_OF_STATES] = {
    {
        .state = &state_stationary,
        .transition_count = 2,
        .transitions =
            {
                {.care = {0xFF}, .expect = {0x40}, .next = 1},
                {.care = {0xFF}, .expect = {0x80}, .next = 0},
            },
    },

    {
        .state = &state_rotate,
        .transition_count = 2,
        .transitions =
            {
                {.care = {0xFF}, .expect = {0x80}, .next = 0},
                {.care = {0xFF}, .expect = {0x40}, .next = 1},
            },
    },
};
```
- Both tables are static data using designated initialisers. `pid` is left zero at compile time and assigned by the parent after forking.
- Index `0` of `state_table` is the stationary state and index `1` is the rotatory state. It is also the value `current_state` starts at.
- The mock reader's `scope = {0, 1}` declares that it owns input bits 0 and 1.
- With `care = 0xFF` every input bit is significant, so each row is an exact match on the input mask. A partial `care` mask is how an `X` is expressed in any position.
- The row fields are `const`, so writing to a transition at runtime is rejected by the compiler.

---

### Building and Running

```bash
make        # produces ./stratos
make clean  # removes ./stratos and build/
```

The parent has no input of its own, so at least one reader must be running. To exercise the state machine with the mock reader:

```bash
make
mkfifo /tmp/stratos
./stratos
```

and from another terminal:

```bash
echo 7 > /tmp/stratos   # digit >= 5 -> input bit 1 -> Rotatory
echo 0 > /tmp/stratos   # digit <  5 -> input bit 0 -> Stationary
```

Expected output, one line per second:

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

---

### To-do

**Race between delivery order and intent order.**
Signals from a single reader arrive in the order they were sent, and the parent's handler is not re-entered while it runs (`SIGUSR1` is blocked for the duration of its own handler, since `SA_NODEFER` is not set). What is *not* preserved is the order of **intent**: if two readers report changes that were observed at different instances, the two messages are delivered in the order they report, not the order they observe.

This is compounded by signal coalescing: `SIGUSR1` is a *standard* signal, so pending instances are not queued. If a reader signals twice while the parent is inside its handler, only one instance remains pending and the other payload is discarded — an input change can be lost outright, not merely reordered. 

Other outstanding work:

- Port the SPI reader from `flight_plan.c` to the `Reader` interface; `mock.c` is only a stand-in.
- Add graceful shutdown: children are never reaped or signalled on exit, and `main()` ends in `while (1) pause();`.
- Clean up on error: a failed `fork()` returns without terminating children that were already forked.
- Call `State.exit` on a transition; the interface declares it but nothing invokes it.
- Make the input mask wider than one word (`MAX_INPUT_FIELDS` currently yields an 8-bit mask), and decide what a reader declaring a `scope` index beyond `WORDS * 8` should do — today it is silently ignored.
- Split the signal path per reader so that `running` is not shared between state and reader actors; only state actors install a `SIGTSTP` handler today.
- Replace `inline` definitions in `.c` files with `static inline` in the headers, and drop the now-unused includes (`sys/stat.h`, `fcntl.h` in `main.c`; `fcntl.h` in `reader_table.h`).
