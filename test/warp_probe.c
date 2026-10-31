// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A throwaway probe for #65 (not to merge): at exit, the path and file
// version of the d3d10warp.dll the process loaded, to show which WARP ran.

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static void Report(void)
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
    void* block = size > 0 ? malloc(size) : NULL;
    VS_FIXEDFILEINFO* info = NULL;
    UINT length = 0;
    if (block != NULL && GetFileVersionInfoA(path, 0, size, block) &&
        VerQueryValueA(block, "\\", (void**)&info, &length) && info != NULL)
    {
        version[0] = HIWORD(info->dwFileVersionMS);
        version[1] = LOWORD(info->dwFileVersionMS);
        version[2] = HIWORD(info->dwFileVersionLS);
        version[3] = LOWORD(info->dwFileVersionLS);
    }
    free(block);
    printf("warp probe: d3d10warp.dll %s, file version %u.%u.%u.%u\n",
           module != NULL ? path : "(not loaded)", version[0], version[1], version[2], version[3]);
    fflush(stdout);
}

__attribute__((constructor)) static void Arm(void)
{
    atexit(Report);
}
