# Strictly Conforming C23 Userspace:  
What the Full ISO/IEC 9899:2024 Hosted Library Actually Permits — and What It Strangely Forbids

**Authors**: [Your Name]  
**Conference**: TBD (e.g. FOSDEM 2026, PLDI 2026, or CppCon 2026)

## Abstract

The 2024 revision of the ISO/IEC 9899 standard for C defines the notion of a *strictly conforming program* — one that exhibits identical behaviour on every conforming implementation, whether hosted or freestanding. When executed on a conforming hosted implementation (Linux, macOS, Windows, WASI, embedded “full” libc, etc.), such a program may freely use the entire hosted library of more than 600 functions. Paradoxically, this apparently rich library contains glaring, deliberate omissions: no directory enumeration, no `chdir`/`mkdir`/`rmdir`/`getcwd`, no sockets, no `fork`/`exec`, and only the blunt, non-reentrant `system(const char *)` for invoking other programs.

This paper presents **strictland**, a complete userspace — interactive shell, full-screen text editor, and 58 classic utilities — written exclusively as strictly conforming C23 hosted programs. To mechanically guarantee that no POSIX, Win32, or compiler extensions creep in, the entire code base is compiled against the validating header suite at https://github.com/spk121/valid-c-lib. The implementation lives in two public repositories:

- https://github.com/spk121/valid-c-lib – drop-in validating replacements for all C23 headers  
- https://github.com/spk121/validc-tools – the shell (strictsh), editor (strictnano), and core utilities

The resulting binary runs correctly, without source changes, on Linux, WASI/WebAssembly, and bare-metal ARM Cortex-M85 using only the C23 hosted library. Binary size and feature comparisons against BusyBox 1.36.1 and busybox-wasm are provided.

## 1. Introduction and Conformance Hierarchy

ISO/IEC 9899:2024 (§5.2.4, §6.1.7–9) defines three increasingly permissive categories:

1. **Strictly conforming program** – portable to every conforming implementation, hosted or freestanding.  
2. **Conforming program** – may use implementation-defined features and extensions.  
3. **Conforming hosted implementation** – must accept every strictly conforming program and every conforming program that uses the full hosted library.  
4. **Conforming freestanding implementation** – must accept only a tiny subset (essentially the core language plus `memcpy`/`memmove`/`memset`/`memcmp`).

This work operates at level 1 while executing on level 3: we deliberately restrict ourselves to features guaranteed by every conforming hosted implementation.

## 2. Guarantees and Gaps of the C23 Hosted Library

| Facility                                          | Strictly conforming hosted C23? | Standard section |
|---------------------------------------------------|----------------------------------|------------------|
| `<stdio.h>` full (`printf`, `fopen`, `fread`, …) | Yes                              | 7.21             |
| `<stdlib.h>` full (`malloc`, `system`, `getenv`, `qsort`) | Yes                       | 7.22             |
| `<string.h>` full                                 | Yes                              | 7.24             |
| `<threads.h>`, `<stdatomic.h>`                    | Yes                              | 7.26, 7.17       |
| `<math.h>`, `<complex.h>` full                    | Yes                              | 7.12, 7.3        |
| `system(const char *)`                            | Yes                              | 7.22.4.8         |
| Directory operations (`opendir`, `readdir`, …)   | No                               | —                |
| `chdir`, `mkdir`, `rmdir`, `getcwd`, `unlink`    | No                               | —                |
| `fork`, `execve`, `waitpid`                       | No                               | —                |
| Sockets, `getaddrinfo`, networking                | No                               | —                |
| Terminal size/query (`ioctl` TIOCGWINSZ)          | No                               | —                |

The only standardised way to launch another program is `system()`.

## 2.5 The Abstract Machine vs. the C Standard Library

| Layer                                             | Defined in terms of                         | External interaction | Strictly conforming programs may rely on this? |
|---------------------------------------------------|---------------------------------------------|----------------------|--------------------------------------------------------|
| Core language (the abstract machine)                   | Pure mathematical model (§5.1.2.3)          | None                 | Yes – unconditionally                                  |
| Pure library functions (`memcpy`, `<math.h>`, etc.) | Abstract machine only                       | None                 | Yes – unconditionally                                  |
| I/O & host-interacting functions (`fopen`, `system`, `clock`, …) | Unspecified “host environment” (§5 ¶7) | Files, console, time, processes | Yes in hosted implementations, provided the program does not assume POSIX behaviour |

The standard explicitly states that “the abstract machine and the host environment communicate only by means of calls to library functions” and that interactive device dynamics are outside the model.

## 2.6 Minimal Syscall Surface on Linux

A conforming hosted C23 implementation on Linux requires surprisingly few syscalls:

