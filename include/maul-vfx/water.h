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
// slowed rather than blown up (counted in mvfxWaterInfo.stepsSlowed).
#define MVFX_MAX_WATER_SUBSTEPS 64

// The most pushes a water surface holds for an instance's next step.
#define MVFX_MAX_WATER_PUSHES 64

// The most sources a shallow water surface has.
#define MVFX_MAX_WATER_SOURCES 8

// The farthest a water surface's cell stands from where it rests: a
// wave's height from the rest level, shallow water's depth. A step's
// pushes, crossings, pours and sources are held within it before the step
// moves the water, so a step's products stay within binary32's range.
#define MVFX_MAX_WATER_DEPTH 1e12f

    // How a water surface moves.
    typedef int32_t mvfxWaterModel;
    enum
    {
        // Waves (ripples, wakes, rain) on water at rest: each cell's
        // displacement from the rest level and its rate, by the wave
        // equation at the def's speed.
        mvfx_waterWaves = 0,
        // Shallow water that runs and pools over terrain: each cell's depth
        // and its four outflows by the virtual pipe model (Mei, Decaudin and
        // Hu 2007), driven by the surface's fall to each neighbor under
        // gravity; the volume exact in a closed basin, depths never below 0,
        // a lake at rest still over any terrain. A step's substeps are equal,
        // counted at its start from the state the last step left (each at
        // most half a cell at its fastest speed, the faces' velocities held
        // within that), so a planned instance's plan can count them too.
        mvfx_waterShallow = 1,
    };

    // A source of shallow water over the grid: a sphere's footprint (a
    // disk of radius about center) or a box's (halfExtents[0] by
    // halfExtents[2] about center, along the frame's X and Z), in the
    // surface's frame; each cell whose center it covers gains rate depth a
    // second while the instance plays.
    typedef struct mvfxWaterSource
    {
        mvfxGasShape shape;
        float center[3];
        // A sphere's radius, a box's half sizes along X and Z, each above 0
        // and at most MVFX_MAX_EXTENT.
        float radius;
        float halfExtents[3];
        // Above 0 and at most MVFX_MAX_EXTENT, in lengths a second.
        float rate;
    } mvfxWaterSource;

    // A water surface's walls, as bits: each closed (waves reflect from
    // it) or, its bit clear, open (waves leave through it).
    typedef uint32_t mvfxWaterWalls;
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
        // Waves (the default) or shallow water; the rest is shallow water's.
        mvfxWaterModel model;
        // Gravity along the frame's -Y, above 0 and at most MVFX_MAX_EXTENT,
        // in lengths a second squared.
        float gravity;
        // The rate a second at which the flows die (each times
        // exp(-friction t)), from 0 to MVFX_MAX_DRAG.
        float friction;
        // The ground: a heightfield collider (its grid held while the
        // effect is; space 0 the world's, 1 the instance's frame) when
        // enableTerrain, else a flat floor at the grid's center; with
        // hostTerrain, the instance's first heightfield collider instead
        // (mvfxInstance_SetColliders), the floor while it has none. Read at
        // each step where the vertical through a cell's center meets it
        // (through the point below the center in the collider's frame when
        // the two frames turn apart), the floor outside its box.
        mvfxCollider terrain;
        bool enableTerrain;
        bool hostTerrain;
        // From 0 to MVFX_MAX_WATER_SOURCES, NULL when sourceCount is 0; the
        // effect copies them.
        const mvfxWaterSource* sources;
        int32_t sourceCount;
        // The level the surface starts filled to above the grid's center
        // (finite, at most MVFX_MAX_EXTENT in size), at its first step, in
        // each cell whose ground is below it; 0 with a flat floor starts it
        // dry.
        float fillLevel;
    } mvfxWaterDef;

// The cookie of a water def, which its default sets and mvfxCreateEffect
// checks.
#define MVFX_WATER_DEF_COOKIE 0x6D767761u

    /// Returns the default water def: 64 by 64 cells of 0.1 at the
    /// instance's origin, in local space, its walls closed, waves at 1 a
    /// second, damped at 0.5 a second; for shallow water, gravity 9.81, no
    /// friction, a flat floor, no sources, dry.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxWaterDef mvfxDefaultWaterDef(void);

    // A water surface's fields, at its cells' centers, in its frame (the
    // instance's, or where it began).
    typedef int32_t mvfxWaterField;
    enum
    {
        // The surface's Y.
        mvfx_waterHeight = 0,
        // The surface's velocity: a wave's vertical rate along Y; shallow
        // water's flow along X and Z.
        mvfx_waterVelocityX = 1,
        mvfx_waterVelocityY = 2,
        mvfx_waterVelocityZ = 3,
        // The surface's unit normal, from its heights' central differences
        // (one-sided at the grid's edges).
        mvfx_waterNormalX = 4,
        mvfx_waterNormalY = 5,
        mvfx_waterNormalZ = 6,
        // Shallow water's depth (waves': 0); its ground is the height less
        // the depth.
        mvfx_waterDepth = 7,
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
