// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Water surfaces: heights over a grid of cells across X and Z of an
// instance's frame, along its Y, as heightfield colliders lie. A surface
// of waves (ripples, wakes, rain) carries each cell's displacement from
// its rest level and its rate: the wave equation, its waves at the def's
// speed. The host pushes it (instance.h), reads its fields into its own
// formats and samples it at points (output.h: floating things, cameras,
// sound); it draws the grid itself.

#ifndef MAUL_VFX_WATER_H
#define MAUL_VFX_WATER_H

#include "maul-vfx/context.h"
#include "maul-vfx/gas.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The most water surfaces an effect has.
#define MVFX_MAX_WATERS 4

// The most cells a water surface has along an axis.
#define MVFX_MAX_WATER_SIZE 1024

// The least edge of a water surface's cell: a wave's update grows as its
// speed over its cell, which this keeps finite.
#define MVFX_MIN_WATER_CELL 1e-6f

// The most substeps a water surface takes in a step: a longer step (a
// hitch) advances that many of the longest stable substep, the water
// slowed rather than blown up.
#define MVFX_MAX_WATER_SUBSTEPS 64

// The most pushes a water surface holds for an instance's next step.
#define MVFX_MAX_WATER_PUSHES 64

    // A water surface's walls, as bits: each closed (waves reflect from
    // it) or, its bit clear, open (waves leave through it).
    typedef uint8_t mvfxWaterWalls;
    enum
    {
        mvfx_waterWallMinX = 1,
        mvfx_waterWallMaxX = 2,
        mvfx_waterWallMinZ = 4,
        mvfx_waterWallMaxZ = 8,
    };

    // A water surface. Build it with mvfxDefaultWaterDef.
    typedef struct mvfxWaterDef
    {
        uint32_t cookie;
        // Its cells along X and Z, each from 2 to MVFX_MAX_WATER_SIZE.
        int32_t size[2];
        // A cell's edge, from MVFX_MIN_WATER_CELL to MVFX_MAX_EXTENT, in the
        // instance's lengths.
        float cellSize;
        // The grid's center in the instance's frame, its Y the rest level;
        // each component finite and at most MVFX_MAX_EXTENT in size.
        float center[3];
        // Where it is: in the instance's frame, moving with it, or where
        // the instance began.
        mvfxGasSpace space;
        // Its closed walls (mvfxWaterWalls); an open wall lets waves out
        // (Mur's absorbing edge, exact for waves meeting it head on).
        mvfxWaterWalls closedWalls;
        // The waves' speed, above 0 and at most MVFX_MAX_EXTENT, in lengths
        // a second: each substep is at most 0.9 h / (c sqrt(2)), the
        // scheme's stable bound for a cell h.
        float waveSpeed;
        // The rate a second at which motion dies (the rate multiplied by
        // exp(-damping t)), from 0 to MVFX_MAX_DRAG.
        float damping;
    } mvfxWaterDef;

// The cookie of a water def, which its default sets and mvfxCreateEffect
// checks.
#define MVFX_WATER_DEF_COOKIE 0x6D767761u

    /// Returns the default water def: 64 by 64 cells of 0.1 at the
    /// instance's origin, in local space, its walls closed, waves at 1 a
    /// second, damped at 0.5 a second.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxWaterDef mvfxDefaultWaterDef(void);

    // A water surface's fields, at its cells' centers, in its frame (the
    // instance's, or where it began).
    typedef uint8_t mvfxWaterField;
    enum
    {
        // The surface's Y.
        mvfx_waterHeight = 0,
        // The surface's velocity: a wave's vertical rate along Y.
        mvfx_waterVelocityX = 1,
        mvfx_waterVelocityY = 2,
        mvfx_waterVelocityZ = 3,
        // The surface's unit normal, from its heights' central differences
        // (one-sided at the grid's edges).
        mvfx_waterNormalX = 4,
        mvfx_waterNormalY = 5,
        mvfx_waterNormalZ = 6,
    };

    // A push on a water surface: at a point in the world, within radius
    // (in the world's lengths, above 0, at most MVFX_MAX_EXTENT) by depth
    // at the point (finite, at most MVFX_MAX_EXTENT in size; below 0 it
    // lifts), falling off as (1 - (r / radius)^2)^2.
    typedef struct mvfxWaterPush
    {
        double point[3];
        float radius;
        float depth;
    } mvfxWaterPush;

#ifdef __cplusplus
}
#endif

#endif
