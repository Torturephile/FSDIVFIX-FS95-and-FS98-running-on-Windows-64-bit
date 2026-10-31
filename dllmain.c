/* FSDIVFIX.DLL: Windows side.
 *
 * Drop FSDIVFIX.DLL into the Modules folder of Flight Simulator for Windows 95
 * or Flight Simulator 98. Both games load every DLL in that folder as one of
 * their own modules, so this DLL exports the "Linkage" block the module loader
 * requires. Everything in it is zero except a module ID no real module uses,
 * so the game registers the module and otherwise leaves it alone.
 *
 * When Windows loads the DLL, it identifies the game build (see core.c), pins
 * itself in memory (the game unloads some modules straight after scanning
 * them) and registers the division handler. It does nothing in any other
 * program, in an unknown build, or in an EXE that has our v2/v3 EXE patch. */
#include "core.h"

#define DLL_PROCESS_ATTACH 1
#define GET_MODULE_HANDLE_EX_FLAG_PIN          1
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4

__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char *name);
__declspec(dllimport) BOOL    WINAPI GetModuleHandleExA(DWORD flags, const char *name, HMODULE *module);
__declspec(dllimport) DWORD   WINAPI GetModuleFileNameA(HMODULE module, char *buf, DWORD size);
__declspec(dllimport) DWORD   WINAPI GetLongPathNameA(const char *in, char *out, DWORD size);
__declspec(dllimport) void *  WINAPI AddVectoredExceptionHandler(DWORD first, FILTER handler);
__declspec(dllimport) BOOL    WINAPI DisableThreadLibraryCalls(HMODULE module);

/* Game module descriptor. [0] = module ID (real modules use 1..33),
 * [1] init, [2] shutdown, [4] init priority, [5] extra table: all unused. */
__declspec(dllexport) DWORD Linkage[12] = { 0x46534446 /* 'FDSF' */ };

static const CORE_API windows_api = { GetModuleHandleExA, GetModuleFileNameA, GetLongPathNameA };
static int attached;

BOOL WINAPI DllMain(HMODULE self, DWORD reason, void *reserved)
{
    (void)reserved;
    int which;
    if (reason != DLL_PROCESS_ATTACH || attached) return 1;
    attached = 1;
    DisableThreadLibraryCalls(self);

    if (core_setup(&windows_api, (const BYTE *)GetModuleHandleA(0),
                   (const BYTE *)GetModuleHandleA("DIVTRAP.DLL"), &which) != SETUP_OK)
        return 1;                       /* not a supported game build, or already fixed: stay inert */

    HMODULE pinned;                     /* keep the handler's code loaded for good */
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (const char *)&Linkage, &pinned))
        return 1;                       /* can't pin: registering would be unsafe */

    AddVectoredExceptionHandler(1, core_veh);
    return 1;
}
