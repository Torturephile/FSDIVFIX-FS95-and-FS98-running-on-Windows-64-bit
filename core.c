/* FSDIVFIX: the logic.
 *
 * Flight Simulator for Windows 95 and Flight Simulator 98 let some integer
 * divisions fault on purpose and rely on their own filter (in DIVTRAP.DLL for
 * FS95, inside the EXE for FS98), reached through __try/__except, to write a
 * capped result and carry on. On 64-bit Windows a fault inside a window
 * callback never reaches that __except, so the game is terminated.
 *
 * This code registers a vectored exception handler that passes integer divide
 * faults (C0000094 / C0000095) to that same filter, but only when the faulting
 * instruction is NOT inside a DLL located outside the game folder. FS98 6.10.446
 * (fs98pat1) also routes x87 exceptions through its filter, so for that build
 * the x87 codes are passed on to it too, with the same location check.
 * Every other exception is passed on unchanged (EXCEPTION_CONTINUE_SEARCH). */
#include "core.h"

#define STATUS_INTEGER_DIVIDE_BY_ZERO 0xC0000094UL
#define STATUS_INTEGER_OVERFLOW       0xC0000095UL
#define STATUS_FLOAT_FIRST            0xC000008DUL   /* denormal operand ... */
#define STATUS_FLOAT_LAST             0xC0000093UL   /* ... underflow */
#define FROM_ADDRESS_UNCHANGED_REFCOUNT 6   /* GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | _UNCHANGED_REFCOUNT */
#define PATH_MAX_A 260
#define PATH_BUF   264

/* ---- supported builds ---- */
static const BYTE divtrap_filter[32] = {      /* DIVTRAP.DLL RVA 0x1010 */
    0x55,0x8B,0xEC,0x83,0xEC,0x68,0x53,0x56,0x57,0xC7,0x45,0xF4,0x00,0x00,0x00,0x00,
    0xC7,0x45,0xB0,0x00,0x00,0x00,0x00,0xC7,0x45,0xB4,0x00,0x00,0x00,0x00,0xC7,0x45 };
static const BYTE fs98_filter[32] = {         /* FLTSIM98.EXE 0x4119A0 (6.10.418), 0x411A30 (6.10.419) */
    0x83,0xEC,0x24,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x38,0x33,0xC0,0x89,0x44,0x24,
    0x30,0x89,0x44,0x24,0x2C,0x89,0x44,0x24,0x14,0x8B,0x07,0x8B,0x00,0x3D,0x94,0x00 };
static const BYTE fs98_446_dispatch[32] = {   /* FLTSIM98.EXE 6.10.446 0x4121D0: routes by code */
    0x8B,0x4C,0x24,0x04,0x8B,0x01,0x8B,0x00,0x05,0x73,0xFF,0xFF,0x3F,0x83,0xF8,0x08,
    0x77,0x21,0x33,0xD2,0x8A,0x90,0x14,0x22,0x41,0x00,0xFF,0x24,0x95,0x08,0x22,0x41 };

const GAME core_games[] = {
    { "FS95 6.00.117 (original)",          0x323558A1, 0x12B70, 0x1F000, 1, 0x01010, divtrap_filter,     0 },
    { "FS95 6.00.119 (fsconv patch)",      0x328B66AB, 0x12BC0, 0x1F000, 1, 0x01010, divtrap_filter,     0 },
    { "FS98 6.10.418 (original)",          0x33E94BD5, 0x14CE0, 0x21000, 0, 0x119A0, fs98_filter,        0 },
    { "FS98 6.10.419 (fsconv98 patch)",    0x3447DD25, 0x14D80, 0x21000, 0, 0x11A30, fs98_filter,        0 },
    { "FS98 6.10.446 (fs98pat1 patch)",    0x3498D991, 0x14E50, 0x21000, 0, 0x121D0, fs98_446_dispatch,  1 },
};
const int core_game_count = sizeof core_games / sizeof core_games[0];

/* ---- state ---- */
static const CORE_API *api;
static FILTER g_filter;
static int    g_x87;                 /* also pass x87 exceptions to the filter */
static DWORD  g_dirlen;              /* 0 = location check off: every handled fault goes to the filter */
static char   g_dir[PATH_BUF];       /* game folder, long-path form, with trailing '\' */
static volatile DWORD g_cache[4];    /* HMODULE | verdict (1 = game file, 2 = elsewhere) */
static volatile DWORD g_next;

static DWORD rd32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }

