/* Loads the real combined FSDIVFIX.DLL (inside the EXE given by -DEXE) with a minimal PE loader (relocated to a base it
 * didn't ask for, imports bound to fake KERNEL32 functions), runs its DllMain,
 * reads its Linkage export the way FLTSIM95.EXE does, and drives the handler it
 * registered with DIVTRAP.DLL's real filter. Freestanding Linux i386, no libc. */
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL; typedef long LONG; typedef void *HMODULE;
#define WINAPI __attribute__((stdcall))
typedef struct { DWORD code, flags; void *rec; void *addr; } EXREC;
typedef struct { EXREC *rec; void *ctx; } EXPTRS;
typedef LONG (WINAPI *HANDLER)(EXPTRS *);

static long sys1(long n, long a) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a) : "memory"); return r; }
static long sys3(long n, long a, long b, long c) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r; }
static void out(const char *s) { long n = 0; while (s[n]) n++; sys3(4, 1, (long)s, n); }
static int  slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int  seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scpy(char *d, const char *s) { while ((*d++ = *s++)) ; }
static DWORD rd32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }
static void wr32(BYTE *p, DWORD v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void *map_at(DWORD addr, DWORD size) { long a[6] = { (long)addr, (long)size, 7, 0x32, -1, 0 }; return (void *)sys1(90, (long)a); }
static BYTE filebuf[0x80000];
static void load_file(const char *path)
{
    long fd = sys3(5, (long)path, 0, 0), n = 0, r;
    if (fd < 0) { out("cannot read "); out(path); out("\n"); sys1(1, 2); }
    while ((r = sys3(3, fd, (long)filebuf + n, sizeof filebuf - n)) > 0) n += r;
    sys1(6, fd);
}
static int problems;
static void check(int ok, const char *what) { out(ok ? "   ok   " : "   BAD  "); out(what); out("\n"); problems += !ok; }

/* ---- fake KERNEL32 ---- */
#define GD "C:\\Microsoft Games\\Flight Simulator 95"
static DWORD dll_base, dll_size; static int pinned, disabled_thread_calls, avh_first, avh_calls, gmh_calls;
static HANDLER registered;
static HMODULE WINAPI k_GetModuleHandleA(const char *n)
{
    gmh_calls++;
    if (!n) return (HMODULE)0x400000;
    if (seq(n, "DIVTRAP.DLL")) return FS95 ? (HMODULE)0x83000000 : 0;
    return 0;
}
static BOOL WINAPI k_GetModuleHandleExA(DWORD flags, const char *addr, HMODULE *m)
{
    DWORD a = (DWORD)addr;
    if (a >= dll_base && a < dll_base + dll_size) { if (flags & 1) pinned++; *m = (HMODULE)dll_base; return 1; }
    if (a >= 0x206B0000 && a < 0x206F4000) { *m = (HMODULE)0x206B0000; return 1; }   /* G3D.DLL */
    if (a >= 0x75000000 && a < 0x75100000) { *m = (HMODULE)0x75000000; return 1; }   /* KERNEL32 */
    return 0;
}
static DWORD WINAPI k_GetModuleFileNameA(HMODULE h, char *buf, DWORD n)
{
    const char *p = (DWORD)h == 0x400000 ? GD "\\FLTSIM95.EXE" : (DWORD)h == 0x206B0000 ? GD "\\G3D.DLL"
                  : (DWORD)h == 0x75000000 ? "C:\\WINDOWS\\System32\\KERNEL32.DLL" : 0;
    if (!p || (DWORD)slen(p) >= n) return 0;
    scpy(buf, p); return slen(p);
}
static DWORD WINAPI k_GetLongPathNameA(const char *in, char *o, DWORD n) { if ((DWORD)slen(in) >= n) return slen(in) + 1; scpy(o, in); return slen(in); }
static void *WINAPI k_AddVectoredExceptionHandler(DWORD first, HANDLER h) { avh_calls++; avh_first = first; registered = h; return (void *)1; }
static BOOL WINAPI k_DisableThreadLibraryCalls(HMODULE h) { disabled_thread_calls += (DWORD)h == dll_base; return 1; }
static const struct { const char *name; void *fn; } k32[] = {
    { "GetModuleHandleA", k_GetModuleHandleA }, { "GetModuleHandleExA", k_GetModuleHandleExA },
    { "GetModuleFileNameA", k_GetModuleFileNameA }, { "GetLongPathNameA", k_GetLongPathNameA },
    { "AddVectoredExceptionHandler", k_AddVectoredExceptionHandler }, { "DisableThreadLibraryCalls", k_DisableThreadLibraryCalls },
};

/* ---- minimal PE loader ---- */
static int unresolved;
static BYTE *load_pe(const char *path, DWORD at, int bind_imports)
{
    load_file(path);
    const BYTE *pe = filebuf + rd32(filebuf + 0x3C);
    const BYTE *opt = pe + 24;
    DWORD pref = rd32(opt + 28), size = rd32(opt + 56), hdrs = rd32(opt + 60);
    if (!at) at = pref;
    BYTE *img = map_at(at, size);
    for (DWORD i = 0; i < hdrs; i++) img[i] = filebuf[i];
    int nsec = pe[6] | (pe[7] << 8);
    const BYTE *sh = opt + (pe[20] | (pe[21] << 8));
    for (int s = 0; s < nsec; s++, sh += 40)
        for (DWORD i = 0; i < rd32(sh + 16) && i < rd32(sh + 8); i++) img[rd32(sh + 12) + i] = filebuf[rd32(sh + 20) + i];
    DWORD delta = at - pref, rrva = rd32(opt + 96 + 8 * 5), rsz = rd32(opt + 96 + 8 * 5 + 4);
    for (DWORD p = rrva; delta && p < rrva + rsz; ) {           /* base relocations */
        DWORD page = rd32(img + p), bs = rd32(img + p + 4);
        if (!bs) break;
        for (DWORD i = 8; i < bs; i += 2) {
            DWORD e = img[p + i] | (img[p + i + 1] << 8);
            if ((e >> 12) == 3) wr32(img + page + (e & 0xFFF), rd32(img + page + (e & 0xFFF)) + delta);
        }
        p += bs;
    }
    if (bind_imports) {
        for (DWORD d = rd32(opt + 96 + 8); rd32(img + d + 12); d += 20) {   /* import descriptors */
            DWORD ilt = rd32(img + d) ? rd32(img + d) : rd32(img + d + 16), iat = rd32(img + d + 16);
            for (DWORD i = 0; rd32(img + ilt + 4 * i); i++) {
                const char *nm = (const char *)img + rd32(img + ilt + 4 * i) + 2; void *fn = 0;
                for (unsigned k = 0; k < sizeof k32 / sizeof k32[0]; k++) if (seq(nm, k32[k].name)) fn = k32[k].fn;
                if (!fn) { unresolved++; out("   unresolved import "); out(nm); out("\n"); }
                wr32(img + iat + 4 * i, (DWORD)fn);
            }
        }
    }
    return img;
}
static void *get_export(BYTE *img, const char *name)          /* what GetProcAddress does */
{
    const BYTE *opt = img + rd32(img + 0x3C) + 24;
    DWORD e = rd32(opt + 96);
    DWORD nn = rd32(img + e + 24), af = rd32(img + e + 28), anp = rd32(img + e + 32), ao = rd32(img + e + 36);
    for (DWORD i = 0; i < nn; i++)
        if (seq((const char *)img + rd32(img + anp + 4 * i), name)) {
            DWORD ord = img[ao + 2 * i] | (img[ao + 2 * i + 1] << 8);
            return img + rd32(img + af + 4 * ord);
        }
    return 0;
}

/* ---- faults ---- */
static BYTE ctx[0x2CC]; static BYTE *code_page;
static LONG fault(DWORD code, DWORD addr)
{
    for (int i = 0; i < 0x2CC; i++) ctx[i] = 0;
    code_page[0] = 0xF7; code_page[1] = 0xFB;                       /* idiv ebx */
    wr32(ctx + 0xB0, 0xD2FF0474); wr32(ctx + 0xA8, 0x2FD); wr32(ctx + 0xA4, (DWORD)-992); wr32(ctx + 0xB8, (DWORD)code_page);
    EXREC r = { code, 0, 0, (void *)addr }; EXPTRS ep = { &r, ctx };
    return registered(&ep);
}
static int handled(LONG r) { return r == -1 && rd32(ctx + 0xB0) == 0x80000001 && rd32(ctx + 0xA8) == 0x7FFFFFFF && rd32(ctx + 0xB8) == (DWORD)code_page + 2; }
static int passed(LONG r)  { return r == 0 && rd32(ctx + 0xB0) == 0xD2FF0474 && rd32(ctx + 0xB8) == (DWORD)code_page; }

void _start(void)
{
    load_pe(EXE, 0, 0);                               /* image at 0x400000 */
    if (FS95) load_pe("in/DIVTRAP.DLL", 0, 0);        /* at 0x83000000 */
    out("inside "); out(EXE); out("\n");
    code_page = map_at(0x50000000, 0x1000);
    dll_base = 0x23450000; dll_size = 0x5000;          /* NOT its preferred base: relocations must work */
    BYTE *dll = load_pe("FSDIVFIX.DLL", dll_base, 1);

    out("loading the real FSDIVFIX.DLL\n");
    check(unresolved == 0, "all imports are the six expected KERNEL32 functions");
    DWORD *linkage = get_export(dll, "Linkage");
    check(linkage != 0, "exports Linkage (GetProcAddress-style lookup)");
    int zeros = 1; for (int i = 1; i < 12; i++) zeros &= linkage[i] == 0;
    check(linkage && linkage[0] == 0x46534446 && zeros, "Linkage = { ID 46534446, rest 0 }: FS95 skips init, shutdown, imports");
    check(linkage[5] == 0, "Linkage[5] (the field FS95 reads at +14h) is 0");

    const BYTE *opt = dll + rd32(dll + 0x3C) + 24;
    BOOL (WINAPI *entry)(HMODULE, DWORD, void *) = (void *)(dll + rd32(opt + 16));
    check(entry((HMODULE)dll_base, 1, 0) == 1, "DllMain(PROCESS_ATTACH) returns TRUE");
    check(pinned == 1, "pinned itself in memory (survives FS95's FreeLibrary)");
    check(disabled_thread_calls == 1, "DisableThreadLibraryCalls on itself");
    check(avh_calls == 1 && avh_first == 1 && registered && (DWORD)registered >= dll_base && (DWORD)registered < dll_base + dll_size,
          "registered one vectored handler, first in line, inside the DLL");
    entry((HMODULE)dll_base, 1, 0); entry((HMODULE)dll_base, 2, 0); entry((HMODULE)dll_base, 0, 0);
    check(avh_calls == 1 && pinned == 1, "repeat attach / thread / detach calls change nothing");

    out("faults through the registered handler (DIVTRAP's real filter)\n");
    check(handled(fault(0xC0000095, 0x206B1A46)), "overflow in G3D.DLL: EAX=80000001 EDX=7FFFFFFF, EIP+2");
    check(handled(fault(0xC0000094, 0x00401000)), "divide by zero in the EXE: handled");
    check(passed(fault(0xC0000094, 0x75001000)), "divide fault in KERNEL32: passed on");
    check(passed(fault(0xC0000005, 0x206B1000)), "access violation: passed on");
    check(passed(fault(0xE06D7363, 0x206B1000)), "C++ exception: passed on");
    check(handled(fault(0xC0000094, 0x7F000000)), "address outside any module: handled");

    out(problems ? "\nPROBLEMS\n" : "\nALL PASSED\n");
    sys1(1, problems);
}
