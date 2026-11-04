# FSDIVFIX.DLL

### Disclaimer:

This was built by Claude Code Opus 5.5. I fed Claude Windows Problem Reporting error logs and x32dbg screenshots. I created this due to being bored of running *FS95* and *FS98* under VMware and much preferring them to work natively on Windows 10 and 11 like *CFS1*, *FS2000*, etc. I like being able to directly access the game files without turning on a virtual environment, let alone the games run better directly rather than in said virtual environment.

Please contribute if you like, maybe add some actual human-written code instead of taking a shortcut like I did.

### What it is:

FSDIVFIX.DLL is a module that allows *Flight Simulator for Windows 95* and *Flight Simulator 98* to run on 64-bit editions of Windows without the following:
- *FS95* crashing when enabling top-down view, the secondary window, and cycling views backwards from the 2D panel to the spot plane view.
* *FS98* crashing at the splash screen.

This DLL is meant to replace the [EXE patcher](https://github.com/Torturephile/FS95-and-FS98-Win64-patcher/tree/main) that was created previously so no game files will be modified at all. It's just a simple drag and drop. It also works with games' official patches, unlike the EXE patcher.

### How to install and delete:
- Copy FSDIVFIX.DLL into the Modules folder inside the main games folder. Use the game's own EXEs (original or official patch), not ones changed by the FS95/FS98 Win64 EXE patcher..
* To delete, just remove FSDIVFIX.DLL from the Modules folder.
+ It does not require anything else to be installed.

### How it works:
Both games load every DLL in their Modules folder as one of their own modules. FSDIVFIX.DLL exports the "Linkage" block the games require, with a module ID no real module uses and nothing else set, so the game registers it and otherwise leaves it alone. When it loads, it identifies the game build, checks that the game's handler is exactly where and what it expects, and registers that handler in a way 64-bit Windows does call. It is used only for integer divide faults in the game's own files (and, for FS98 6.10.446, whose handler also covers floating-point exceptions like in *CFS1*, for those too); every other error, and any fault in Windows or other programs' DLLs, is passed on exactly as before.

It does nothing in any other program or EXE build.

### This DLL supports all versions of the games:
- *FS95* version 6.00.117  (original game install)
* *FS95* version 6.00.119  (official [fsconv.exe](https://web.archive.org/web/19990117005131/microsoft.com/games/fsim/converter.htm) patch)
+ *FS98* version 6.10.418  (original game install)
- *FS98* version 6.10.419  (official [fsconv98.exe](https://simviation.com/1/download-file?file=fsconv98.exe) patch)
* *FS98* version 6.10.446  (official [fs98pat1.exe](https://simviation.com/1/download-file?file=fs98pat1.exe) patch)

### Why did the games crash on 64-bit editions of Windows::
The games deliberately let some integer divisions fail and rely on their own handler, DIVTRAP.DLL for *FS95* and inside FLTSIM98.EXE for *FS98*. When a division fails, it reads the instruction from memory, puts a capped result in the right registers, and skips past the instruction. On 64-bit Windows, that handler can't be reached while the game is handling a window message, so Windows ends the game instead. The games run everything under it with __try/__except, which is exactly the mechanism 64-bit Windows can’t reach inside a window callback. More information better explained [here](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wndproc).

### Extra information:

This package contains no Microsoft code. The source code is inside the source folder; build.sh rebuilds FSDIVFIX.DLL byte for byte with clang and lld-link (LLVM 18).

SHA-256 of FSDIVFIX.DLL:
  D337C8049569141B260B724873627D5C38CFF6A58F15FF0C033F7D2C26A3DDF4

### Other goodies:
For *FS95*, give the [ILS patch](https://web.archive.org/web/19990117055640/http://www.microsoft.com/games/fsim/downloads.htm) a go. It's located at the bottom of the page. Sadly, the other downloads in the page don't work.

For *FS98*, use [DDrawCompat](https://github.com/narzoul/DDrawCompat) to fix the hardware renderer's issue displaying the 2D panel, plus other improvements. That also helps with *FS2000* which has the same issue in full screen. I also suggest that if you install *FS98*'s official patch, backup the original HG2D.DLL and replace the patched version with it, as the patched version introduces an annoying bug with the hardware renderer that wipes out all textures whenever the program's 3D window is clicked at and so on. The only expense is the flaps volume will be inconsistent at times.

I suggest getting [WinHlp32.EXE](https://github.com/knuth-konrad/winhlp32-on-windows10), as these games need it to display help items, similar to the Learing Center from FS2004 and FSX.
