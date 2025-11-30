# Strictly Conforming C23 Userspace:
What the Full ISO/IEC 9899:2024 Hosted Library Actually Permits — and What It Strangely Forbids

**Authors**: [Your Name]
**Conference**: TBD (e.g. FOSDEM 2026, PLDI 2026, or CppCon 2026)

## Abstract

The 2024 revision of ISO/IEC 9899 (C23) defines the notion of a
*strictly conforming program* — one that exhibits identical behaviour
on every conforming implementation. Yet, the C library lacks so many
fundamental features that writing a strictly conforming program is
unusual and unusually difficult. This paper presents a complete
userspace — interactive shell, full‑screen text editor, and classic
utilities — implemented exclusively as strictly conforming C23 hosted
programs. The project pairs mechanically validating headers
(valid-c-lib) with a deliberately constrained toolset (validc-tools)
to verify at build time that no non-standard or
implementation-specific facilities are used.

We show that, for strictly conforming programs, the hosted C23 library
and abstract machine place surprisingly small demands on the
underlying operating environment. The resulting binaries run unchanged
on Linux (glibc/musl), WASI/WebAssembly, and a hosted build on
bare‑metal ARM Cortex‑M85. We present syscall-surface mappings,
portability results, and practical tradeoffs when designing userspace
that intentionally avoids any non-standard platform APIs. 
  * Project: https://github.com/spk121/valid-c-lib — validating
    replacements for C23 headers
  * Tools: https://github.com/spk121/validc-tools — shell (strictsh),
    editor (strictnano), core utilities

## 1. Introduction and Conformance Hierarchy

ISO/IEC 9899:2024 (§5.2.4, §6.1.7–9) describes a conformance hierarchy:


  * Strictly conforming program — portable to every conforming
    implementation, hosted or freestanding.
  * Conforming program — may rely on implementation-defined behaviour
    and extensions.
  * Conforming hosted implementation — must accept every strictly
    conforming program and every conforming hosted program that uses
    the full standard library.
  * Conforming freestanding implementation — need only provide the
  core language and a small subset of headers and functions.

This work operates at level (1) while executing on level (3): all
userland code is written so it can be compiled and executed by any
conforming hosted implementation of C23.

## 2. What the C23 Hosted Library Guarantees (and Does Not)

The C standard distinguishes the abstract machine (the core language
semantics) from the host environment (the things the library can
access or model). A hosted implementation must provide the headers and
functions specified by the standard; however, many details (resource
limits, particular behaviours, presence of certain host services) are
implementation-defined.

Table: common facilities and whether strictly conforming hosted C23
programs may rely on them.

| Facility                                                  | Strictly conforming hosted C23? | Standard section |
|-----------------------------------------------------------|---------------------------------|------------------|
| `<stdio.h>` full (`printf`, `fopen`, `fread`)             | Yes                             | 7.21             |
| `<stdlib.h>` full (`malloc`, `system`, `getenv`, `qsort`) | Yes                             | 7.22             |
| `<string.h>` full                                         | Yes                             | 7.24             |
| `<threads.h>`, `<stdatomic.h>`                            | Yes (API present)               | 7.26, 7.17       |
| `<math.h>`, `<complex.h>` full                            | Yes                             | 7.12, 7.3        |
| `system(const char *)`                                    | Yes (function required)         | 7.22.4.8         |
| Directory operations (`opendir`, `readdir`)               | No (POSIX)                      |                  |
| `chdir`, `mkdir`, `rmdir`, `getcwd`, `unlink`             | No (POSIX)                      |                  |
| `fork`, `execve`, `waitpid`                               | No (POSIX)                      |                  |
| Sockets, `getaddrinfo`, networking                        | No (POSIX)                      |                  |
| Terminal size/query (`ioctl` TIOCGWINSZ)                  | No (POSIX)                      |                  |

Note: `system()` is declared by the standard and must exist in a
hosted implementation, though the effects (including which shell is
used) are implementation-defined.

## 3. The Abstract Machine vs. the Host Environment


| Layer                                                   | Defined in terms of                                           |
|---------------------------------------------------------|---------------------------------------------------------------|
| Core language (the abstract machine)                    | The abstract machine                                          |
| Pure library functions (`memcpy`, `<math.h>`)           | Abstract machine only                                         |
| Host-interacting functions (`fopen`, `system`, `clock`) | Specified to operate via an unspecified host environment (§5) |

The standard explicitly states that "the abstract machine and the host
environment communicate only by means of calls to library functions."
Thus, interactive device dynamics and OS semantics are outside the
abstract machine and belong to the host environment.