static int bytes_equal(const BYTE *a, const BYTE *b, int n)
{
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void copy_str(char *dst, const char *src, DWORD max)
{
    DWORD i = 0;
    for (; i + 1 < max && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c | 0x20) : c; }

/* FALSE only if addr is inside a module whose file is outside the game folder. */
static int is_game_code(void *addr)
{
    HMODULE mod;
    char path[PATH_BUF], longpath[PATH_BUF];
    const char *p;
    DWORD n, verdict, i;

    if (!api->GetModuleHandleExA(FROM_ADDRESS_UNCHANGED_REFCOUNT, (const char *)addr, &mod))
        return 1;                                    /* not inside any module */

    DWORD h = (DWORD)mod;
    if ((h & 0xFFFF) == 0) {                         /* module bases are 64K-aligned */
        for (i = 0; i < 4; i++) {
            DWORD e = g_cache[i];
            if ((e & 0xFFFF0000) == h && (e & 3)) return (e & 3) == 1;
        }
    }

    n = api->GetModuleFileNameA(mod, path, PATH_MAX_A);
    if (n == 0 || n >= PATH_MAX_A) return 1;         /* unsure: treat as game code */
    p = path;
    if (api->GetLongPathNameA) {
        n = api->GetLongPathNameA(path, longpath, PATH_BUF);
        if (n && n < PATH_BUF) p = longpath;
    }

    verdict = 1;
    for (i = 0; i < g_dirlen; i++) {
        if (!p[i] || lower(p[i]) != lower(g_dir[i])) { verdict = 2; break; }
    }

    if ((h & 0xFFFF) == 0)
        g_cache[g_next++ & 3] = h | verdict;         /* one aligned write: safe if threads race */
    return verdict == 1;
}

LONG WINAPI core_veh(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != STATUS_INTEGER_DIVIDE_BY_ZERO && code != STATUS_INTEGER_OVERFLOW
        && !(g_x87 && code >= STATUS_FLOAT_FIRST && code <= STATUS_FLOAT_LAST))
        return 0;                                    /* EXCEPTION_CONTINUE_SEARCH */
    if (g_dirlen && !is_game_code(ep->ExceptionRecord->ExceptionAddress))
        return 0;
    return g_filter(ep);
}

int core_setup(const CORE_API *a, const BYTE *exe, const BYTE *divtrap, int *which)
{
    api = a;
    *which = -1;

    /* Which supported build is this, and is it free of our EXE patch? */
    if (!exe || exe[0] != 'M' || exe[1] != 'Z') return SETUP_NOT_SUPPORTED;
    const BYTE *pe = exe + rd32(exe + 0x3C);
    if (rd32(pe) != 0x00004550) return SETUP_NOT_SUPPORTED;
    const GAME *g = 0;
    int known_but_patched = 0;
    for (int i = 0; i < core_game_count; i++) {
        if (rd32(pe + 8) != core_games[i].timestamp) continue;
        if (rd32(pe + 24 + 16) == core_games[i].entry_rva && rd32(pe + 24 + 56) == core_games[i].image_size) {
            g = &core_games[i]; *which = i; break;
        }
        known_but_patched = 1;
    }
    if (!g) return known_but_patched ? SETUP_EXE_ALREADY_PATCHED : SETUP_NOT_SUPPORTED;

    /* The game's own division filter */
    const BYTE *fmod = g->filter_in_divtrap ? divtrap : exe;
    if (!fmod) return SETUP_NO_FILTER_MODULE;
    if (!bytes_equal(fmod + g->filter_rva, g->filter_start, 32))
        return SETUP_FILTER_UNKNOWN;
    g_filter = (FILTER)(fmod + g->filter_rva);
    g_x87 = g->handles_x87;

    /* Game folder = folder of the EXE, in long-path form. If anything fails,
     * the location check stays off and every handled fault goes to the filter. */
    g_dirlen = 0;
    if (api->GetModuleHandleExA && api->GetModuleFileNameA) {
        char tmp[PATH_BUF];
        DWORD n = api->GetModuleFileNameA((HMODULE)exe, tmp, PATH_MAX_A);
        if (n && n < PATH_MAX_A) {
            DWORD m = api->GetLongPathNameA ? api->GetLongPathNameA(tmp, g_dir, PATH_BUF) : 0;
            if (!m || m >= PATH_BUF) copy_str(g_dir, tmp, PATH_BUF);
            DWORD last = 0;
            for (DWORD i = 0; g_dir[i]; i++) if (g_dir[i] == '\\') last = i + 1;
            g_dirlen = last;
        }
    }
    return SETUP_OK;
}

const char *core_game_dir(void) { return g_dirlen ? g_dir : ""; }
