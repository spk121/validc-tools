# strictland

[![CI](https://github.com/spk121/strictland/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/spk121/strictland/actions/workflows/ci.yml)

This project, _strictland_,
intends to be a self-contained userland, rather like
busybox, but that only uses functions in the ISO C standard,
with no POSIX or Win32 extensions.

This quixotic quest has huge and obvious disadvantages.
The ISO C standard chooses not to 
standardize functions vital for a proper userland. It doesn't even
have directory support.  But a userland that strictly complies
with the ISO C standard, while seemingly silly at first glance,
has utility for the professional work I do with
embedded systems, where filesystems and I/O are all custom
and rarely with a POSIX API.  This set of tools has extremely limited
expectations of the host environment.

So here I'm collecting and organizing several standalone tools
I've written into its own project, with a
couple of shell programs, one of which is `sh`-like,
a `vi`-like editor, some common Unix tools, and some
scripting and DSL languages that can be made to compile using
strict ISO C: namely sed, awk, lua, and (if I can figure out
where I left it) rexx.

In my professional work, I've written random shells and tools
to run on bare-metal systems. This project hopes to be a better
starting point should that need ever arise again.

I'm still actively pulling together bits from many projects
so this is disorganized, but, soon enough you should see.

* _batch_: an extremely simple shell
* _sh23_: a POSIX-like shell
* _ed_ and _vi_: editors from the old days
* _lua_, _rexx_: scripting languages that can be built in ISO C
* a collection of Unix-like userland tools: grep, sed, cat, etc.

Someday I may also complete some other related projects.

* _tcc_: a lightly patched version of the Tiny C Compiler that
  only requires ISO C to compile it
* _valid-c-lib_: a C library that only includes the necessary
  functions required by ISO C.


