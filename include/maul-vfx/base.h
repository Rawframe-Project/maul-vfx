// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The base of the Maul VFX API: the library version, the export and
// attribute macros, and the result codes every fallible function
// returns.

#ifndef MAUL_VFX_BASE_H
#define MAUL_VFX_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The library version. CMake reads it from here.
#define MVFX_VERSION_MAJOR 0
#define MVFX_VERSION_MINOR 0
#define MVFX_VERSION_PATCH 1

// The version a root def carries: the major and minor version of the
// headers a program was built with. The ABI holds within a minor version.
#define MVFX_ABI_VERSION ((MVFX_VERSION_MAJOR << 16) | MVFX_VERSION_MINOR)

// MVFX_API marks the public functions: dllexport or dllimport in a
// shared Windows build (maul_vfx_EXPORTS is defined while building
// the library), default visibility in a shared build elsewhere.
#if defined(MAUL_VFX_SHARED) && defined(_WIN32)
#if defined(maul_vfx_EXPORTS)
#define MVFX_API __declspec(dllexport) extern
#else
#define MVFX_API __declspec(dllimport) extern
#endif
#elif defined(MAUL_VFX_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define MVFX_API __attribute__((visibility("default"))) extern
#else
#define MVFX_API extern
#endif

// MVFX_NODISCARD marks a function whose result must be read: every
// function that returns a status. The attribute is standard in C23 and
// C++17 and left out for older dialects. MSVC keeps __cplusplus at
// 199711L unless /Zc:__cplusplus is given, so _MSVC_LANG is read too.
#if (defined(__cplusplus) && __cplusplus >= 201703L) ||                                            \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define MVFX_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define MVFX_NODISCARD [[nodiscard]]
#else
#define MVFX_NODISCARD
#endif

    // The status a fallible function returns. Zero is success, positive
    // values are outcomes that are not errors, negative values are errors.
    // The type has a fixed width so that result structs have one layout in
    // C and C++.
    typedef int32_t mvfxResult;
    enum
    {
        // The call did what was asked.
        mvfx_success = 0,
        // An argument is invalid: a null pointer where one is required, a
        // value out of range, a def not made by its default function.
        mvfx_errorInvalid = -1,
        // A caller buffer is too small for the result, or the allocator
        // failed.
        mvfx_errorCapacity = -2,
        // A named limit the caller set was reached: the work it bounds was
        // not done.
        mvfx_errorLimit = -3,
        // A def was built against headers of another major or minor
        // version.
        mvfx_errorVersion = -4,
        // An id names an object that has been destroyed since it was
        // handed out.
        mvfx_errorStale = -5,
        // The object is in use by another and cannot be destroyed or
        // changed now.
        mvfx_errorBusy = -6,
        // The call is not allowed in the object's current state.
        mvfx_errorState = -7,
    };

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's allocation functions.
    typedef struct mvfxAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } mvfxAllocator;

    // A version: major, minor and patch.
    typedef struct mvfxVersion
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    } mvfxVersion;

    /// Returns the version of the library that was linked, which may differ
    /// from the MVFX_VERSION macros a program was compiled with.
    ///
    /// @return The library version.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxVersion mvfxGetVersion(void);

    // The seed of a hash that starts from nothing.
#define MVFX_HASH_INIT 0xCBF29CE484222325ull

    /// Hashes bytes into a 64-bit value, the hash the determinism checks
    /// and effect identities are built on: eight bytes a round, read in the
    /// host's byte order, xored in, multiplied by an odd constant and
    /// folded, the leftover bytes one at a time. Its constants are frozen.
    ///
    /// @param seed       MVFX_HASH_INIT, or a hash to continue.
    /// @param data       The bytes. May be NULL when byteCount is 0.
    /// @param byteCount  The number of bytes, at least 0.
    /// @return The hash.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API uint64_t mvfxHash64(uint64_t seed, const void* data, int32_t byteCount);

    /// Returns the name of a result code, for diagnostics.
    ///
    /// @param result  Any value; an unknown one is named as such.
    /// @return A static, NUL-terminated string such as "mvfx_errorCapacity".
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API const char* mvfxResultName(mvfxResult result);

#ifdef __cplusplus
}
#endif

#endif
