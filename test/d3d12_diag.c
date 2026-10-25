// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A diagnosis on WARP (to drop): which WARP a process loaded, and its
// version; the stack of a fault, each frame's module and symbol.

#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// dbghelp.h needs windows.h's types first.
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on

#pragma comment(lib, "version.lib")
#pragma comment(lib, "dbghelp.lib")

void mvfxDiagWarp(void);
void mvfxDiagCatch(void);
int mvfxDiagOnBigStack(int (*body)(void*), void* argument);

typedef struct BigStack
{
    int (*body)(void*);
    void* argument;
    int result;
} BigStack;

static DWORD WINAPI RunBigStack(LPVOID parameter)
{
    BigStack* run = parameter;
    run->result = run->body(run->argument);
    return 0;
}

// Runs body on a thread of 8 MB of stack, against a main thread's 1 MB,
// when MVFX_DIAG_THREAD is set; on the calling thread otherwise.
int mvfxDiagOnBigStack(int (*body)(void*), void* argument)
{
    const char* asked = getenv("MVFX_DIAG_THREAD");
    if (asked == NULL || asked[0] == '\0')
    {
        return body(argument);
    }
    printf("on a thread of 8 MB of stack\n");
    BigStack run = {.body = body, .argument = argument, .result = 1};
    HANDLE thread = CreateThread(NULL, (SIZE_T)8 << 20u, RunBigStack, &run,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (thread == NULL)
    {
        printf("FAIL: no thread\n");
        return 1;
    }
    (void)WaitForSingleObject(thread, INFINITE);
    (void)CloseHandle(thread);
    return run.result;
}

static void PrintFrame(DWORD64 address)
{
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)address, &module))
    {
        (void)GetModuleFileNameA(module, name, MAX_PATH);
    }
    const char* file = strrchr(name, '\\');
    printf("  %s+0x%llx", file != NULL ? file + 1 : name,
           (unsigned long long)(address - (DWORD64)(uintptr_t)module));
    union
    {
        SYMBOL_INFO info;
        char bytes[sizeof(SYMBOL_INFO) + 256];
    } symbol;
    memset(&symbol, 0, sizeof symbol);
    symbol.info.SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol.info.MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, &symbol.info))
    {
        printf(" %s+0x%llx", symbol.info.Name, (unsigned long long)displacement);
    }
    printf("\n");
}

static volatile LONG s_reported;

static LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if ((code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000374u &&
         code != EXCEPTION_STACK_OVERFLOW) ||
        InterlockedIncrement(&s_reported) > 4)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    printf("exception %08lx on thread %lu\n", (unsigned long)code,
           (unsigned long)GetCurrentThreadId());
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
    {
        ULONG_PTR kind = info->ExceptionRecord->ExceptionInformation[0];
        printf("  %s %p\n",
               kind == 1   ? "writing"
               : kind == 8 ? "executing"
                           : "reading",
               (void*)info->ExceptionRecord->ExceptionInformation[1]);
    }
    (void)SymRefreshModuleList(GetCurrentProcess());
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame;
    memset(&frame, 0, sizeof frame);
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    for (int32_t i = 0;
         i < 40 &&
         StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame,
                     &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL) &&
         frame.AddrPC.Offset != 0;
         ++i)
    {
        PrintFrame(frame.AddrPC.Offset);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void mvfxDiagCatch(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    (void)SymInitialize(GetCurrentProcess(), NULL, TRUE);
    (void)AddVectoredExceptionHandler(1, OnException);
}

void mvfxDiagWarp(void)
{
    HMODULE warp = GetModuleHandleA("d3d10warp.dll");
    char path[MAX_PATH] = "not loaded";
    if (warp != NULL)
    {
        (void)GetModuleFileNameA(warp, path, MAX_PATH);
    }
    printf("WARP: %s", path);
    DWORD handle = 0;
    DWORD size = warp != NULL ? GetFileVersionInfoSizeA(path, &handle) : 0;
    static char info[65536];
    VS_FIXEDFILEINFO* fixed = NULL;
    UINT length = 0;
    if (size > 0 && size <= sizeof info && GetFileVersionInfoA(path, 0, size, info) &&
        VerQueryValueA(info, "\\", (void**)&fixed, &length) && fixed != NULL)
    {
        printf(", version %lu.%lu.%lu.%lu", (unsigned long)HIWORD(fixed->dwFileVersionMS),
               (unsigned long)LOWORD(fixed->dwFileVersionMS),
               (unsigned long)HIWORD(fixed->dwFileVersionLS),
               (unsigned long)LOWORD(fixed->dwFileVersionLS));
    }
    printf("\n");
}
