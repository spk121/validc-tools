BATCH EXECUTION PROGRAM MANUAL
=============================

NAME
    batch - A simple batch file execution program

SYNOPSIS
    batch [OPTIONS] [FILENAME]

DESCRIPTION
    The 'batch' program reads commands from a file or standard input (stdin),
    performs variable substitution, and executes the commands using the
    system's command processor. It supports line continuation, comments,
    and variable assignment within the command file. It is designed to
    facilitate automated builds in a C23-only development environment.

OPTIONS
    -h, --help
        Display a help message showing usage information and exit.

    -v, --verbose
        Print each command before execution and its return value after.

    -i, --ignore-errors
        Continue execution even if a command returns a non-zero value.
        Without this flag, a command failure terminates the program with an error.

    -n, --dry-run
        Perform variable substitution and print commands without executing them.
        All warnings are still issued, but system() is not called; commands are
        assumed to succeed.

    FILENAME
        Optional path to a command file. If omitted, reads from stdin until EOF
        (e.g., Ctrl+D on Unix-like systems, Ctrl+Z on Windows).

COMMAND FILE FORMAT
    The command file consists of lines of text, processed sequentially. Lines
    can be commands to execute, variable assignments, comments, or empty. The
    program supports two line endings: '\n' (Unix-style) and '\r\n' (Windows-style).
    Every line must end with one of these, except the final line at EOF.

    Comments
        A line starting with a semicolon (;) is a comment and ignored, but only
        if the semicolon is the first character of a new line (not after
        continuation). Semicolons elsewhere are part of the command or value.
        Examples:
            ; This is a comment
            echo Hello ; This is not a comment

    Line Continuation
        A backslash (\) as the last character before the line ending indicates
        continuation. The backslash is removed, and the next line is appended.
        A semicolon on a continued line is not a comment. Example:
            echo This is a \
            single command
        Resulting command: "echo This is a single command"
        Continuation with semicolon:
            echo Start \
            ;end
        Resulting command: "echo Start ;end"

    Variable Assignment
        Variables are set with the 'var' command:
            var <name>=<value>
        - Whitespace after 'var' and before '=' is ignored.
        - <name>: 1-31 ASCII printable characters (32-126), excluding '(', ')', '$', '='.
          Invalid names trigger a warning and are skipped.
        - <value>: All characters after '=' up to the line ending, up to 255 characters.
          Longer values are truncated with a warning.
        - Maximum 100 variables; exceeding this triggers a warning.
        Examples:
            var name=value
            var    dir    =   path with spaces
            var tool=hammer     ; Preserves spaces and semicolon

    Variable Substitution
        Variables are referenced with {{name}} syntax. The reference is replaced
        with the variable's value. Undefined variables become empty strings.
        Example:
            var dir=/home/user
            echo {{dir}}/file.txt
        Resulting command: "echo /home/user/file.txt"

    Commands
        Lines not starting with 'var' (after continuation) are commands, passed
        to the system's command processor after substitution. Examples:
            echo Hello World
            gcc {{cflags}} -o myprog myprog.c

LIMITATIONS
    - Maximum line length (including continuations): 1023 characters
    - Maximum variable name length: 31 characters
    - Maximum variable value length: 255 characters
    - Maximum number of variables: 100
    - Only '\n' and '\r\n' line endings are supported
    - Comments only at the start of a new line
    - Lines exceeding 1023 characters without a newline cause an error, unless
      it’s the final line at EOF
    - Command encoding depends on the system (e.g., UTF-8 or active code page)

EXAMPLES
    Sample build script (build.bat):
        ; Define build variables
        var CC=gcc
        var CFLAGS=-std=c23
        
        ; Compile the program
        {{CC}} {{CFLAGS}} -o myprog myprog.c

    Running the program:
        batch build.bat            ; Execute, exit on failure
        batch -v build.bat         ; Verbose execution
        batch -i build.bat         ; Ignore errors and continue
        batch -n build.bat         ; Dry run, print commands
        batch -v -n build.bat      ; Verbose dry run
        batch < build.bat          ; From stdin
        batch --help               ; Show help

EXIT STATUS
    0   Successful execution (all commands return 0 or -i used)
    1   Error (e.g., file not found, incomplete line, or command failure without -i)

NOTES
    - Uses system(), so command behavior and return values are platform-dependent.
    - Empty lines are ignored unless part of a continued command.
    - Warnings are printed to stderr for invalid syntax or limits exceeded.
    - Semicolons within commands/values are preserved.
    - Final line without a newline at EOF is processed; otherwise, incomplete
      lines cause an error.
    - In dry run mode (-n), commands are printed but not executed, assuming success.

AUTHOR
    Generated by Grok 3, built by xAI
    Date: April 08, 2025