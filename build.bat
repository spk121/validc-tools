; build.bat - Build all tools in the C23 toolkit

; Compiler settings
var CC=gcc
var CFLAGS=-std=c23

; Update hash database to remove stale entries
; fnvupdate
; echo Updated file_hash.dat

; Build batch
fnvtest batch.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o batch batch.c
echo batch build status: {{?}}

; Build echo
fnvtest echo.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o echo echo.c
echo echo build status: {{?}}

; Build cat
fnvtest cat.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o cat cat.c
echo cat build status: {{?}}

; Build chgenc
fnvtest chgenc.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o chgenc chgenc.c
echo chgenc build status: {{?}}
; Build fnvtest
fnvtest fnvtest.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o fnvtest fnvtest.c
echo fnvtest build status: {{?}}

; Build fnvupdate
fnvtest fnvupdate.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o fnvupdate fnvupdate.c
echo fnvupdate build status: {{?}}

; Build ifc
fnvtest ifc.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o ifc ifc.c
echo ifc build status: {{?}}

; Build sed
fnvtest sed.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o sed sed.c bre.c
echo sed build status: {{?}}

; Build checkenc
fnvtest checkenc.c
ifc {{?}} == 1 {{CC}} {{CFLAGS}} -o checkenc checkenc.c
echo checkenc build status: {{?}}

; Final status
echo Build complete