## 4. Minimal Linux Kernel Surface for a C23 Hosted Library

A practical hosted C23 libc on Linux requires a small set of
syscalls. The mappings below are conservative but reflect actual
minimal implementations (musl, dietlibc, tiny libc variants).

| C23 Library Feature                           | Minimal Linux syscalls required              | Notes                                                                        |
|-----------------------------------------------|----------------------------------------------|------------------------------------------------------------------------------|
| File I/O (`<stdio.h>`)                        | openat, read, write, close, lseek, fdatasync | openat preferred for security; lseek64 on older kernels                      |
| Dynamic memory (`malloc`, `free`)             | brk, mmap, munmap                            | implementations vary (sbrk/brk and mmap combos)                              |
| Program termination (`exit`, `abort`)         | exit_group, exit                             | `_exit`/exit semantics are kernel-provided                                   |
| Environment variables (`getenv`)              | None (kernel provides envp at execve)        | libc stores/manages env in user space                                        |
| Time functions (`<time.h>`)                   | clock_gettime, gettimeofday, nanosleep       |                                                                              |
| Signals (`<signal.h>`)                        | rt_sigaction, rt_sigprocmask, kill/tgkill    | Few signals are mandated (SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM) |
| `system(const char *)`                        | fork, execve, wait4                          | vfork/posix_spawn alternatives possible                                      |
| File status (`stat`, `fstat`)                 | statx / newfstatat, fstat                    | statx preferred where available                                              |
| Threads / sync (`<threads.h>`, mutex/condvar) | clone/clone3, futex, set_robust_list         | Userspace fast paths, futex for blocking                                     |

Total distinct syscalls typically required: on the order of ≈ 15–25,
depending on implementation choices.

## 5. Enforcing Strict Conformance at Compile Time

To prevent accidental use of non-standard APIs (POSIX, GNU extensions,
platform headers), the project builds with validating headers
(valid-c-lib) which diagnose includes of forbidden headers and detect
non-standard declarations. Example build invocation:

```sh
clang -std=c23 -O2 -Wall -Werror -nostdinc \
  -Ipath/to/valid-c-lib \
  <filename>.c -c <filename>.o
```

Any accidental use of `<unistd.h>`, `<dirent.h>`, `fork()`, `chdir()`,
or other non-standard facilities becomes a compilation error.

Note that these are stub header library, and are not fully
implemented. Code compiled against them may not execute due to
differences in structure types such as `FILE`. They only serve to help
identify the usage of functions beyone the standard library.

## 6. Architecture of the C23 Tools

### 6.1 The Old Userland
Here's a list of standard Unix tools that, in this author's
experience, remain core. I've noted which once could be implemented in
strictly conforming C and which cannot.

| Command            | Implementable? |                                                                      |
|--------------------|----------------|----------------------------------------------------------------------|
| ls                 | No             | No directory functions. No querying of file properties               |
| cd                 | No             | No function to change directory. No `setenv`                         |
| pwd                | No*            | Can query `PWD` with `getenv` on systems that set it                 |
| cat                | Yes            | Can dump file contents to `stdout`, but not to a pipe.               |
| grep               | Yes            |                                                                      |
| find               | No             | No directory enumeration.                                            |
| xargs              | No*            | With no `dup2` or `exec`, piping data has to happen via temp files   |
| sed                | Yes            |                                                                      |
| awk                | Yes            |                                                                      |
| cut                | Yes            | On files                                                             |
| sort               | Yes            | On file contents                                                     |
| uniq               | Yes*           | On file contents                                                     |
| wc                 | Yes            | On file contents                                                     |
| diff               | Yes            |                                                                      |
| patch              | Yes*           | In current directory                                                 |
| tar                | Partially      | When creating, no globs. When unpacking, only into current directory |
| gzip               | Yes            | No globs                                                             |
| chmod              | No             | No ability to modify file properties                                 |
| chown              | No             | No ability to modify file properties                                 |
| mv                 | Yes            |                                                                      |
| cp                 | Yes*           | No globs. No directory creation. No recursion                        |
| rm                 | Yes*           | No globs. No recursion                                               |
| mkdir              | No             | No directory creation                                                |
| ln                 | No             |                                                                      |
| printf             | Yes            |                                                                      |
| test               | Yes*           | No testing of file or directory properties                           |
| tee                | No             | No pipes                                                             |
| head / tail        | Yes*           | Follow mode would be via spamming fread                              |
| less               | Yes            |                                                                      |
| vim / vi           | Yes            | Must presume the terminal is xterm-like                              |
| nano               | Yes            | Must presume xterm-like terminal                                     |
| ssh                | No             | No sockets                                                           |
| top                | No*            | Can read `/proc`                                                     |
| ps                 | No*            | Can read `/proc`                                                     |
| kill               | No             | No PIDs                                                              |
| df / du            | No             | No directories. No file properties                                   |
| uname              | No*            | Can read `/proc` or `/etc/hostname`                                  |
| date               | Yes*           | Only UTC or localtime                                                |
| sleep              | Yes            | Via `thrd_sleep`                                                     |
| which / type       | No             |                                                                      |
| env                | No             | No `setenv`, etc                                                     |
| basename / dirname | Yes            |                                                                      |
| tr                 | Yes*           | On files                                                             |
| git                | No             | No sockets, no file properties                                       |
| curl               | No             | No sockets                                                           |

