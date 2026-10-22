This document explains the working of the flight plan v2.0 for the Gimbal project. This flight plan is structured in according to an actor model. An Actor is an entity that interacts with other actors purely through asynchronous events (like signals). In our program, an actor is materialized through a process that is forked from an initial process.

### Code Principles
---
#### Types of Actors
---
The three types of actors in our system are the Parent, States and Readers. Readers are actors that concurrently read multiple inputs across the satellite and report changes to the parent. States are actors that represent various modes in the state table and Parent is the Actor that controls all the States.

Each actor type is described by an interface of three function pointers, so that a concrete actor is a plain `const` object in a table rather than a special case in the parent:

```c
struct Reader { int (*init)(void); void (*run)(void); void (*exit)(void); };
struct State  { int (*init)(void); void (*run)(void); void (*exit)(void); };
```

Adding a reader or a state therefore means adding a file and a table entry — the parent's logic does not change.

##### Parent
This process controls all the other processes. Communication between other processes is strictly through signals. The reader actors signal the parent process a change in the input mask, along with the modifications to be done to the input mask. The parent process then updates the input mask and uses it to decide the next state (based on a state table).

The parent is `src/main.c`. It forks every state actor and every reader actor, records their PIDs in the tables, installs the `SIGUSR1` handler, and then blocks in `pause()` — all decision making happens inside the signal handler.

##### Readers
A reader actor is a process that reads a specific input. The reader sends signals to the parent whenever there is a change in the input (based on certain thresholds). In the Gimbal project we have only a single reader, which checks the input from the antenna using the SPI communication protocol.

A reader declares, in advance, which input bits it is allowed to affect. It does this with a `scope[]` array of input bit indices, of which only the first `scope_len` entries are valid. This is what lets the parent map a 32-bit signal onto an arbitrarily large input mask — the reader sends *addresses* (through its `scope[]`) and *values*, not the whole mask.

> The SPI reader has not yet been ported to this interface. `src/readers/mock.c` currently stands in for it: it reads characters from a FIFO at `/tmp/stratos` and signals the parent, so that the state machine can be exercised without hardware.

##### States
A state actor represents a mode in the flight plan. A mode represents the set of instructions that the satellite must perform under certain conditions. In the Gimbal project we have two states, which represent the stationary and rotatory modes. The modes are described as follows:
- Stationary mode: This mode indicates that the satellite must have a fixed orientation.
- Rotatory mode: This mode indicates that the satellite must have a constant angular velocity.

Only one state actor runs at a time. Every state actor pauses itself with `raise(SIGSTOP)` immediately after `init()`. The parent starts the initial state with `SIGCONT`, and switches states by sending `SIGSTOP` to the outgoing actor and `SIGCONT` to the incoming one.

#### State Table
---
The decision to switch to the next mode is taken by the parent based on the state table. The state table consists of multiple rows containing the current state, inputs (represented through an input bitmask) and the next state.

The state table is declared as static data in `src/state_table.c`. Each row is:

```c
typedef struct {
  const uint8_t care[WORDS];   // bits that must match, ie aren't X. 0 = don't care / are X
  const uint8_t expect[WORDS]; // value those bits must have
  const size_t next;
} Transition;
```

Transitions are evaluated **in order** and the first match wins. If the matched row's `next` equals the current state, the parent leaves the processes alone — that is how a self-transition ("stay") is expressed.

