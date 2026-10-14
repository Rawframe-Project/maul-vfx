// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grids: values the host gives at the points of a regular 3D lattice, one
// to four components each, sampled trilinearly. A vector field force
// reads a grid of three components spanning a box; the host may give a
// grid new values between steps, as an animated field.

#ifndef MAUL_VFX_GRID_H
#define MAUL_VFX_GRID_H

#include "maul-vfx/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most points a grid has along an axis.
#define MVFX_MAX_GRID_SIZE 4096

// The most points a grid has.
#define MVFX_MAX_GRID_POINTS (1 << 24)

    // Names a grid of a context: a 1-based slot, 0 for the null id, and a
    // generation that tells a live grid from earlier occupants of its slot.
    typedef struct mvfxGridId
    {
        uint32_t index1;
        uint32_t generation;
    } mvfxGridId;

    // How a grid keeps its values.
    typedef uint8_t mvfxGridFormat;
    enum
    {
        // As binary32.
        mvfx_gridFloat32 = 0,
        // As binary16, rounded to nearest with ties to even: half the memory,
        // each value at most 65,504 in size.
        mvfx_gridFloat16 = 1,
    };

    // How a grid is made. Build it with mvfxDefaultGridDef. The grid copies
    // its values: the array may be released once it is created.
    typedef struct mvfxGridDef
    {
        uint32_t cookie;
        // The points along X, Y and Z, each from 1 to MVFX_MAX_GRID_SIZE, at
        // most MVFX_MAX_GRID_POINTS in all; one along an axis is constant
        // along it.
        int32_t size[3];
        // The values a point has, from 1 to 4.
        int32_t components;
        mvfxGridFormat format;
        // components values a point, X fastest, then Y, then Z; each finite
        // and at most MVFX_MAX_VALUE in size (65,504 as binary16).
        const float* values;
    } mvfxGridDef;

// The cookie of a grid def, which its default sets and mvfxCreateGrid
// checks.
#define MVFX_GRID_DEF_COOKIE 0x6D766772u

    /// Returns the default grid def: one point of one component, binary32,
    /// with no values yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxGridDef mvfxDefaultGridDef(void);

    /// Creates a grid: checks the def and copies its values.
    ///
    /// @param context  The context that will own the grid.
    /// @param def      The def, from mvfxDefaultGridDef.
    /// @param gridOut  Receives the grid's id; the null id on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def
    ///         without its cookie, a size or count out of range or a value
    ///         out of range, which counts as misuse when the context is
    ///         given; `mvfx_errorLimit` when the context holds its limit of
    ///         grids; `mvfx_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateGrid(mvfxContext* context, const mvfxGridDef* def,
                                                      mvfxGridId* gridOut);

    /// Destroys a grid.
    ///
    /// @param context  The context that owns the grid.
    /// @param grid     The grid.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         grid; `mvfx_errorBusy` while an effect reads it;
    ///         `mvfx_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyGrid(mvfxContext* context, mvfxGridId grid);

    /// Returns whether an id names a live grid of the context. A stale id is
    /// not misuse.
    ///
    /// @param context  The context.
    /// @param grid     Any id.
    /// @return Whether the grid is alive.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API bool mvfxGrid_IsValid(const mvfxContext* context, mvfxGridId grid);

    /// Gives a grid new values for the same points, as an animated field;
    /// steps after read them.
    ///
    /// @param context  The context that owns the grid.
    /// @param grid     The grid.
    /// @param values   The values, as in the grid def.
    /// @param count    How many: the grid's points times its components.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         grid; `mvfx_errorInvalid` for a NULL context or values, a count
    ///         not the grid's or a value out of range, which counts as misuse
    ///         when the context is given (the grid is then unchanged).
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxGrid_SetValues(mvfxContext* context, mvfxGridId grid,
                                                          const float* values, int64_t count);

#ifdef __cplusplus
}
#endif

#endif
