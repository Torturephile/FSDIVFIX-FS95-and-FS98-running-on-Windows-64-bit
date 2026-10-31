/* Linux i386 test for core.c, freestanding (no libc).
 * For each supported build: maps the real EXE at 0x400000 (and the real
 * DIVTRAP.DLL at 0x83000000 for FS95), fakes the Windows functions core.c
 * uses, and checks core_setup and core_veh against the game's real filter. */
#include "../core.h"

static long sys1(long n, long a) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a) : "memory"); return r; }
static long sys3(long n, long a, long b, long c) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r; }
static void out(const char *s) { long n = 0; while (s[n]) n++; sys3(4, 1, (long)s, n); }
static int  slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int  seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scpy(char *d, const char *s) { while ((*d++ = *s++)) ; }
static void *map_at(DWORD addr, DWORD size) { long a[6] = { (long)addr, (long)size, 7, 0x32, -1, 0 }; return (void *)sys1(90, (long)a); }
static BYTE filebuf[0x80000];
static void load_file(const char *path)
{
    long fd = sys3(5, (long)path, 0, 0), n = 0, r;
    if (fd < 0) { out("cannot read "); out(path); out("\n"); sys1(1, 2); }
    while ((r = sys3(3, fd, (long)filebuf + n, sizeof filebuf - n)) > 0) n += r;
    sys1(6, fd);
}
static DWORD rd32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }
static void wr32(BYTE *p, DWORD v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static BYTE *map_pe(const char *path)
{
    load_file(path);
    const BYTE *pe = filebuf + rd32(filebuf + 0x3C);
    DWORD base = rd32(pe + 24 + 28), size = rd32(pe + 24 + 56), hdrs = rd32(pe + 24 + 60);
    BYTE *img = map_at(base, size);
    for (DWORD i = 0; i < size; i++) img[i] = 0;
    for (DWORD i = 0; i < hdrs; i++) img[i] = filebuf[i];
    int nsec = pe[6] | (pe[7] << 8);
    const BYTE *sh = pe + 24 + (pe[20] | (pe[21] << 8));
    for (int s = 0; s < nsec; s++, sh += 40)
        for (DWORD i = 0; i < rd32(sh + 16) && i < rd32(sh + 8); i++) img[rd32(sh + 12) + i] = filebuf[rd32(sh + 20) + i];
    return img;
}

/* ---- fake Windows ---- */
typedef struct { DWORD base, size; const char *path; } MOD;
static const MOD mods[] = {
    { 0x00400000, 0x21000, "C:\\Games\\FS\\FLTSIM.EXE" },
    { 0x206B0000, 0x44000, "C:\\Games\\FS\\Modules\\G3D.DLL" },
    { 0x75000000, 0x100000, "C:\\WINDOWS\\System32\\KERNEL32.DLL" },
    { 0x10000000, 0x10000, "C:\\Program Files\\Overlay\\hook.dll" },
};
#define NMODS 4
static BOOL WINAPI f_gmhex(DWORD flags, const char *addr, HMODULE *m)
{
    (void)flags;
    for (int i = 0; i < NMODS; i++)
        if ((DWORD)addr >= mods[i].base && (DWORD)addr < mods[i].base + mods[i].size) { *m = (HMODULE)mods[i].base; return 1; }
    return 0;
}
static DWORD WINAPI f_gmfn(HMODULE h, char *buf, DWORD n)
{
    for (int i = 0; i < NMODS; i++) if ((DWORD)h == mods[i].base && (DWORD)slen(mods[i].path) < n) { scpy(buf, mods[i].path); return slen(mods[i].path); }
    return 0;
}
static DWORD WINAPI f_glpn(const char *in, char *o, DWORD n) { if ((DWORD)slen(in) >= n) return slen(in) + 1; scpy(o, in); return slen(in); }

static int problems;
static void check(int ok, const char *what) { out(ok ? "   ok   " : "   BAD  "); out(what); out("\n"); problems += !ok; }

/* ---- faults ---- */
static BYTE ctx[0x2CC]; static BYTE *code_page;
enum { CW = 0x1C, EDX = 0xA8, EBX = 0xA4, EAX = 0xB0, EIP = 0xB8 };
static LONG fault(DWORD code, DWORD addr)
{
    for (int i = 0; i < 0x2CC; i++) ctx[i] = 0;
    code_page[0] = 0xF7; code_page[1] = 0xFB;                          /* idiv ebx */
    wr32(ctx + EAX, 0xD2FF0474); wr32(ctx + EDX, 0x2FD); wr32(ctx + EBX, (DWORD)-992); wr32(ctx + EIP, (DWORD)code_page);
    wr32(ctx + CW, 0xFFFF0262);                                        /* x87 control word with exceptions unmasked */
    EXCEPTION_RECORD r = { code, 0, 0, (void *)addr }; EXCEPTION_POINTERS ep = { &r, ctx };
    return core_veh(&ep);
}
static int int_handled(LONG r) { return r == -1 && rd32(ctx + EAX) == 0x80000001 && rd32(ctx + EDX) == 0x7FFFFFFF && rd32(ctx + EIP) == (DWORD)code_page + 2; }
static int x87_handled(LONG r) { return r == -1 && (rd32(ctx + CW) & 0x3F) == 0x3F && rd32(ctx + EIP) == (DWORD)code_page; }
static int untouched(LONG r)   { return r == 0 && rd32(ctx + EAX) == 0xD2FF0474 && rd32(ctx + EIP) == (DWORD)code_page && rd32(ctx + CW) == 0xFFFF0262; }

static const struct { const char *exe; int fs95; } builds[] = {
    { "in/fs95_117.exe", 1 }, { "in/fs95_119.exe", 1 },
    { "in/fs98_418.exe", 0 }, { "in/fs98_419.exe", 0 }, { "in/fs98_446.exe", 0 },
};
static BYTE hdr[0x400], bad_divtrap[0x2000];

void _start(void)
{
    BYTE *divtrap = map_pe("in/DIVTRAP.DLL");          /* at 0x83000000 */
    for (int i = 0; i < 0x2000; i++) bad_divtrap[i] = divtrap[i];
    bad_divtrap[0x1012] ^= 0xFF;
    code_page = map_at(0x50000000, 0x1000);
    CORE_API api = { f_gmhex, f_gmfn, f_glpn };
    int which;

    for (int b = 0; b < 5; b++) {
        BYTE *exe = map_pe(builds[b].exe);              /* at 0x400000 */
        const BYTE *dt = builds[b].fs95 ? divtrap : 0;
        out("== "); out(core_games[b].name); out("\n");
        check(core_setup(&api, exe, dt, &which) == SETUP_OK && which == b, "recognised; its own filter found and verified");
        check(seq(core_game_dir(), "C:\\Games\\FS\\FLTSIM.EXE"), "game folder recorded");
        check(int_handled(fault(0xC0000095, 0x206B1A46)), "integer overflow in G3D.DLL: game's result (80000001 / 7FFFFFFF, EIP+2)");
        check(int_handled(fault(0xC0000094, 0x00406525)), "divide by zero in the EXE: game's result");
        check(untouched(fault(0xC0000094, 0x75001000)), "divide fault in KERNEL32: passed on");
        check(untouched(fault(0xC0000005, 0x206B1000)), "access violation: passed on");
        if (core_games[b].handles_x87) {
            check(x87_handled(fault(0xC000008E, 0x206B2000)), "x87 divide by zero in G3D.DLL: game's FPU handler (exceptions masked, retried)");
            check(x87_handled(fault(0xC0000091, 0x00402000)), "x87 overflow in the EXE: game's FPU handler");
            check(untouched(fault(0xC000008E, 0x10001000)), "x87 fault in an overlay DLL: passed on");
        } else {
            check(untouched(fault(0xC000008E, 0x206B2000)), "x87 fault: passed on (this build's filter doesn't handle them)");
        }
        for (int i = 0; i < 0x400; i++) hdr[i] = exe[i];
        const BYTE *pe = exe + rd32(exe + 0x3C); int ep = (int)(pe - exe) + 24 + 16;
        wr32(hdr + ep, rd32(hdr + ep) + 0x1000);
        check(core_setup(&api, hdr, dt, &which) == SETUP_EXE_ALREADY_PATCHED, "same build with a changed entry point (EXE patch): stays inert");
        if (builds[b].fs95) {
            check(core_setup(&api, exe, 0, &which) == SETUP_NO_FILTER_MODULE, "DIVTRAP.DLL not loaded: stays inert");
            check(core_setup(&api, exe, bad_divtrap, &which) == SETUP_FILTER_UNKNOWN, "different DIVTRAP.DLL: stays inert");
        } else {
            exe[core_games[b].filter_rva + 5] ^= 0xFF;
            check(core_setup(&api, exe, 0, &which) == SETUP_FILTER_UNKNOWN, "filter bytes changed: stays inert");
            exe[core_games[b].filter_rva + 5] ^= 0xFF;
        }
    }
    out("== other programs\n");
    BYTE *cfs = map_pe("in/combatfs.exe");
    check(core_setup(&api, cfs, 0, &which) == SETUP_NOT_SUPPORTED, "Combat Flight Simulator EXE: stays inert");
    for (int i = 0; i < 0x400; i++) hdr[i] = cfs[i];
    hdr[0] = 'X';
    check(core_setup(&api, hdr, 0, &which) == SETUP_NOT_SUPPORTED, "not a PE image: stays inert");

    out(problems ? "\nPROBLEMS\n" : "\nALL PASSED\n");
    sys1(1, problems);
}
