# lib_fsm Repository Guide

Welcome to the `lib_fsm` repository — a finite state machine library, part of the Programming 101 C library collection. This guide will help you set up, build, and install the library.

`lib_fsm` owns executable state-machine policy: callbacks, decisions, effects,
receipts, notifications, and error reporting. Its transition index is provided
by the allocation-free `lib_transition` mechanism; `lib_fsm` retains ownership
of all allocated storage and executable behavior.

## **Table of Contents**

1. [Cloning the Repository](#cloning-the-repository)
2. [Prerequisites](#prerequisites)
3. [Configuring the Build](#configuring-the-build)
4. [Building](#building)
5. [Testing](#testing)
6. [Installing](#installing)
7. [Adding or Removing Files](#adding-or-removing-files)

## **Cloning the Repository**

Clone the repository using the following command:

```bash
git clone https://github.com/programming101dev/lib_fsm.git
```

Navigate to the cloned directory:

```bash
cd lib_fsm
```

Ensure the scripts are executable:

```bash
chmod +x *.sh
```

## **Prerequisites**

To ensure you have all of the required tools installed, run:

```bash
./check-env.sh
```

If you are missing tools follow these [instructions](https://docs.google.com/document/d/1ZPqlPD1mie5iwJ2XAcNGz7WeA86dTLerFXs9sAuwCco/edit?usp=drive_link). If something still looks wrong, `./doctor.sh` reports what actually works on this machine for this project.

## **Configuring the Build**

Tell CMake which compiler you want to use:

```bash
./change-compiler.sh -c <compiler>
```

To see the list of possible compilers:

```bash
cat supported_c_compilers.txt
```

Run it again any time to switch compilers; each compiler configures into its own build directory (e.g. `build-clang`, `build-gcc-15`).

## **Building**

To build the library run:

```bash
./build.sh
```

This compiles through the strict analysis pipeline: the clang-format check, clang-tidy, cppcheck, the Clang static analyzer, and hundreds of warnings under `-Werror`. `./build.sh -f` applies the formatter and tidy fixes in place.

## **Testing**

`./check.sh` is the one command to run before you submit: the format check, the strict build, the tests, and a short fuzz smoke run, with a single PASS/FAIL at the end.

The behavioral tests cover lifecycle ownership, terminal-state persistence,
pause/retry behavior, invalid transition tables, bad-transition recovery,
recursive-operation rejection, observer reentrancy, C++ linkage, and balanced
entry/exit tracing:

```bash
./test.sh
```

The fuzz target varies transition-table structure and callback results under
libFuzzer, AddressSanitizer, and UndefinedBehaviorSanitizer:

```bash
./fuzz.sh -t 30
```

## FSM contract

The fundamental operation is `p101_fsm_step()`. It executes exactly one state
callback or one rejected-transition policy decision and produces a
`struct p101_fsm_step_result`. `p101_fsm_run()` is only a convenience loop over
that operation.

### Construction and ownership

`p101_fsm_info_create()` receives the transition table. It validates the table
and builds an owned, immutable open-addressed hash map, so a machine cannot
resume with a different definition and the caller's array does not need to
remain alive. Construction is linear on average, transition lookup is constant
time on average, and no transition lookup allocates memory. The map stays at
or below a 50% load factor to keep probe chains short; that intentionally uses
more memory than the former compact array. Its internal iteration order is not
part of the API.

Exactly one transition must originate at `P101_FSM_INIT`. Every executable
state is at least `P101_FSM_USER_START`, and every table entry requires a
callback.

The function borrows two context pairs:

- the application `env` and `err`, passed to state callbacks;
- the FSM `fsm_env` and `fsm_err`, used for allocation, validation, notifiers,
  and bad-transition policy.

The machine owns its copied name and transition map. All four context objects
remain caller-owned and must outlive it. Keeping the error pairs separate
prevents an FSM diagnostic from replacing an application failure. Pass the FSM
error object to `p101_fsm_info_destroy()` as well, because destruction refuses
a recursive attempt and reports that policy failure explicitly.

### Typed decisions

Callbacks no longer overload integer state IDs with pause and exit sentinels.
They fill a `struct p101_fsm_decision` using one of:

```c
p101_fsm_decide_transition(decision, NEXT_STATE);
p101_fsm_decide_pause(decision);
p101_fsm_decide_exit(decision);
```

This makes a state ID, pause, and exit distinct operations. A callback that
does not provide a decision is refused with
`P101_FSM_REFUSAL_INVALID_CALLBACK_DECISION`.

### Commit semantics

A transition is committed only after:

1. the edge exists;
2. the will-change notifier succeeds;
3. the state callback provides a valid decision without raising an application
   error; and
4. the did-change notifier succeeds.

If any of those fail, the current state remains unchanged. A pause also leaves
the state unchanged, so the next step retries the same callback.

Each step result contains a monotonically increasing sequence number, the
source state, attempted state, selected next state, status, and typed refusal.
The sequence never wraps: an exhausted `size_t` sequence produces
`P101_FSM_REFUSAL_SEQUENCE_EXHAUSTED`. A step observer can collect these
records while `run` executes. It is an observation hook and must not call back
into or destroy the same machine.

Exit is persistent. Stepping an exited machine reports
`P101_FSM_STEP_EXITED` with `P101_FSM_REFUSAL_TERMINAL_MACHINE`.

### Effects

Callbacks may receive an optional `struct p101_fsm_effect_sink`. Calling
`p101_fsm_emit_effect()` sends a small typed record to that sink. Tests can
capture effects while a runtime can execute them; callers that do not need
effect separation pass `NULL`.

The sink is intentionally not an application framework. It has a string kind,
an opaque byte payload, and caller-owned context. Direct delivery is
synchronous and is not rolled back if the callback later pauses, returns an
invalid decision, or raises an error.

`p101_fsm_effect_batch_*` provides the bounded transactional option. It copies
effects into caller-sized storage during one `p101_fsm_step()` and delivers
them only after the step commits a transition or exit. Paused, refused, and
failed steps discard the batch. Capacity exhaustion is the typed
`P101_FSM_REFUSAL_EFFECT_CAPACITY`. The batch is deliberately step-scoped and
must not be passed to `p101_fsm_run()`. Final delivery still invokes external
code and therefore cannot undo effects already accepted by the target.

### Receipted transition boundary

`p101_fsm_step_with_receipt()` is the integration boundary for runtimes that
need accountable effects. One receipt binds the process-local machine identity,
argument identity, step sequence, source and attempted states, state-change
disposition, typed step result, and the exact staged effect generation. An
applied no-change (pause), a refusal, and an execution error remain distinct
without inspecting the effect list. `p101_fsm_step_receipt_effect()` exposes the
effects as borrowed views, and `p101_fsm_effect_batch_finish_receipt()` refuses
a stale receipt or a receipt paired with another batch.

The opaque batch retains the admitted binding and result privately. Changing a
public receipt field therefore makes effect lookup and delivery fail without
consuming caller-owned batch contents. This detects a forged or accidentally
cross-paired receipt; it does not snapshot the bytes behind the opaque argument
pointer.

The ordinary `p101_fsm_step()` remains the primitive transition operation for
callers that do not need a receipted effect boundary. The receipted API stages
effects around that same operation instead of maintaining a second transition
implementation.

The architecture choice is deliberately a caller-owned opaque batch plus a
borrowed receipt. Keeping the former separate result and batch as the primary
interface allowed accidental cross-step pairing. Putting a fixed effect array
inside every receipt would make the public ABI and stack cost depend on an
arbitrary capacity. The chosen design preserves caller-selected bounds and
detects reuse with a generation identity. Unlike Rust's borrow checker, C
cannot make use-after-destroy unrepresentable, and the machine pointer is not a
durable identity. Reopen this choice if receipts must cross a process boundary;
that requires an owned serialized record and an external append sequence, not
more mutable state inside the FSM.

### Refusal and execution boundaries

Unknown edges, invalid callback or handler decisions, redirect cycles, terminal
machines, and recursive invocation have distinct `p101_fsm_refusal` values.
The default unknown-transition handler also raises
`P101_FSM_ERROR_UNKNOWN_TRANSITION`; a custom handler may redirect, pause, or
exit.

An FSM instance is not thread-safe or reentrant. A callback must not step, run,
or destroy the same instance. Recursive operations are rejected before state
is changed.

The library validates table structure and transition decisions. It cannot prove
that callbacks terminate, all states are reachable, effects are safe to
execute, or callback-owned data remains valid. Those remain caller
responsibilities.

## **Installing**

To install the library run:

```bash
./install.sh
```

You may need to run it via sudo, or give the user account access to the install directories. `./uninstall.sh` removes it again.

## **Adding or Removing Files**

The `CMakeLists.txt` is fixed and shared across every repository — do not edit it. When you add or remove a source or header, edit the lists in `config.cmake` (`p101_fsm_SOURCES`, `p101_fsm_HEADERS`, and `p101_fsm_LINK_LIBRARIES`), then re-configure and build:

```bash
./change-compiler.sh -c <compiler>
./build.sh
```