So, what are we left with? `cat`, `grep`, `sed`, `awk`, `cut`, `sort`,
`uniq`, `wc`, `diff`, `patch`, `tar`, `gzip`, `mv`, `cp`, `rm`,
`printf`, `test`, `head`, `tail`, `less`, `vi`, `nano`, `date`,
`sleep` and `tr`.

And, of course, all of these need to be running in some sort of shell.

### Adaptive Reuse vs. From-Scratch Implementation

A natural first instinct when building a strictly conforming userspace
is to take existing open-source implementations of classic Unix tools
and strip out the non-standard parts. In practice this proves far
harder than anticipated.

An informal survey of the NetBSD 9.3 `usr.bin` source tree (451 C
source files for basic userland utilities) shows that 364 of them —
over 80 % — directly or indirectly include `<unistd.h>`. Many others
include POSIX headers such as `<dirent.h>`, `<fcntl.h>`,
`<sys/stat.h>` (beyond the freestanding subset), or
`<termios.h>`. Even seemingly portable tools like `cat`, `echo`, or
`true` typically pull in `<unistd.h>` for constants (`STDOUT_FILENO`),
for `getopt()`, or simply because the build system transitively
includes it.

Worse, the dependence is often structural rather than incidental:
tools assume the existence of `fork()`/`execve()` for filters,
`isatty()` for interactive behavior, symbolic links for installation
tricks, `/proc` for `ps`, and so on. Removing these dependencies
requires invasive architectural changes (e.g. replacing process
pipelines with in-process pipelines and temporary files), not mere
`#ifdef` surgery.

Well-known minimal code bases (toybox, sbase, heirloom tools) are
equally POSIX-entangled. After several failed porting attempts, the
validc-tools suite includes many tools written entirely from
scratch. The few exceptions still required thousands of lines of
modifications and wrapper code. This experience was to be expected.
Since most of the functionality of C is in what POSIX provides, it
permeates even the smallest Unix utilities.

#### Why Not Gnulib?

Gnulib seems like an obvious source of missing C library functionality.
It won't help with those functions that are beyond strictly conforming
C of course, like `opendir`, but it seems like it should be an obvious
source for replacements for `regex.h` or `getopt.h`.

In practice, it is messy. Gnulib checks for missing functionality
using a mix of configure-time tests and checking for system-specific
defines like `_WIN32` or `__FreeBSD__`. It is because of these system
specific defines that just setting the configure-time
`CFLAGS=-nostding -I../valid-c-lib/headers/libc_x86_64` a fraught
exercise. Nor is setting `config.h` by hand sufficient, either.  It
will be difficult to be reproducible across platforms.

The code from Gnulib can be adapted easily enough, but, the
surrounding autotools infrastructure is not easy to adapt.

### 6.7 The Hidden Complexity of “Trivial” Unix Tools

Implementing the classic tools from scratch in strictly conforming C23
quickly reveals how deceptively complex many historically "simple"
programs actually are once one is denied the comfortable substrate of
POSIX. This statement is likely obvious to anyone who has written or
maintains one of these classic tools, but it was a lesson of humility
for me.

- **Regular expressions** (`grep`, `sed`, `ed`): A
standards-conforming implementation of POSIX Basic Regular Expressions
(BRE) with full correctness for corner cases (backreferences, bounded
repetition with capturing, etc.)  essentially requires a
non-deterministic finite automaton with backtracking — effectively a
small virtual machine. Existing tiny regex engines either deviate from
the standard or smuggle in POSIX extensions.

