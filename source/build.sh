#!/bin/sh
# Builds FSDIVFIX.DLL (FS95 + FS98) with clang + lld-link (LLVM 18). No C runtime is used.
set -e
llvm-dlltool -m i386 -k -d kernel32.def -l kernel32.lib
CF="--target=i686-pc-windows-msvc -O2 -march=i686 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror"
clang $CF -c core.c -o core.obj
clang $CF -c dllmain.c -o dllmain.obj
lld-link /dll /nologo /machine:x86 /subsystem:windows /nodefaultlib /entry:DllMain /safeseh:no /Brepro \
         /out:FSDIVFIX.DLL core.obj dllmain.obj kernel32.lib
