# ResourceGuard

### A Banker's Algorithm deadlock-avoidance simulator, with a GUI and a testable C engine

[![Language](https://img.shields.io/badge/language-C11-blue.svg)](https://en.cppreference.com/w/c)
[![GUI](https://img.shields.io/badge/GUI-GTK4-4A86CF.svg)](https://www.gtk.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-FCC624.svg)](https://www.kernel.org/)
[![Status](https://img.shields.io/badge/status-in%20development-orange.svg)](#project-status)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](#license)

**ResourceGuard** lets you enter a system state (processes, resource types, `Available`, `Allocation`, `Max`),
submit a resource request, and see *why* the Banker's Algorithm grants, delays, or denies it, one step at a time.
For an unsafe request, its **Why-not analysis** exhaustively explores the feasible request vectors and reports the
safe region, the nearest safe alternative, and the component-wise maximum safe request when one exists.

It is built as two separate layers:

1. **`engine/`** : a pure-C library with no GTK dependency. It implements validation, the safety algorithm, the
   request algorithm, rollback, and a step-by-step **trace**. It is unit-tested on its own.
2. **`gui/`** : a GTK4 front end that only *displays* what the engine returns. It never decides anything itself.

> **Screenshots / demo GIF:** _add after the dashboard is wired to the engine (see roadmap, Phase 4)._

---

## Project status

| Area | State |
|---|---|
| Engine: validation, safety algorithm, request + rollback, release, trace (`engine/`) | **Done** |
| Why-not request-space analysis (`engine/`) | **Done**: bounded exhaustive search, safe-region visitor, nearest alternative |
| Scenario file loader | **Done** |
| Unit + property tests (`make test`) | **Done**: 24,092 checks, 0 failures in this revision |
| Terminal front end (`banker_cli`) | **Done**: includes `why` command |
| GTK4 GUI (`gui/`) | Next: not started in this repository |
| pthread concurrency demonstration | Planned next advanced feature |
| Step-by-step visualiser, history, advanced extensions | Planned |

Everything the GUI will show must come from the engine. Nothing in this repository is hard-coded for display.


---

## Quick start

```bash
make            # builds banker_cli and test_banker (needs only gcc and make)
make test       # unit + property tests
make demo       # interactive session on the classic example
```

```text
> why 4 3 3 0
REQUEST DENIED: no safe sequence exists after granting this vector.
  step 1: Work =  0  0  2  -> no unfinished process fits: STUCK
Safe request region: 24 / 32 vectors; 8 unsafe.
Per-resource maximum among safe vectors: 3 2 1
Resource 1 boundary: feasible requests can ask up to 3, but no safe request can ask for more than 2.
Largest component-wise safe request: 3 2 1
Nearest safe alternative (L1 distance 1): 3 2 0
```

## Table of contents

- [Why this project](#why-this-project)
- [Problem statement](#problem-statement)
- [Objectives](#objectives)
- [Core concepts](#core-concepts)
- [The algorithms](#the-algorithms)
- [Engine design](#engine-design)
- [Input validation rules](#input-validation-rules)
- [Features](#features)
- [Repository layout](#repository-layout)
- [Requirements and build](#requirements-and-build)
- [Demonstration dataset](#demonstration-dataset)
- [Test vectors](#test-vectors)
- [Testing strategy](#testing-strategy)
- [Advanced extensions](#advanced-extensions)
- [Roadmap](#roadmap)
- [Limitations of the Banker's Algorithm](#limitations-of-the-bankers-algorithm)
- [Viva preparation](#viva-preparation)
- [Contributing](#contributing)
- [License](#license)
- [References](#references)

---

## Why this project

Operating systems share a finite set of resources among competing processes. Granting every request immediately
can push the system into a state from which deadlock is unavoidable. The Banker's Algorithm (Dijkstra, 1965; Habermann,
1969) avoids that by granting a request only if the resulting state is still **safe**.

ResourceGuard turns that rule into something you can *see*: the `Work` vector changing, processes finishing and
releasing resources, and the safe sequence forming, or the exact point where it gets stuck.

**Link to processor architecture.** The safety check is a tight loop over small integer vectors, so it is a good case
study for cost analysis and concurrency. See [Advanced extensions](#advanced-extensions) for the complexity analysis,
benchmark, and the multi-threaded "real processes" mode that exercises locking and atomicity.

This is a simulator. It does not manage real operating-system resources.

## Problem statement

> Create a program that accepts a process resource request and determines whether granting it would keep the system in
> a safe state, using the Banker's Algorithm.

## Objectives

- Implement the safety algorithm and the resource-request algorithm.
- Validate input and requests before any algorithm runs, with a distinct error for each failure.
- Apply a request tentatively and roll back completely if the result is unsafe.
- Record a **trace** of every step so the GUI (and tests) can replay the decision.
- Keep the engine independent of the GUI so it can be tested, benchmarked, and reused (CLI, tests, GUI).
- Provide a reproducible demonstration with documented expected outputs.

---

## Core concepts

For `n` processes and `m` resource types:

| Structure | Meaning |
|---|---|
| `Total[m]` | Total instances of each resource type that exist in the system |
| `Available[m]` | Instances currently free |
| `Max[n][m]` | Maximum a process may ever hold, declared in advance |
| `Allocation[n][m]` | Currently held by each process |
| `Need[n][m]` | Remaining possible demand, **derived**: `Need = Max - Allocation` |

**Invariants that must always hold** (the engine checks these on load and after every commit):

```text
0 <= Allocation[i][j] <= Max[i][j] <= Total[j]
Available[j] + sum_i Allocation[i][j] == Total[j]
```

`Need` should be computed from `Max` and `Allocation`, not stored and updated separately. A second copy of the same
information is a way for the state to become inconsistent.

**Terminology**

- **Safe state:** at least one order exists in which every process can obtain its remaining `Need`, finish, and release
  everything it holds.
- **Safe sequence:** one such order. There is usually more than one.
- **Unsafe state:** no safe sequence exists. This does **not** mean deadlock has already happened; it means deadlock
  can no longer be ruled out.
- **Deadlock:** a set of processes each waiting for a resource held by another in the set.
- **Rollback:** restoring the exact previous state after a tentative grant fails the safety check.

---

## The algorithms

### Safety algorithm

1. `Work = Available`; `Finish[i] = false` for all `i`.
2. Find an unfinished `Pi` with `Need[i] <= Work` (component-wise).
3. `Work = Work + Allocation[i]`; `Finish[i] = true`; append `Pi` to the sequence.
4. Repeat 2-3 until no such process exists.
5. Safe if and only if every `Finish[i]` is true.

**Selection policy (specify it, or tests will be fragile).** There are often many valid safe sequences, and the one
returned depends on how step 2 picks a process. ResourceGuard uses **lowest index first, rescanning from `P0` after
every pick**. Tests must therefore not hard-code a sequence from a textbook; they should either use this policy's
output or *verify that the returned sequence is valid* (see [Testing strategy](#testing-strategy)).

**Complexity.** Each pick scans up to `n` processes at `O(m)` per comparison, and there are up to `n` picks:
`O(m * n^2)` time, `O(n + m)` extra space.

### Resource-request algorithm

For a request vector `Request` from process `Pi`, apply the checks **in this order**:

| # | Check | On failure |
|---|---|---|
| 0 | `i` is a valid process index; every `Request[j] >= 0` | `REQ_ERR_BAD_INPUT` |
| 1 | `Request <= Need[i]` | `REQ_ERR_EXCEEDS_NEED` (process broke its declared maximum) |
| 2 | `Request <= Available` | `REQ_WAIT_UNAVAILABLE` (process must wait; not an error) |
| 3 | Tentatively grant, run safety algorithm | unsafe → `REQ_DENIED_UNSAFE`, roll back |
| 4 | Safe | `REQ_GRANTED`, commit |

The order matters. A request that exceeds `Need` is invalid even if it also exceeds `Available`. A request that
exceeds `Available` never reaches the safety algorithm. An all-zero request is defined as `REQ_GRANTED` with no state
change (document this choice and test it).

**Tentative grant and rollback.** Do not "undo" by reversing the arithmetic. Copy the state, modify the copy, test the
copy, and commit only if safe:

```c
BankerState trial = *state;          // value copy (fixed-size arrays) or a deep copy helper
apply_request(&trial, pid, req);
if (is_safe(&trial, /*trace*/ NULL, /*seq*/ NULL)) *state = trial;   // commit
// otherwise: nothing to undo, *state was never touched
```

This makes rollback correct by construction and easy to test.



### "Why not?" request-space analysis

When a request passes the Need and Available checks but is denied as unsafe, `banker_why_not()` explains the
failure instead of returning only a denial label.

For the requesting process, the engine enumerates every integer request vector in the Cartesian product:

```text
0 <= Request[j] <= min(Need[pid][j], Available[j])
```

Each candidate is tentatively applied to a copy of the state and passed through the same safety algorithm. The
real state is never modified. A visitor callback receives every candidate vector and its safe/unsafe result; a GUI
can use this callback to draw a safe-region map without reimplementing Banker's Algorithm.

The report includes:

- Number of feasible candidate vectors, safe vectors, and unsafe vectors.
- Per-resource maxima among all safe vectors.
- Whether those coordinate-wise maxima combine into one safe vector (if yes, it is a component-wise largest safe request).
- The nearest safe alternative to the rejected vector, using Manhattan/L1 distance. Ties prefer the alternative that grants
  more total units; any remaining tie is resolved lexicographically for deterministic output.
- The original denied request's safety trace, including the final stuck `Work` vector.

**Bounded search:** exhaustive enumeration can grow exponentially with the number of resource types and resource counts.
The current engine refuses searches whose candidate space exceeds `BANKER_WHY_MAX_CANDIDATES` (100,000). It returns
`WHY_ERR_SEARCH_SPACE_TOO_LARGE` before visiting any candidates rather than silently returning partial results.

**Demo result:** for `P4: 3 3 0`, the feasible search space is 32 vectors; 24 are safe and 8 are unsafe. The maximum
safe vector is `[3, 2, 1]`, which is itself safe. The nearest safe alternative by L1 distance is `[3, 2, 0]`—it removes
one unit of B from the rejected request. The safe-region boundary also shows that no safe vector can request 3 units
of B in this state.

---

## Engine design

The engine knows nothing about GTK. The GUI calls it and renders the result.

```c
// engine/banker.h  (implemented; see the header for full comments)
BankerStatus  banker_validate(const BankerState *s, int *bad_proc, int *bad_res);
void          banker_need(const BankerState *s, int need[][BANKER_MAX_RES]);
bool          banker_is_safe(const BankerState *s, int seq[], int *seq_len, Trace *trace);
RequestResult banker_request(BankerState *s, int pid, const int req[], Trace *trace);
bool          banker_release(BankerState *s, int pid, const int rel[]);
bool          banker_sequence_is_valid(const BankerState *s, const int seq[], int len);  // test oracle
WhyNotStatus  banker_why_not(const BankerState *s, int pid, const int rejected[], WhyNotReport *report,
                             BankerWhyVisitor visitor, void *user_data);
bool          banker_load_scenario(const char *path, BankerState *out, char *err, int err_len);
```

**Why a trace.** The step-by-step view in the GUI should replay data produced by the engine, not re-implement the
algorithm. The "why is it unsafe?" panel is simply the final `picked == -1` step plus the unfinished processes and the
`Work` vector they could not satisfy.

**Static vs dynamic sizes.** The fixed-size structs above keep the code simple and make value-copy rollback trivial.
If the GUI needs larger systems, switch to heap allocation with `banker_clone()` / `banker_free()` and keep the same
semantics.

---

## Input validation rules

Everything below is checked by `banker_validate()` before any algorithm runs. Each rule has its own error code and
its own test.

| Rule | Why |
|---|---|
| `1 <= n <= MAX_PROCS`, `1 <= m <= MAX_RES` | Bounds the arrays |
| No negative value in `Total`, `Available`, `Max`, `Allocation`, or any request | Meaningless input |
| `Allocation[i][j] <= Max[i][j]` | `Need` would be negative |
| `Max[i][j] <= Total[j]` | A process that may need more than exists can never be guaranteed to finish |
| `Available[j] + sum_i Allocation[i][j] == Total[j]` | Resources are neither created nor lost |
| Process index in `[0, n)` | Prevents out-of-bounds access |
| Integer overflow guard on sums | Use `long long` or check bounds on load |

The GUI should also prevent bad input at the widget level (non-negative spin buttons) but the engine must not rely
on that.

---

### Scenario file format

Plain text, `#` starts a comment. `processes` and `resources` come first.

```text
processes 5
resources 3
total     10 5 7
available  3 3 2
allocation
0 1 0
...            # n rows of m numbers
max
7 5 3
...            # n rows of m numbers
```

The loader only parses. Call `banker_validate()` afterwards: the same file can be a valid state or a deliberate
test of an error (see `tests/scenarios/`).

## Features

### Implemented

- Validation of every invariant, with the offending process/resource reported
- Safety algorithm with a replayable step trace
- Request algorithm with ordered checks and exact rollback
- Release operation
- **Why-not analysis:** bounded exhaustive request-space search, safe/unsafe counts, per-resource safe boundary,
  nearest safe alternative, component-wise maximum check, and callback for map rendering
- Scenario files; terminal front end (`banker_cli`) with `show`, `safe`, `trace`, `request`, `why`, `release`, `reset`

### Planned

- **Dashboard:** `n`, `m`, `Available`, current safe/unsafe status, recent decisions
- **System editor:** editable `Total`, `Available`, `Max`, `Allocation`; live `Need`; validation messages
- **Safety analysis:** run the algorithm, show a safe sequence or the reason none exists
- **Request simulator:** choose process, enter vector, see the check-by-check outcome, commit or roll back
- **Step-by-step visualiser:** `Work`, `Finish`, current candidate, resources returning, sequence forming
- **Request history:** process, vector, outcome (`GRANTED`, `WAIT`, `INVALID`, `DENIED_UNSAFE`), timestamp
- **Scenario files:** load/save a system state as JSON or CSV so any test case is one click to reproduce
- **Release action:** a process releases some or all of its resources (see extensions)

---

## Repository layout

```text
resourceguard/
├── engine/
│   ├── banker.h / banker.c   # validation, safety, request, rollback, release, trace
│   └── scenario.c            # scenario file loader
├── cli/main.c                # terminal front end on the engine
├── tests/
│   ├── test_banker.c         # vectors T1-T14, brute-force cross-check, rollback properties
│   └── scenarios/            # demo.txt and deliberate invalid/unsafe states
├── gui/                      # (planned) GTK4 front end
├── legacy/                   # put the original terminal program here for reference
├── docs/                     # screenshots, report
├── .github/workflows/ci.yml
├── .gitignore  LICENSE  Makefile  README.md
```

Notes:

- Do **not** commit compiled binaries (the old `checker` executable). Use `.gitignore`.
- Avoid spaces in directory names (`Resource Request Safety Checker/` will cause trouble in shells and Makefiles).
- Move the original terminal program to `legacy/` so the repository has one clear entry point.

`.gitignore`:

```text
*.o
resourceguard
banker_cli
test_banker
checker
build/
```

---

## Requirements and build

**Engine, tests and CLI:** `gcc` (or Clang) and `make`. Nothing else.

```bash
make            # build
make test       # run tests (from the project root; they read tests/scenarios/)
make sanitize   # same tests under AddressSanitizer + UBSan
make demo       # interactive CLI on the classic example
make clean
```

**GUI (when added):** GTK4 development files.

```bash
sudo dnf install gcc make pkgconf-pkg-config gtk4-devel      # Fedora
sudo apt install build-essential pkg-config libgtk-4-dev     # Debian / Ubuntu
sudo pacman -S base-devel gtk4                               # Arch
```

The Makefile is in the repository root. Its recipe lines use real TAB characters; if you copy a Makefile from
somewhere and `make` says "missing separator", that is why.

**GUI notes for later.** Embed the CSS with `glib-compile-resources` instead of loading a relative path, and use a
reverse-DNS application ID such as `io.github.<your-username>.ResourceGuard`.

---

## Demonstration dataset

The classic 5-process, 3-resource example (resource types `A`, `B`, `C`).

```text
Total      = 10 5 7
Available  =  3 3 2

Allocation        Max            Need (= Max - Allocation)
     A B C        A B C               A B C
P0   0 1 0        7 5 3          P0   7 4 3
P1   2 0 0        3 2 2          P1   1 2 2
P2   3 0 2        9 0 2          P2   6 0 0
P3   2 1 1        2 2 2          P3   0 1 1
P4   0 0 2        4 3 3          P4   4 3 1
```

Check: `Available + column sums of Allocation = (3+7, 3+2, 2+5) = (10, 5, 7) = Total`. The conservation invariant holds.

**Safe sequences.** This state has **16** valid safe sequences. Two examples:

```text
P1 -> P3 -> P4 -> P0 -> P2        (valid; common textbook answer)
P1 -> P3 -> P0 -> P2 -> P4        (what the lowest-index-first policy returns)
```

---

## Test vectors

All outcomes below are implemented in `tests/test_banker.c` and pass. They were also checked against an independent
brute-force enumeration of safe sequences. Each test starts from the **original** demonstration state unless stated.

| # | Scenario | Request | Expected result |
|---|---|---|---|
| T1 | Initial safety check | (none) | `SAFE`; policy sequence `P1 P3 P0 P2 P4` |
| T2 | Safe grant | `P1: 1 0 2` | `REQ_GRANTED`; `Available = 2 3 0`; state still safe (8 safe sequences) |
| T3 | Available but unsafe | `P4: 3 3 0` | `REQ_DENIED_UNSAFE`; `Work = 0 0 2`; no process can proceed; state restored exactly |
| T4 | Exceeds Available (after T2 committed) | `P4: 3 3 0` | `REQ_WAIT_UNAVAILABLE` (needs 3 A, only 2 free); safety algorithm **not** run |
| T5 | Exceeds Need | `P1: 1 3 0` | `REQ_ERR_EXCEEDS_NEED` (B: 3 > 2) |
| T6 | Check ordering | `P2: 7 0 0` | `REQ_ERR_EXCEEDS_NEED`, not `WAIT` (also exceeds Available, but Need is checked first) |
| T7 | Zero request | `P0: 0 0 0` | `REQ_GRANTED`, state unchanged |
| T8 | Bad process index | `P9: 0 0 0` | `REQ_ERR_BAD_INPUT` |
| T9 | Negative request | `P0: -1 0 0` | `REQ_ERR_BAD_INPUT` |
| T10 | Invalid state: Allocation > Max | set `Alloc[2][0] = 10` with `Max[2][0] = 9` | `BANKER_ERR_ALLOC_GT_MAX` (validation checks run in the order of the rules table, so this is reported before conservation) |
| T11 | Invalid state: Max > Total | set `Max[2][0] = 11` | `BANKER_ERR_MAX_GT_TOTAL` |
| T12 | Invalid state: conservation | set `Available = 4 3 2` with allocations unchanged | `BANKER_ERR_CONSERVATION` |
| T13 | Unsafe initial state | load: `Available = 0 0 2`, `Alloc[4] = 3 3 2`, other rows unchanged (this is the state T3 would have produced; conservation holds) | `BANKER_OK` from validate, then `UNSAFE`; stuck step lists all five unfinished processes |
| T14 | Rollback integrity | run T3, then T2 | T2 still `GRANTED` (state after T3 was identical to the original) |

> The expected output for T1 and T2 depends on the selection policy. If you change the policy, regenerate those
> expectations, or better, assert validity rather than an exact sequence (see below).

### T15 — Why-not request-space analysis

From the original demo state, analyse the unsafe request `P4: 3 3 0`.

| Metric | Expected |
|---|---|
| Candidate vectors (`0..min(Need, Available)` per resource) | 32 |
| Safe vectors | 24 |
| Unsafe vectors | 8 |
| Search bounds | `[3, 3, 1]` |
| Component-wise maximum over safe vectors | `[3, 2, 1]` |
| Is the combined component-wise maximum safe? | Yes |
| Nearest safe alternative by L1 distance | `[3, 2, 0]`, distance 1 |
| Final stuck Work after the rejected request | `[0, 0, 2]` |

The 8 unsafe vectors are exactly those that request 3 units of resource B. The nearest alternative removes one B.
The component-wise largest safe vector also adds one C, so it is `[3, 2, 1]`. These are different recommendations
because the nearest alternative minimises changes, while the component-wise maximum maximises each coordinate.

---

## Testing strategy

**Status:** the engine, request-space analysis, scenario files, and deterministic property tests are implemented.
The random test runs 5,000 seeded random systems, compares the safety engine against a brute-force permutation oracle,
and checks rollback with `memcmp`. This revision passes `make test` with 24,092 checks and passes a standalone
UndefinedBehaviorSanitizer run. AddressSanitizer could not reserve its shadow memory in the current build environment;
re-run `make sanitize` on a normal Fedora/Linux development host or in CI. The CI workflow is present but still needs
a green run on GitHub.

1. **Assert validity, not just equality.** Write a tiny independent checker `sequence_is_valid(state, seq)` that replays
   a returned sequence and confirms each process's `Need` fits the running `Work`. Then assert `banker_is_safe()`
   returns a sequence this checker accepts. This is robust to policy changes.
2. **Cross-check by brute force.** For small `n` (up to 7), enumerate all `n!` permutations and check that
   `banker_is_safe()` says "safe" exactly when at least one permutation is valid. Run this on thousands of random
   states generated from a fixed seed.
3. **Rollback property.** For every random request that returns `DENIED_UNSAFE`, assert the state is bit-for-bit equal
   to the state before the call (`memcmp`).
4. **Invariant property.** After every `GRANTED`, `banker_validate()` must still return `BANKER_OK`.
5. **Conservation property.** After a `GRANTED`, total resources are unchanged.
6. **Sanitizers.** Run the whole suite under UndefinedBehaviorSanitizer (`make ubsan`) and AddressSanitizer + UBSan (`make sanitize`).
7. **Scenario files.** Store each T-case as a JSON file in `tests/scenarios/` so the CLI, tests, and GUI load the same data.
8. **CI.** A GitHub Actions workflow that runs `make test` and `make sanitize` on every push.

Minimal CI:

```yaml
name: ci
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y build-essential pkg-config libgtk-4-dev
      - run: make test
      - run: make sanitize
```

---

## Advanced extensions

Pick two or three. Each is something a plain Banker's demo does not have, and each ties back to the course.

1. **Release and process lifecycle.** The base algorithm only grants. Real systems also release. Add
   `banker_release()` (a process returns some of what it holds), "process finishes" (returns everything and is removed),
   and "new process arrives" (declares its `Max`, rejected if `Max > Total`). Without these the simulator cannot show
   a system evolving over time.
2. **Multi-threaded mode (processor architecture + OS) — selected next extension.** Run each simulated process as a
   `pthread` that requests resources, performs simulated work, and releases them. Compare a correct implementation
   that holds a mutex across the complete check-and-commit transaction with an intentionally split check/commit
   demonstration. The latter should synchronize individual memory accesses but release the lock between checking and
   committing, so two threads can act on stale snapshots and demonstrate a time-of-check/time-of-use race without
   introducing an actual unsynchronized C data race (undefined behaviour). Show a live log and explain critical sections,
   mutual exclusion, atomicity, and the Banker's safety invariant. Compare a version with the lock removed to demonstrate the failure.
3. **Complexity and performance study.** Benchmark `banker_is_safe()` for growing `n` and `m`, plot time against
   `m * n^2`, and discuss cache behaviour: why contiguous `int` arrays and a row-major `Need` scan are fast, and what
   changes with 8-bit versus 32-bit elements or with a transposed layout. Optional: a bitset-style or SIMD comparison of
   `Need[i] <= Work`.
4. **Deadlock detection mode.** Add the detection algorithm (same shape as the safety check but using `Request`
   instead of `Need`) so the tool can show avoidance and detection side by side on the same state.
5. **Resource-allocation graph view.** Draw processes, resources, request and assignment edges, and highlight a cycle.
   Explain why a cycle implies deadlock only for single-instance resources, which is exactly why the Banker's
   Algorithm exists for multi-instance resources.
6. **Strategy comparison.** For the same random workload, compare *no avoidance*, *Banker's*, and *simple prevention*
   (for example, request everything up front). Report deadlocks, average wait, and resource utilisation. This
   demonstrates the cost of safety (lower utilisation) quantitatively.
7. **Explainability panel.** Plain-English reason for every outcome: "P4 asked for 3 A but only 2 are free" or "granting
   this leaves Work = [0 0 2]; every process still needs at least 1 A or 1 B."
8. **Import / export and reproducibility.** Save and load scenarios; export the history as CSV; a CLI flag to run a
   scenario headless and print the trace.
9. **Accessibility and UX.** Do not rely on colour alone (add icons/text for safe, unsafe, waiting); keyboard
   navigation; a light theme.

---

## Roadmap

**Phase 0: Repository hygiene**
- [x] No spaces in names, `.gitignore`, `LICENSE` (replace `<YOUR NAME>`), CI file
- [ ] Copy the old terminal program into `legacy/`

**Phase 1: Engine**
- [x] `BankerState`, `banker_validate()` with every rule
- [x] `banker_is_safe()` with documented policy and trace
- [x] `banker_request()` with ordered checks and copy-based rollback
- [x] `banker_release()`
- [x] `banker_why_not()` bounded request-space explorer and visitor callback
- [x] Tests T1-T14 plus validity checker and T15 explainability assertions

**Phase 2: Confidence**
- [x] Brute-force cross-check and random property tests
- [ ] Re-run AddressSanitizer after the Why-not extension (ASan shadow-memory reservation was unavailable in this build environment)
- [x] UndefinedBehaviorSanitizer run after the Why-not extension
- [x] CLI front end
- [ ] First green CI run on GitHub

**Phase 3: GUI wiring**
- [ ] GTK4 window, sidebar, page switching
- [ ] Dashboard fed by the engine
- [ ] System editor with validation messages
- [ ] Request simulator page

**Phase 4: Visualisation**
- [ ] Step-by-step replay from `Trace`
- [ ] "Why unsafe" panel
- [ ] Request history
- [ ] Embedded CSS via `glib-compile-resources`

**Phase 5: Advanced (choose one or two)**
- [ ] Multi-threaded mode (`pthread` + transaction-level mutex; controlled TOCTOU demonstration)
- [ ] Complexity / cache performance study
- [ ] Process lifecycle (arrive, finish) and deadlock-detection mode
- [ ] Resource-allocation graph view

**Phase 6: Release**
- [ ] Screenshots and demo GIF in `docs/`
- [ ] `docs/report.md` with design, results, viva notes
- [ ] Tag `v1.0`

---

## Limitations of the Banker's Algorithm

These belong in the report; examiners often ask about them.

- **Maximum demand must be declared in advance.** Most real programs cannot know their peak resource use.
- **Fixed number of processes and resources.** The base algorithm assumes neither changes.
- **Processes must release in finite time.** The safety argument assumes each process eventually finishes and returns
  what it holds.
- **Conservative.** An unsafe state is not necessarily a deadlock, so the algorithm may refuse requests that would have
  worked out. This lowers utilisation.
- **Cost.** `O(m * n^2)` on every request.
- **Why general-purpose operating systems rarely use it.** The assumptions above do not hold, so they typically rely on
  prevention by design (for example lock ordering) or on detection and recovery. It remains important for teaching and for
  closed systems where demands are known.
- **This tool is a simulator.** It models resources as integers and does not touch real operating-system resources.

---

## Viva preparation

Be ready to answer:

1. What is the difference between a safe state, an unsafe state, and a deadlock?
2. Why check `Request <= Need` before `Request <= Available`?
3. Why can an unavailable request and an unsafe request not be treated the same way?
4. Is the safe sequence unique? (No. This dataset has 16.)
5. What is the time complexity, and where does it come from?
6. How does your code guarantee rollback is correct?
7. Why is `Max <= Total` a required validation?
8. What assumptions does the algorithm make that real systems violate?
9. How does the algorithm differ from deadlock detection?
10. (If multi-threaded mode is built) Why must the check-and-grant be atomic?

---

## Contributing

Bug reports, test cases, and improvements are welcome once a licence is in place.

1. Fork and create a feature branch.
2. Keep the change focused; add or update tests.
3. Run `make test` and `make sanitize`.
4. Open a pull request; include screenshots for UI changes.

Design rule: the engine never includes GTK headers, and the GUI never re-implements algorithm decisions.

## License

MIT (see `LICENSE`). Add the `LICENSE` file before publishing. If you prefer to keep all rights reserved, say so
explicitly instead; a repository without a licence file is not reusable by others.

## References

- A. Silberschatz, P. B. Galvin, G. Gagne, *Operating System Concepts* (deadlocks chapter)
- A. S. Tanenbaum, H. Bos, *Modern Operating Systems* (deadlocks chapter)
- E. W. Dijkstra, "Cooperating Sequential Processes" (1965); A. N. Habermann, "Prevention of System Deadlocks" (1969)
- GTK4 documentation: https://docs.gtk.org/gtk4/

---

**ResourceGuard: making resource-allocation safety visible.**