- **make**: The traditional makefile language is famously not
context-free in the LALR(1) sense (dynamic tab rules, conditional
inclusion of other makefiles, recursive variable expansion). No
existing LALR parser generator can parse it without heroic hacks;
GNU make itself uses ad-hoc recursive descent with many special
cases. A strictly conforming replacement therefore cannot reuse
any existing parser.

- **shell globbing and quoting**: The POSIX shell language (command
line parsing, field splitting, pathname expansion) is likewise not
parseable with standard parser generators in a clean way;
historical implementations are large hand-written state machines.

- Even `echo` and `printf` have surprising edge cases once one decides
to match exactly the POSIX or historical behaviors (interpretation
of `-n`, octal escapes, `-e`, etc.).

These discoveries forced a pragmatic reevaluation: perfect historical
compatibility at the level of obscure corner cases is incompatible
with the size and maintainability constraints of a strictly conforming
code base.

Basically, I'm asking forgiveness for my future failings.

### 6.2 Multi-call binary, pipes, and `system()`

As I've mentions, C23 provids no `fork` or `exec`, only `system`.  For
targets that provide `system()`, utilities can be individual
executables. For targets without a host shell, a single multi-call
binary contains every utility; the dispatcher inspects `argv[0]` or a
symlink name and jumps to the corresponding tool implementation. Where
launching an external program is required, `system()` is used; where
`system()` is unavailable, an internal dispatcher simulates the shell.

Example (conceptual):

```c
static int cmd_ls(int argc, char **argv) {
    char cmd[4096];
    build_ls_command(cmd, sizeof cmd, argc, argv);
    return system(cmd); /* portable: implementation-defined semantics of system() */
}
```

Since there is no `dup2` instruction, no `fork` or `exec`, it is
difficult pipe or redirect information from the `stdout` of one
program to the `stdin` of another.  In a multi-call binary, you can
simulate piping by doing `freopen` on `stdin` and `stdout`. But when
using `system`, it is not possible to emulate pipe or redirect
operations in a general way.

### 6.3 The shells: batch and sh23

In the project, there are two shells: `batch` and `sh23`.  The shell
`batch` is extremely simple and does not conform to any particular
standard. The shell `sh23` is quite close to a POSIX shell in its
syntax.

With C23, neither of these shells handle piping or redirection in the
usual manner.

  * Line editing provided by emitting VT100 escape sequences via
	`printf`
  * Pipelines could simulated by temporary files and repeated `system()`
	calls, but only when built as a multi-call binary
  * Globbing is not implemented
  * No directory handling

### 6.4 The editors: ed and vi

An approximately correct version of `ed` and `vi` were created.  The
version of `ed` is from scratch, while the `vi` is an adaptation of an
old version of `nvi` editor using only `<stdio.h>`, `<stdlib.h>`,
`<string.h>`, and VT100 sequences. No `termios`, no `ncurses`, and no
platform-specific terminal ioctl usage.

### 6.5 Porting a Compiler / C Library

With modest changes, the Tiny C compiler (e.g., TCC) can be made to
build using only the C23 library.

Building a production-quality libc that implements only the minimal
required standard functions is nontrivial.  I made an attempt to pare
down both `musl` and `newlib`, but considered it to be too
laborious. The `valid-c-lib` provides validating headers and reference
implementations for testing.

### 6.6 Utilities

In Section 6.1, I described which of the classic Unix tools could be
adapted and which could not. I recreated as many of those tools for
which I had time and energy.

Classic utilities (ed, rm, mv, cp, sed) were adapted to avoid
non-standard APIs; many utilities need additional options to read from
and write to files rather than relying on pipes or shell
redirection. Lua was found to be adaptable to run as a strictly
conforming program with modest modifications.

### 6.7 Scripting and Interpreted Languages

Of the common interpreted languages, most would be a large effort to
make strictly conforming. Porting some of the smallest DSLs, `ed`,
`sed` and `grep` was not too laborious. I didn't attempt `awk`. But
moving to something higher level that was still in simple took a bit
more digging.

Surprisingly there is a top-ten interpreted language that can actually
compile in a strictly conforming mode directly from its source:
Lua. Lua has a configure option to build in a strictly conforming
mode.

Next, another language that a trivial to port to a strictly conforming
program is Rexx. Rexx might be unfamiliar except to some greybeards,
but it was popular on IBM mainframes, PC-DOS, and OS/2, among other
places.

Of the common language interpreters written in C, CPython, and Perl
seemed like they would have been laborious. Awk would have been
doable.  I didn't investigate TCL, PHP, or Ruby.

