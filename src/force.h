// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Forces: their checks, their compiled form, the form a step resolves
// them to in the space its particles are stored in, and the acceleration
// and wind drag they give a place.

#ifndef MAUL_VFX_SRC_FORCE_H
#define MAUL_VFX_SRC_FORCE_H

#include "direction.h"
#include "gas_lattice.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/plan.h"

// Whether a force is valid for an effect of parameterCount parameters.
bool mvfxIsForceValid(const mvfxForce* force, int32_t parameterCount);

// A valid force as compiled: its axes normalized.
mvfxForce mvfxCompileForce(const mvfxForce* force);

// How a force's space maps to the particles' space: a point p goes to
// turn(q, sigma (p - before)) + after, a vector v to turn(q, sigma v), an
// axis to turn(q, v) and a length L to sigma L.
typedef struct mvfxSpaceMap
{
    double before[3];
    double after[3];
    float rotation[4];
    float sigma;
} mvfxSpaceMap;

// Maps a point, and a vector scaled by sigma, by a map.
void mvfxMapPoint(const mvfxSpaceMap* map, const float p[3], float out[3]);
void mvfxMapVector(const mvfxSpaceMap* map, const float v[3], float sigma, float out[3]);

// Maps a binary64 point by a map, in binary64 until the end.
void mvfxMapPointDouble(const mvfxSpaceMap* map, const double p[3], float out[3]);

// A vector mapped back by a map's turn back and 1 / sigma.
void mvfxUnmapVector(const mvfxSpaceMap* map, const float v[3], float out[3]);

// A point mapped back by a map, in binary64: the map's inverse; inline,
// as output maps every record's place back.
static inline void mvfxUnmapPoint(const mvfxSpaceMap* map, const float x[3], double out[3])
{
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (double)x[k] - map->after[k];
    }
    const float* q = map->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    double turned[3];
    mvfxRotateDouble(inverse, d, turned);
    // A division by 1 is exact; skipped, as it is slow.
    bool unscaled = map->sigma == 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (unscaled ? turned[k] : turned[k] / (double)map->sigma) + map->before[k];
    }
}

struct mvfxGrid;

// A force resolved for a step, in the particles' space, its strength
// taken from its parameter and held.
typedef struct mvfxResolvedForce
{
    mvfxForceKind kind;
    mvfxFalloffKind falloff;
    float vector[3];
    float point[3];
    float strength;
    float softening;
    float killRadius;
    float pull;
    float center[3];
    float axis[3];
    float radius;
    float cosine;
    float exponent;
    // A field's mode and tightness, the map back to its space for sampling,
    // and the turbulence's noise and seed.
    mvfxFieldMode fieldMode;
    float tightness;
    mvfxSpaceMap map;
    // The compiled force it was resolved from (its noise, its box) and a
    // vector field's grid.
    const mvfxForce* source;
    const struct mvfxGrid* grid;
    // Whether a vector field's box is unturned (its rotation's vector part
    // zero), so its sampling skips the two turns, and whether the map is,
    // so the place's turn back is skipped for it too.
    bool unturned;
    bool mapUnturned;
    uint32_t seed;
    // A gas force's gas: its velocity's faces and its lattice; map is then
    // from the gas's box to the particles' space.
    const float* gasVelocity[3];
    mvfxGasLattice gasLattice;
} mvfxResolvedForce;

// Resolves a compiled force with the map, the instance's parameters and
// its seed, and a vector field's grid; the force and the grid must outlive
// the resolved one.
mvfxResolvedForce mvfxResolveForce(const mvfxForce* force, const mvfxSpaceMap* map,
                                   const float (*parameters)[4], uint32_t instanceSeed,
                                   const struct mvfxGrid* grid);

// A resolved force as a plan starting at time seconds hands it over.
mvfxPlannedForce mvfxPlannedForceOf(const mvfxResolvedForce* force, double time);

// The drag a resolved force adds at full weight: a wind's strength, a
// velocity field's tightness, else 0.
float mvfxForceDrag(const mvfxResolvedForce* force);

// Adds what the forces give a particle at x at time seconds: their
// accelerations to a, and each wind's or velocity field's share of drag
// to drag (its pull toward its velocity to a).
void mvfxForcesAt(const mvfxResolvedForce* forces, int32_t count, const float x[3], double time,
                  float a[3], float* drag);

#endif