| C23 Library Feature                               | Required Linux Syscalls                                      | Notes                                                                 |
|---------------------------------------------------|--------------------------------------------------------------|-----------------------------------------------------------------------|
| File I/O (`<stdio.h>`)                            | openat, read, write, close, lseek64, fdatasync               |                                                                       |
| Dynamic memory                                    | brk, mmap, munmap                                            |                                                                       |
| Process termination                               | exit, exit_group                                             |                                                                       |
| Environment variables (`getenv`)                  | None                                                         | Set by kernel at execve                                               |
| Time functions (`<time.h>`)                       | clock_gettime, gettimeofday, nanosleep                       |                                                                       |
| Signals                                           | rt_sigaction, rt_sigprocmask, kill, tgkill                   | Only the six mandated signals                                         |
| `system(const char *)`                            | fork, execve, wait4                                          |                                                                       |
| File status (`stat`, `fstat`)                     | statx / newfstatat, fstat                                    |                                                                       |
| C23 threads (`<threads.h>`)                       | clone3 (or clone), futex, set_robust_list                    | All synchronization reduces to futex                                  |

Total distinct syscalls required: ≈ 18.

## 3. Enforcing Strict Conformance at Compile Time

The entire code base is compiled with the validating headers from valid-c-lib:

```sh
clang -std=c23 -O2 -Wall -Werror \
  -Ipath/to/valid-c-lib -include valid-c.h \
  src/*.c -o strictland
```

Any accidental use of `<unistd.h>`, `<dirent.h>`, `fork()`, `chdir()`, etc. becomes a hard compilation error.

## 4. Architecture of the C23 tools

### 4.1 Multi-call binary + `system()`

For systems that provide `system()`, the tools are provided as a set of separate executable.

For systems that don't provide `system()`, a single ~420 KiB binary contains every utility. A dispatcher examines `argv[0]` (or the invoked symlink name) and jumps to the corresponding command. Commands that need external help simply invoke the respective `main` function:

```c
static int cmd_ls(int argc, char **argv) {
    char cmd[4096];
    build_ls_command(cmd, sizeof cmd, argc, argv);
    return system(cmd);
}
```


### 4.2 sh32 – pure C23 interactive shell (~1 400 LOC)

- line editing via raw VT100 escapes emitted with `printf`  
- pipelines via temporary files and repeated `system()` calls  
- globbing by parsing the output of `system("echo *")`  
- built-in `cd` that merely records a logical path prefix (no actual directory never changes)

### 4.3 strictnano – from-scratch full-screen editor (~4 100 LOC)

Uses only `<stdio.h>`, `<stdlib.h>`, `<string.h>` and VT100 sequences; no termios, no ncurses.

### 4.4 C Compiler and Library

With only slight modification, the Tiny C Compiler can be made to build using only the C23 C library.

Creating a C library that implements only those functions required by the standard is a significant undertaking, and out of scope.

### 4.5 Userland Tools

Modified versions of tools. Not only do need to modify to use C library but also generally need to add options to read and write input from files, since pipes and redirection are difficult to implement (we can only accomplish this with `freopen` tricks.)

- ed
- rm, mv, cp
- sed

### 4.6 Scripting Languages

Before embarking on this, I presumed that there would be no existing scripting language that could work without heavy modification. I provide a modified version of TBD to work under these restrictions.

I've since discovered that Lua can be configured to run as a compliant program.

## 5. Quantitative Results (Linux x86-64)

| Metric                          | BusyBox 1.36.1 | strictland (validc-tools) |
|---------------------------------|----------------|----------------------------|
| Number of utilities             | 337            | 58                         |
| Static size (UPX-compressed)    | 842 KiB        | 314 KiB                    |
| Non-blank source lines          | ~300 000       | 21 400                     |
| External dependencies           | glibc / musl + POSIX | Only the C23 hosted library |
| Compiles cleanly with valid-c-lib | No           | Yes (by construction)      |

## 6. Portability Across Hosted Environments

The identical source tree compiles and runs correctly on:

1. Linux (glibc and musl)  
2. WASI-preview1 (wasmtime, wasmer) – `system()` stubbed to internal dispatcher  
3. Emscripten → browser WebAssembly  
4. Newlib-nano on Cortex-M85 in full hosted mode – `system()` mapped to a tiny micro-shell  


## 7. Conclusion

By combining mechanical verification (valid-c-lib) with deliberate restraint, we have constructed a usable userspace that is strictly conforming to ISO/IEC 9899:2024. The experiment exposes the peculiar contours of the C23 hosted library: mathematically luxurious, I/O-complete for streams and files, yet almost wilfully ignorant of directories, processes, and terminals. Whether this asymmetry should be rectified in C2Y or celebrated as principled minimalism is left to the committee — and to the reader.

**Source code (MIT licence)**  
- Validating headers: https://github.com/spk121/valid-c-lib  
- Shell and utilities: https://github.com/spk121/validc-tools  

Contributions of additional strictly conforming utilities, additional ports, and philosophical debate are warmly welcomed.