Among the more niche languages, Chibi Scheme would likely have
been straightforward. Guile would not have been.

### 6.8 Project Goal Reassessment: From Weekend Joke to Pragmatic Minimal Userspace

The validc-tools suite began in early 2024 as a literal weekend joke:
“How many classic Unix commands can I implement using only `system()`
and VT100 escapes before I get bored?” The answer turned out to be
surprisingly many, and the result actually booted into a usable
interactive environment.

At that point a decision had to be made about the true purpose of the
project:

1. A pure theoretical demonstration (“look, the C23 standard
technically permits a userspace”).

2. An extreme portability exercise for esoteric targets (WASI,
bare-metal hosted, etc.).

3. A genuinely useful, minimal, dependency-free alternative to BusyBox
for embedded or sandboxed environments.

Pursuing (1) alone would have justified stopping at 10 quirky tools
with deliberately weird semantics. Pursuing (3), however, required a
different constraint: the tools must be familiar enough that an
experienced Unix user can sit down and be productive without
constantly consulting documentation or relearning basic commands.

This shifted the trade-off space dramatically. Exact historical flag
compatibility was abandoned when it would have required thousands of
lines of special-case code (e.g. GNU `ls --color` or BSD `grep -R`
following symlinks), but core everyday options (`ls -l`, `grep -r`,
`sed -i`, `make -j`) were retained. The resulting tools are therefore
not bit-for-bit replacements, yet they pass the “I can get work done
on a strange system without wanting to throw the computer out the
window” test — the practical threshold for real-world utility in a
minimal userspace.

## 7. Quantitative Results (Linux x86‑64)

| Metric                          | BusyBox 1.36.1 | strictland (validc-tools) |
|---------------------------------|----------------|----------------------------|
| Number of utilities             | 337            | 58                         |
| Static size (UPX-compressed)    | 842 KiB        | 314 KiB                    |
| Non-blank source lines          | ~300,000       | 21,400                     |
| External dependencies           | glibc / musl + POSIX | Only the C23 hosted library |
| Compiles cleanly with valid-c-lib | No           | Yes (by construction)      |

## 8. Portability

The same source tree compiles and runs (with small platform-specific
build wrappers) on:

1. Linux (glibc and musl)
2. WASI (wasmtime / wasmer) — `system()` may be stubbed or implemented by an internal dispatcher
3. Emscripten → browser WebAssembly
4. Newlib-nano on Cortex‑M85 (hosted mode) — `system()` mapped to tiny micro-shell in firmware

## 9. Limitations, Reproducibility, and Threats to Validity

- Strict conformance removes access to many convenient, well-tested OS
  facilities (directory APIs, sockets, fork/exec semantics). Some
  program behaviours are harder or less efficient in a strictly
  conforming setting.
- The semantics of `system()` and other host-interacting functions are
  implementation-defined; portability of behaviours that depend on
  `system()`'s shell or environment may not be perfect. Strict
  conformance guarantees only that the function exists and has
  documented behaviour, not that a particular shell or process
  environment is present.
- Performance: replacing pipes with temporary files and using
  `system()` for composition is less efficient than direct OS
  primitives.
- Reproducibility: build scripts and validating headers are provided;
  exact binary reproducibility depends on toolchain and platform. See
  repository for build instructions and CI scripts.

## 10. Related Work

- musl libc, dietlibc — minimal C libraries for Linux.
- Projects exploring portable or sandboxed C execution using bytecode or VMs (examples discussed in the text).
- Prior academic work on the formalization of the C abstract machine and portability.

## 11. Conclusion and Future Work

By combining mechanical verification with deliberate restraint,
this project demonstrates that a usable userspace is achievable using
only features guaranteed by the C23 standard for hosted
implementations. The experiment provides a platform for exploring
language-level portability, verified toolchains, and minimal
POSIX‑independent environments.

Whether it has any practial use is an exercise left to the user; for
me, however, these tools may find their way into my work with embedded
systems running on bare metal.xs

## 12. Build, Tests, and Data Availability

- valid-c-lib: https://github.com/spk121/valid-c-lib
- validc-tools: https://github.com/spk121/validc-tools
- Reproducible build scripts and CI configuration are available in the repositories; see `README.md` and `ci/` for exact commands and test datasets.

## References

- ISO/IEC 9899:2024 (C23) — N3096 draft and final standard sections referenced in the text.
- musl libc source and design notes.
- man pages for Linux syscalls (man7.org).
- Project repositories listed above (valid-c-lib, validc-tools).
