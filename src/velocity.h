// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Initial velocities: a direction from the mode (the shape's normal, away
// from its center, within a cone, fixed), blended toward a uniformly
// random one, times a speed drawn from its range.

#ifndef MAUL_VFX_SRC_VELOCITY_H
#define MAUL_VFX_SRC_VELOCITY_H

#include "shape.h"

// A velocity as the step draws it: its direction normalized, its cone's
// cosine and a basis about its axis.
typedef struct mvfxCompiledVelocity
{
    mvfxDirectionMode mode;
    float direction[3];
    float tangentA[3];
    float tangentB[3];
    float coneCosine;
    float randomize;
    mvfxRange speed;
} mvfxCompiledVelocity;

// Whether a velocity is valid: mvfx_errorLimit for a direction or a
// speed past MVFX_MAX_EXTENT, mvfx_errorInvalid for anything else wrong.
mvfxResult mvfxCheckVelocity(const mvfxVelocity* velocity);

// Compiles a valid velocity.
void mvfxCompileVelocity(const mvfxVelocity* velocity, mvfxCompiledVelocity* out);

// The initial velocity of a particle born at a shape's sample, from four
// random words.
void mvfxDrawVelocity(const mvfxCompiledVelocity* velocity, const mvfxShapeSample* sample,
                      mvfxWords4 words, float out[3]);

#endif
