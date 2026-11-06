/* Which path does core_setup take for a given EXE? (prints the table entry or fallback) */
#include "../core.h"
static long sys1(long n, long a) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a) : "memory"); return r; }
static long sys3(long n, long a, long b, long c) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r; }
static void out(const char *s) { long n = 0; while (s[n]) n++; sys3(4, 1, (long)s, n); }
static void *map_at(DWORD addr, DWORD size) { long a[6] = { (long)addr, (long)size, 7, 0x32, -1, 0 }; return (void *)sys1(90, (long)a); }
static BYTE fb[0x80000];
static DWORD rd32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }
static BOOL WINAPI gmhex(DWORD f, const char *a, HMODULE *m) { (void)f; (void)a; (void)m; return 0; }
static DWORD WINAPI gmfn(HMODULE h, char *b, DWORD n) { (void)h; (void)b; (void)n; return 0; }
void _start(void)
{
    long fd = sys3(5, (long)EXE, 0, 0), n = 0, r;
    while ((r = sys3(3, fd, (long)fb + n, sizeof fb - n)) > 0) n += r;
    const BYTE *pe = fb + rd32(fb + 0x3C);
    BYTE *img = map_at(rd32(pe + 24 + 28), rd32(pe + 24 + 56));
    for (DWORD i = 0; i < rd32(pe + 24 + 60); i++) img[i] = fb[i];
    const BYTE *sh = pe + 24 + (pe[20] | (pe[21] << 8));
    for (int s = 0; s < (pe[6] | (pe[7] << 8)); s++, sh += 40)
        for (DWORD i = 0; i < rd32(sh + 16) && i < rd32(sh + 8); i++) img[rd32(sh + 12) + i] = fb[rd32(sh + 20) + i];
    CORE_API api = { gmhex, gmfn, 0 }; int which;
    int res = core_setup(&api, img, 0, &which);
    out("   core_setup: "); out(res == SETUP_OK ? "active, " : "INACTIVE");
    if (res == SETUP_OK) out(which >= 0 ? core_games[which].name : which == -3 ? "build not in the list: handler found via its __except filter" : "?");
    out("\n");
    sys1(1, res);
}
