// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A diagnosis on WARP (to drop): Maul RHI's test/d3d12_debug.c without
// GPU-based validation, the debug layer's messages watched the same way
// (its file defines the interfaces' identifiers).

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <directx/d3d12.h>
#include <directx/d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

void mvfxDiagWatchWithoutGbv(void);
uint32_t mvfxDiagErrors(void);

static uint32_t s_errors;

static void __stdcall OnMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity,
                                D3D12_MESSAGE_ID id, LPCSTR description, void* context)
{
    (void)category;
    (void)context;
    if (severity <= D3D12_MESSAGE_SEVERITY_ERROR)
    {
        printf("FAIL: D3D12 debug layer, message %d: %s\n", (int)id, description);
        ++s_errors;
    }
}

void mvfxDiagWatchWithoutGbv(void)
{
    printf("debug layer on, GPU-based validation off\n");
    ID3D12Debug* debug = NULL;
    if (FAILED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug)))
    {
        printf("FAIL: no D3D12 debug layer\n");
        ++s_errors;
        return;
    }
    ID3D12Debug_EnableDebugLayer(debug);
    ID3D12Debug1* validation = NULL;
    if (SUCCEEDED(ID3D12Debug_QueryInterface(debug, &IID_ID3D12Debug1, (void**)&validation)))
    {
        ID3D12Debug1_SetEnableGPUBasedValidation(validation, FALSE);
        ID3D12Debug1_Release(validation);
    }
    ID3D12Debug_Release(debug);
    IDXGIFactory4* factory = NULL;
    if (FAILED(CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&factory)))
    {
        return;
    }
    IDXGIAdapter1* adapter = NULL;
    for (UINT i = 0; IDXGIFactory4_EnumAdapters1(factory, i, &adapter) == S_OK; ++i)
    {
        // Held until the process ends, so that the library opens this
        // device, watched.
        ID3D12Device* device = NULL;
        ID3D12InfoQueue1* callbacks = NULL;
        if (SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_12_0,
                                        &IID_ID3D12Device, (void**)&device)) &&
            SUCCEEDED(
                ID3D12Device_QueryInterface(device, &IID_ID3D12InfoQueue1, (void**)&callbacks)))
        {
            DWORD cookie = 0;
            (void)ID3D12InfoQueue1_RegisterMessageCallback(
                callbacks, OnMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, NULL, &cookie);
            ID3D12InfoQueue1_Release(callbacks);
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory4_Release(factory);
}

uint32_t mvfxDiagErrors(void)
{
    return s_errors;
}
