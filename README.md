# validc-tools
This intends to be a self-contained userland, rather like
busybox, but that only uses functions in the ISO C standard,
with no POSIX or Win32 extensions.

This may see disadvantageous. The ISO C standard chooses not to 
standardize functions vital for a proper userland. It doesn't even
have directory support.  But a userland that strictly complies
with the ISO C standard, while seemingly silly at first glance,
has utility for the professional work I do with
embedded systems, where filesystems and I/O are all custom
and rarely with a POSIX API.

So here I'm collecting and organizing several standalone tools
I've written into its own project, with a bash-like shell,
a vi-like editor, some common Unix tools, and some
scripting and DSL languages that can be made to compile using
strict ISO C: namely sed, lua, and (if I can figure out
where I left it) rexx.

In my professional work, I've written random shells and tools
to run on bare-metal systems. This project hopes to be a better
starting point should that need ever arise again.

I'm still actively pulling together bits from many projects
so this is disorganized, but, soon enough you should see.

    * _batch_: an extremely simple shell
    * _sh23_: a POSIX-like shell
    * _ed_ and _vi_
    * _lua_, _rexx_ and _tcc_: patches and build scripts for
      the upstream projects to get them to compile in ISO C
    * and a collection of Unix-like userland tools: grep, sed,
      cat, etc.


