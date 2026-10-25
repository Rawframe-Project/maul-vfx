// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A diagnosis on WARP (to drop): which WARP a process loaded, and its
// version.

#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>

#pragma comment(lib, "version.lib")

void mvfxDiagWarp(void);

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
