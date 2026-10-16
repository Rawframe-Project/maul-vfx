// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Water surfaces as the host meets them: their grids in the world.

#ifndef MAUL_VFX_SRC_WATER_API_H
#define MAUL_VFX_SRC_WATER_API_H

#include "box.h"
#include "instance.h"

// Where water surface w's grid is now (its center at the rest level), and
// with the instance at a transform.
mvfxBoxPlace mvfxWaterPlace(const mvfxInstance* instance, int32_t w);
mvfxBoxPlace mvfxWaterPlaceAt(const mvfxInstance* instance, int32_t w, const mvfxTransform* at);

// The box in the world around water surface w now: its grid, and along
// its frame's Y its largest displacement.
mvfxBounds mvfxWaterBounds(const mvfxInstance* instance, int32_t w);

// The four cells about a point (x, z) of a surface's frame, bilinear
// between the cells' centers and held at the outer cells' within the
// grid's edge: corner k is cell (lo[0] + (k & 1), lo[1] + (k >> 1)) of
// weight mvfxWaterShare(t, k), the weights summing to 1; false off the
// grid.
bool mvfxWaterCorners(const mvfxCompiledWater* water, float x, float z, int32_t lo[2], float t[2]);

static inline float mvfxWaterShare(const float t[2], int32_t corner)
{
    return ((corner & 1) != 0 ? t[0] : 1.0f - t[0]) * ((corner >> 1) != 0 ? t[1] : 1.0f - t[1]);
}

// The surface over a point (x, z) of its frame: its height over the
// grid's center and its ground (shallow water's; waves have none,
// -infinity), bilinear as mvfxWaterCorners weighs; false off the grid
// (nothing written).
bool mvfxWaterHeight(const mvfxCompiledWater* water, const mvfxWaterState* state, float x, float z,
                     float* height, float* ground);

// The water's velocity over a point (x, z) of a surface's frame, bilinear
// likewise; 0 off the grid.
void mvfxWaterVelocity(const mvfxCompiledWater* water, const mvfxWaterState* state, float x,
                       float z, float velocity[3]);

// The surface's height and ground, and the water's velocity, at the
// corners mvfxWaterCorners found for a point.
void mvfxWaterHeightOf(const mvfxCompiledWater* water, const mvfxWaterState* state,
                       const int32_t lo[2], const float t[2], float* height, float* ground);
void mvfxWaterVelocityOf(const mvfxCompiledWater* water, const mvfxWaterState* state,
                         const int32_t lo[2], const float t[2], float velocity[3]);

// The surface's unit normal over a point (x, z) of its frame: its cells'
// normals, bilinear, normalized; off the grid, up.
void mvfxWaterNormal(const mvfxCompiledWater* water, const mvfxWaterState* state, float x, float z,
                     float normal[3]);

#endif
