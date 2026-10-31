/* FSDIVFIX: shared definitions.
 * core.c holds all the logic. dllmain.c connects it to Windows; the tests
 * connect it to fake Windows functions so it can be tested anywhere. */
#ifndef FSDIV_CORE_H
#define FSDIV_CORE_H

#define WINAPI __attribute__((stdcall))

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;
typedef int            BOOL;
typedef long           LONG;
typedef void          *HMODULE;

typedef struct {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    void *ExceptionRecord;
    void *ExceptionAddress;
} EXCEPTION_RECORD;

typedef struct {
    EXCEPTION_RECORD *ExceptionRecord;
    void *ContextRecord;
} EXCEPTION_POINTERS;

typedef LONG (WINAPI *FILTER)(EXCEPTION_POINTERS *);

/* The Windows functions the core needs (filled in by dllmain.c or the tests). */
typedef struct {
    BOOL  (WINAPI *GetModuleHandleExA)(DWORD flags, const char *name, HMODULE *module);
    DWORD (WINAPI *GetModuleFileNameA)(HMODULE module, char *buf, DWORD size);
    DWORD (WINAPI *GetLongPathNameA)(const char *in, char *out, DWORD size);
} CORE_API;

/* One supported game EXE build, and where its own division filter lives. */
typedef struct {
    const char *name;         /* for the tests / documentation */
    DWORD timestamp;          /* EXE TimeDateStamp */
    DWORD entry_rva;          /* AddressOfEntryPoint (our v2/v3 EXE patches change it) */
    DWORD image_size;         /* SizeOfImage */
    int   filter_in_divtrap;  /* 1: filter is in DIVTRAP.DLL (FS95); 0: in the EXE (FS98) */
    DWORD filter_rva;         /* filter's RVA in that module */
    const BYTE *filter_start; /* its first 32 bytes (no values the loader relocates) */
    int   handles_x87;        /* the filter also handles x87 exceptions C000008D..C0000093 */
} GAME;

enum {
    SETUP_OK = 0,              /* handler ready to register */
    SETUP_NOT_SUPPORTED,       /* not a supported EXE build: do nothing */
    SETUP_EXE_ALREADY_PATCHED, /* a supported build, but with the v2/v3 EXE patch: do nothing */
    SETUP_NO_FILTER_MODULE,    /* DIVTRAP.DLL not loaded */
    SETUP_FILTER_UNKNOWN       /* the filter isn't where/what we expect */
};

extern const GAME core_games[];
extern const int  core_game_count;

/* exe_base: the EXE; divtrap_base: DIVTRAP.DLL or 0. *which receives the matched game (or -1). */
int  core_setup(const CORE_API *api, const BYTE *exe_base, const BYTE *divtrap_base, int *which);
LONG WINAPI core_veh(EXCEPTION_POINTERS *ep);

/* For tests: the game folder the core recorded ("" if the location check is off). */
const char *core_game_dir(void);

#endif
