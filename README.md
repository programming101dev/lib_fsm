# lib_fsm Repository Guide

Welcome to the `lib_fsm` repository — a finite state machine library, part of the Programming 101 C library collection. This guide will help you set up, build, and install the library.

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
recursive-run rejection, C++ linkage, and balanced entry/exit tracing:

```bash
./test.sh
```

The fuzz target varies transition-table structure and callback results under
libFuzzer, AddressSanitizer, and UndefinedBehaviorSanitizer:

```bash
./fuzz.sh -t 30
```

## FSM contract

`p101_fsm_info_create()` borrows two context pairs:

- the application `env` and `err`, passed to state callbacks;
- the FSM `fsm_env` and `fsm_err`, used for allocation, validation, notifiers,
  and bad-transition policy.

The FSM copies and owns its name, but all four context objects remain owned by
the caller and must outlive the FSM. Keeping the pairs separate prevents a
diagnostic or policy failure inside the FSM from overwriting an application
error.

Transition tables contain executable edges only. Pass the number of elements
(`sizeof(table) / sizeof(table[0])`), not the table's raw byte size, to
`p101_fsm_run()`. Every entry requires a non-null callback. `P101_FSM_EXIT` and
`P101_FSM_IGNORE` are callback results; neither is a transition-table
destination.

`p101_fsm_run()` returns:

- `P101_FSM_RUN_EXITED` after a callback or bad-transition handler requests
  `P101_FSM_EXIT`;
- `P101_FSM_RUN_PAUSED` after one returns `P101_FSM_IGNORE`;
- `P101_FSM_RUN_ERROR` when either error object is set or the contract is
  violated.

Exit is persistent: running an exited FSM again is a no-op. Pause retains the
current state, so a later run retries that state callback. An FSM instance is
not thread-safe or reentrant; use one instance per execution context and never
run or destroy the same instance from one of its callbacks.

Named library error codes are declared in `<p101_fsm/errors.h>`.

The library can validate the transition table it receives, but it cannot prove
that callbacks eventually terminate, that every application state is
reachable, or that callback-owned data remains valid. Those remain caller
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