##### Input Bitmask
The input bitmask consists of an array of cells. Each cell can either be a 0, 1 or X (Don't care). This array is used by the parent to switch to the next state. For example, let a machine be in state A, which will switch to state B if its input mask is of the form 1X0X. Now, if our current inputs were represented by 1100, 1101, 1001 or 1000, then we would switch to state B.

Previously this was implemented with two binary bitmasks named `bitmask1` and `bitmask0`, and an equality test using the formula `(AB + A'C')'`, which read awkwardly and returned an inverted result. That pair has been replaced by `care` and `expect`:

- **`expect`** holds the value the cared bits must have.
- **`care`** holds a `1` where the cell is `0` or `1` (must match), and a `0` where the cell is `X` (don't care). There is no longer a separate `X` symbol — "don't care" is just a cleared bit in `care`.

The equality test is now a single mask-and-compare per word (see [Input Mask Comparison](#input-mask-comparison)).

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
Mode switching occurs when a reader sends a signal to the parent (along with the changes in the inputs). The signal API of the Linux Kernel allows us to send a maximum of 32 bits of information. If we send the input bitmask directly through this API, then we would have to limit our input bitmask to 16 bits. Since we anticipate that we will require more than 16 bits for the input bitmask, an address based mechanism is used. In this mechanism we divide the 32 bits into two halves: an address half (which bits are being updated) and a data half (their new values):

```
                        ┌──────────────────────────┐
                        │     32-bit sival_int     │
                        └────────────┬─────────────┘
                 ┌───────────────────┴───────────────────┐
       ┌─────────▼──────────┐                  ┌─────────▼──────────┐
       │   care (16 bits)   │                  │  change (16 bits)  │
       │  which scope[]     │                  │  new value for     │
       │  entries to update │                  │  each scope[]      │
       └─────────┬──────────┘                  └─────────┬──────────┘
                 │                                       │
           bit 0x8000 >> j                         bit 0x8000 >> j
                 │                                       │
                 └───────────────► scope[j] ◄────────────┘
                                     │
                                     ▼
                       inputs[scope[j] / 8], mask 0x80 >> (scope[j] % 8)
```

The j-th bit from the **MSB** of `care` and the j-th bit from the **MSB** of `change` both refer to `scope[j]`, so a reader updates only the bits it owns and the parent writes them into the input mask at the right place. `MAX_AFFECTED_BITS` is 16 because the payload is split in half — that is the ceiling on how many input bits a single reader may declare, and it is a property of the transport rather than a tunable.

As a concrete example, the mock reader declares `scope_len = 2`, `scope = {0, 1}`, and sends:

| Payload      | `care`   | `change` | Resulting input mask |
| ------------ | -------- | -------- | -------------------- |
| `0xFFFF8000` | `0xFFFF` | `0x8000` | `0x80` (bit 0 set)   |
| `0xFFFF4000` | `0xFFFF` | `0x4000` | `0x40` (bit 1 set)   |
| `0xFFFFC000` | `0xFFFF` | `0xC000` | `0xC0` (both set)    |

### Code Documentation

---

The implementation has been split into modules under `src/`:

```
src/
├── main.c              Parent actor: forks children, decodes signals, switches state
├── reader_table.{c,h}  Reader registry + the scope[] declaration
├── state_table.{c,h}   State table + the Transition encoding
├── utils.{c,h}         signals_install(), compare_input_mask()
├── readers/
│   ├── reader.h        Reader interface
│   └── mock.c          Mock reader (FIFO at /tmp/stratos)
└── states/
    ├── state.h         State interface
    ├── stationary.c    Stationary mode
    └── rotate.c        Rotate mode
```

---

#### **Includes**

System headers used across the modules:

```c
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
```
- **errno.h**: Used by the mock reader to distinguish `EINTR` (an interrupted `read`) from a genuine read error.
- **fcntl.h**: Provides file control options, such as manipulating file descriptors and the `open` flags used by the mock reader.
- **signal.h**: For signal handling in Unix-based systems — `sigaction`, `sigqueue`, `sigemptyset`, `SIGSTOP`, `SIGCONT`, `SIGUSR1`, `SIGTSTP`.
- **stddef.h**: For `size_t`, used by `scope_len` and the table index types.
- **stdint.h**: For fixed-width integer types such as `uint8_t` and the 32-bit signal payload.
- **stdio.h**: Standard input/output functions (e.g., `printf`), used by the state actors and by `perror` on error paths.
- **stdlib.h**: For general utility functions such as `exit` in the mock reader's error path.
- **sys/stat.h**: Included by `main.c`, but nothing from it is currently used — the FIFO is created externally (`mkfifo`), not by the program. Left over from the SPI setup; see [Known Limitations](#known-limitations).
- **sys/types.h**: For `pid_t`, the process ID type stored in both tables.
- **unistd.h**: Provides access to the POSIX operating system API, such as `fork`, `read`, `write`, `sleep`, `close` and `getppid`.

> `linux/spi/spidev.h`, `sys/ioctl.h` and `sys/wait.h` are **no longer included**: the SPI setup and the `wait()`-based child handling were part of the old monolith (`flight_plan.c`), which this branch replaces.

Project headers follow the module layout: `reader_table.h` and `state_table.h` are included by the parent, `readers/reader.h` by the reader implementations, and `states/state.h` by the state implementations.

---

#### **Macro Definitions**
```c
// src/reader_table.h
#define MAX_AFFECTED_BITS 16
#define NO_OF_READERS 1

// src/state_table.h
#define MAX_INPUT_FIELDS 2
#define WORDS ((MAX_INPUT_FIELDS + 7) / 8)
#define NO_OF_STATES 2
#define MAX_TRANSITIONS 2
```
- **MAX_AFFECTED_BITS**: The maximum number of input bits a single reader may declare in its `scope[]`. Set to 16 because the 32-bit signal payload is split into two 16-bit halves. Marked `DO NOT CHANGE` at the definition — it follows from the transport, not from a design preference.
- **NO_OF_READERS**: The number of reader actors to fork, and the size of `reader_table`. Currently 1.
- **MAX_INPUT_FIELDS**: The number of input cells the state table is dimensioned for. Currently 2.
- **WORDS**: The number of bytes needed to hold `MAX_INPUT_FIELDS` cells, computed as `(MAX_INPUT_FIELDS + 7) / 8`. With the current value this is 1, so the input mask is a single byte.
- **NO_OF_STATES**: The number of state actors to fork, and the size of `state_table`. Currently 2, for stationary and rotatory.
- **MAX_TRANSITIONS**: The maximum number of transition rows a single state may have (per state, not in total).

> `NO_OF_GROUPS`, `SIG_P2C` and `SPI_PATH` from the previous version no longer exist. `NO_OF_GROUPS` is subsumed by `WORDS`; `SIG_P2C` is gone because the parent no longer sends a stop command to children with a payload — it uses `SIGSTOP`/`SIGCONT` directly.

---

#### **Global Variables**
```c
// src/main.c
static volatile sig_atomic_t running = 1;
static uint8_t inputs[WORDS] = {0};
static int current_state = 0;

// src/readers/mock.c
static int fd = -1;
```
- **running**: The stop/continue flag for a child actor. It is `volatile sig_atomic_t` because it is written from a signal handler.
- **inputs**: The input mask itself, `WORDS` bytes. Only the parent writes to it, and only inside its `SIGUSR1` handler.
- **current_state**: Index into `state_table` of the state that is currently allowed to run. Initialised to `0`, the stationary state.
- **fd**: The mock reader's file descriptor for `/tmp/stratos`. Static to the reader implementation, since it is per-process state.

---

#### **Function Definitions**

1. **Signal Installation**
```c
int signals_install(int signo, void (*handler)(int)) { ... }
```
- Installs `handler` for `signo` with an empty mask and no flags, using `sigaction`.
- Returns `0` on success and `-1` if `sigemptyset` or `sigaction` fails.
- Exists so the state actors register their `SIGTSTP` handler without repeating the `struct sigaction` boilerplate.

2. **Input Mask Comparison**
```c
int compare_input_mask(uint8_t *inputs, Transition transition) { ... }
```
- Returns `1` if the input mask satisfies the transition's `care` / `expect` pair, otherwise `0`.
- Detailed below in [Input Mask Comparison](#input-mask-comparison).

3. **Mock Reader**
```c
int mock_init(void);
void mock_run(void);
void mock_exit(void);
```
- **mock_init**: Opens `/tmp/stratos` with `O_RDWR` and reports failure with `perror`.
- **mock_run**: Reads one character. Digits below `5` send `0xFFFF8000` to the parent (input bit 0), digits `5` and above send `0xFFFF4000` (input bit 1). A newline or `EINTR` is ignored, and a real read error exits the process.
- **mock_exit**: Closes the FIFO file descriptor.

4. **State Actors**
```c
int stationary_init(void);  void stationary_run(void);  void stationary_exit(void);
int rotate_init(void);      void rotate_run(void);      void rotate_exit(void);
```
- Each `init` announces the mode, each `run` performs one iteration of the mode (currently a `sleep(1)` loop body), and each `exit` announces the mode change.
- `run` is called in a tight `while (1)` loop by the child, and is only reached while the process is not stopped — so the loop body is where a mode's real work belongs.

> `kalmanfilterR`, `kalmanfilterS`, `pid` and `pwm` were placeholders in the old monolith and are **not** present in this tree. They will return alongside the SPI reader.

---

#### **State Machine Structures**
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

// src/reader_table.h
struct ReaderEntry {
  pid_t pid;
  const Reader *reader;
  const size_t scope_len;                  // how many entries of scope are valid
  const uint8_t scope[MAX_AFFECTED_BITS];  // the index of the bits the reader affects
};

// src/state_table.h
typedef struct {
  const uint8_t care[WORDS];
  const uint8_t expect[WORDS];
  const size_t next;
} Transition;

typedef struct {
  pid_t pid;
  const State *state;
  const Transition transitions[MAX_TRANSITIONS];
  const int transition_count;
} StateEntry;
```
- **Reader** / **State**: The actor interfaces. A concrete actor is a `const` object holding three function pointers.
- **ReaderEntry**: One row of the reader table, with:
  - **pid**: The reader's process ID, filled in by the parent after `fork`. Used to identify the sender of a `SIGUSR1`.
  - **reader**: Pointer to the actor's `init`/`run`/`exit` table.
  - **scope_len**: How many entries of `scope` are valid.
  - **scope**: The input bit indices this reader is allowed to write.

- **Transition**: One row of a state's transition list, with:
  - **care**: Which input bits must match; `0` means don't care (`X`).
  - **expect**: The value those bits must have.
  - **next**: Index into `state_table` of the next state.

- **StateEntry**: One row of the state table, with:
  - **pid**: The state actor's process ID, filled in by the parent after `fork`.
  - **state**: Pointer to the actor's `init`/`run`/`exit` table.
  - **transitions**: The transition rows for this state, evaluated in order.
  - **transition_count**: How many of `transitions` are valid.

---

#### **Global Arrays for Input and States**

1. **Reader Table**
```c
// src/reader_table.c
extern const Reader mock;

struct ReaderEntry reader_table[NO_OF_READERS] = {
    {.reader = &mock, .scope_len = 2, .scope = {0, 1}},
};
```
- Holds one row per reader actor. `pid` is left zero here and is assigned by the parent after forking.
- `scope = {0, 1}` declares that the mock reader owns input bits 0 and 1.

2. **State Table**
```c
// src/state_table.c
extern const State state_stationary;
extern const State state_rotate;

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
- Index `0` is the stationary state, index `1` is the rotatory state.
- With `care = 0xFF` every input bit is significant, so each row is an exact match on the input mask. Rows may set `care` to a partial mask to express an `X` in any position.

---

#### **Row and Mode Initialization**

The previous version built the state table at runtime: `init_modes()` called `add_row(...)` for each transition, which mutated `stat_rows` / `rot_rows` through a `head` pointer and a row index. That had two drawbacks — the flight plan was only visible by tracing the calls, and the row/group indexing was easy to get wrong (the earlier `static int index` scheme was replaced by explicit `row_no` / `group_no` for exactly this reason).

Both `add_row()` and `init_modes()` are now gone. The tables are **static data** using designated initialisers, as shown in [Global Arrays for Input and States](#global-arrays-for-input-and-states). This means:

- The flight plan can be read directly as a table, in one place.
- The ordering of rows is explicit rather than implied by call order.
- `NO_OF_READERS` / `NO_OF_STATES` and the initialisers sit next to each other, so a mismatch is a compile-time error rather than a silent overwrite.

The tables are `const` where possible (`reader`, `state`, `care`, `expect`, `next`, `scope`), so accidentally writing to a transition at runtime is rejected by the compiler.

---

#### **Input Mask Comparison**
```c
inline int compare_input_mask(uint8_t *inputs, Transition transition) {
  for (size_t i = 0; i < WORDS; i++) {
    if ((inputs[i] & transition.care[i]) !=
        (transition.expect[i] & transition.care[i])) {
      return 0;
    }
  }
  return 1;
}
```
- Compares the input mask against the provided row's `care` / `expect` pair, word by word, and returns `1` if they match, otherwise `0`.
- Only the bits set in `care` are compared; bits cleared in `care` are don't-care (`X`) and cannot cause a mismatch.
- This replaces the previous test, `~((inputmask[i] & bitmask1[i]) | (~inputmask[i] & ~bitmask0[i]))`, which required reconstructing the `X` positions from two masks and returned `0` for a match — an inversion that made call sites read backwards.

---

#### **Signal Handlers**

1. **Stop Signal Handler**
```c
static void child_sigtstp_handler(int signo) {
  (void)signo;
  running = 0;
}
```
- Installed by each state actor with `signals_install(SIGTSTP, ...)` before entering its run loop.
- Clears `running`, which the run loop observes after the current `run()` returns, at which point the child re-raises `SIGSTOP` and parks itself until the parent continues it.
- Note that the parent actually parks a state with `SIGSTOP` directly when switching; `SIGTSTP` is the cooperative path a child can also use on itself.

2. **User Signal Handler**
```c
void parent_sigusr_handler(int signo, siginfo_t *info, void *context) {
  uint32_t sigv = (uint32_t)info->si_value.sival_int;
  uint16_t care   = (uint16_t)(sigv >> 16);
  uint16_t change = (uint16_t)sigv;
  ...
}
```
- Installed by the parent on `SIGUSR1` with `SA_SIGINFO`, so the payload and the sending PID are both available.
- **Identifies the sender** by matching `info->si_pid` against `reader_table[i].pid`. Signals from processes not in the table are ignored.
- **Decodes the payload** by splitting it into the `care` and `change` halves described in [Mode switching](#mode-switching).
- **Updates the input mask**: for each `j` in `0 .. scope_len - 1`, if the `care` bit is set, the `change` bit is written to `inputs[]` at the position given by `scope[j]`. Bits whose `care` bit is clear are left untouched, which is what allows a reader to report only part of its scope.
- **Evaluates the current state's transitions in order**, using `compare_input_mask`, and takes the first match. If the matched row's `next` differs from `current_state`, it sends `SIGSTOP` to the outgoing state actor, updates `current_state`, and sends `SIGCONT` to the incoming one. If `next` equals the current state, nothing is signalled.

---

### Building and Running

```bash
make        # produces ./stratos
make clean  # removes ./stratos and build/
```

The build uses `gcc -Wall -Wextra -Werror`, so warnings fail the build. `build/` and the `stratos` binary are git-ignored.

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

> State actors inherit the parent's stdout, so when redirecting to a file the output is block-buffered and may not appear until exit. Use `stdbuf -o0 ./stratos` when redirecting.

---

### Known Limitations

- **No graceful shutdown.** Child processes are never reaped or signalled on exit; `main()` ends in `while (1) pause();`.
- **No error cleanup.** A failed `fork()` returns immediately without terminating children that were already forked.
- **No SPI reader.** `src/readers/mock.c` is a stand-in; the SPI reader from `flight_plan.c` has not been ported to the `Reader` interface.
- **`State.exit` is never called.** The interface declares it, but nothing invokes it on a transition.
- **`running` is shared.** The same flag drives the stop/continue logic in state and reader actors, but only state actors install a `SIGTSTP` handler.
- **`MAX_INPUT_FIELDS = 2`** yields a single 8-bit word, so multi-word input handling is not yet exercised.
- **`signals_install` and `compare_input_mask` are `inline` in a `.c` file.** This works under GCC's default C mode but is fragile; `static inline` in the header would be more portable.
- **A few includes are unused.** `sys/stat.h` and `fcntl.h` in `main.c`, and `fcntl.h` in `reader_table.h`, are left over from the SPI/FIFO setup and can be dropped.
