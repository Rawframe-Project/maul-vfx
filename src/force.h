// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Forces: their checks, their compiled form, the form a step resolves
// them to in the space its particles are stored in, and the acceleration
// and wind drag they give a place.

#ifndef MAUL_VFX_SRC_FORCE_H
#define MAUL_VFX_SRC_FORCE_H

#include "maul-vfx/effect.h"

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
    const mvfxNoise* noise;
    uint32_t seed;
} mvfxResolvedForce;

// Resolves a compiled force with the map, the instance's parameters and
// its seed; the force must outlive the resolved one.
mvfxResolvedForce mvfxResolveForce(const mvfxForce* force, const mvfxSpaceMap* map,
                                   const float (*parameters)[4], uint32_t instanceSeed);

// The drag a resolved force adds at full weight: a wind's strength, a
// velocity field's tightness, else 0.
float mvfxForceDrag(const mvfxResolvedForce* force);

// Adds what the forces give a particle at x at time seconds: their
// accelerations to a, and each wind's or velocity field's share of drag
// to drag (its pull toward its velocity to a).
void mvfxForcesAt(const mvfxResolvedForce* forces, int32_t count, const float x[3], double time,
                  float a[3], float* drag);

#endif
