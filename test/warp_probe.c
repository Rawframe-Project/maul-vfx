// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A throwaway probe for #65 (not to merge): the d3d10warp.dll the process
// loaded, its path and file version, at exit and at a crash, with the
// crash's code and the module it faulted in.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <dbghelp.h>

static void Report(const char* when)
{
    HMODULE module = GetModuleHandleA("d3d10warp.dll");
    char path[MAX_PATH] = "";
    if (module != NULL)
    {
        GetModuleFileNameA(module, path, MAX_PATH);
    }
    unsigned version[4] = {0, 0, 0, 0};
    DWORD handle = 0;
    DWORD size = path[0] != 0 ? GetFileVersionInfoSizeA(path, &handle) : 0;
    static char block[1 << 16];
    VS_FIXEDFILEINFO* info = NULL;
    UINT length = 0;
    if (size > 0 && size <= sizeof block && GetFileVersionInfoA(path, 0, size, block) &&
        VerQueryValueA(block, "\\", (void**)&info, &length) && info != NULL)
    {
        version[0] = HIWORD(info->dwFileVersionMS);
        version[1] = LOWORD(info->dwFileVersionMS);
        version[2] = HIWORD(info->dwFileVersionLS);
        version[3] = LOWORD(info->dwFileVersionLS);
    }
    printf("warp probe (%s): d3d10warp.dll %s, file version %u.%u.%u.%u\n", when,
           module != NULL ? path : "(not loaded)", version[0], version[1], version[2], version[3]);
    fflush(stdout);
}

static void AtExit(void)
{
    Report("exit");
}

// A crash's code and module, then the WARP loaded; the search goes on.
static LONG CALLBACK Crashed(EXCEPTION_POINTERS* pointers)
{
    DWORD code = pointers->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW &&
        code != 0xC0000374u)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    void* at = pointers->ExceptionRecord->ExceptionAddress;
    HMODULE module = NULL;
    char name[MAX_PATH] = "(none)";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)at, &module))
    {
        GetModuleFileNameA(module, name, MAX_PATH);
    }
    printf("warp probe: exception 0x%08lx at %p, %s+0x%llx\n", (unsigned long)code, at, name,
           (unsigned long long)((const char*)at - (const char*)module));
    fflush(stdout);
    if (code != EXCEPTION_STACK_OVERFLOW)
    {
        // The stack from here, the faulting frames among them: each
        // frame's module and offset, and its symbol where one is found.
        void* frames[48];
        USHORT count = RtlCaptureStackBackTrace(0, 48, frames, NULL);
        HANDLE process = GetCurrentProcess();
        static char storage[sizeof(SYMBOL_INFO) + 256];
        for (USHORT f = 0; f < count; ++f)
        {
            HMODULE owner = NULL;
            char file[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)frames[f], &owner))
            {
                GetModuleFileNameA(owner, file, MAX_PATH);
            }
            const char* base = strrchr(file, '\\') != NULL ? strrchr(file, '\\') + 1 : file;
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)storage;
            memset(storage, 0, sizeof storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            bool named = SymFromAddr(process, (DWORD64)frames[f], &displacement, symbol);
            printf("warp probe: frame %2u %s+0x%llx %s%s0x%llx\n", (unsigned)f, base,
                   (unsigned long long)((const char*)frames[f] - (const char*)owner),
                   named ? symbol->Name : "", named ? "+" : "",
                   (unsigned long long)displacement);
        }
        fflush(stdout);
        Report("crash");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

__attribute__((constructor)) static void Arm(void)
{
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash != NULL)
    {
        strcpy(slash + 1, "d3d10warp.dll");
    }
    printf("warp probe (start): beside the test %s %s\n", exe,
           GetFileAttributesA(exe) != INVALID_FILE_ATTRIBUTES ? "exists" : "does not exist");
    fflush(stdout);
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    AddVectoredExceptionHandler(1, Crashed);
    atexit(AtExit);
}
