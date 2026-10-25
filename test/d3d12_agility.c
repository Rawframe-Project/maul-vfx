// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A diagnosis on WARP (to drop): rhi_kernels on the Agility SDK's runtime
// in D3D12/ beside it, as the harness suites run through Maul RHI's
// test/d3d12_debug.c.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

__declspec(dllexport) extern const UINT D3D12SDKVersion;
__declspec(dllexport) extern const char* D3D12SDKPath;

__declspec(dllexport) const UINT D3D12SDKVersion = 619;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
