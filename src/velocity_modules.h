// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The velocity modules: their checks, their compiled form, and what they
// do to a particle over a span.

#ifndef MAUL_VFX_SRC_VELOCITY_MODULES_H
#define MAUL_VFX_SRC_VELOCITY_MODULES_H

#include "life.h"

// The velocity modules as compiled: directions and axes normalized.
typedef struct mvfxCompiledVelocityModules
{
    mvfxCompiledLife motionScale;
    float addedDirection[3];
    mvfxCompiledLife addedSpeed;
    float orbitAxis[3];
    float orbitCenter[3];
    mvfxCompiledLife orbitRate;
    float radialCenter[3];
    mvfxCompiledLife radialSpeed;
    bool limitSpeed;
    mvfxCompiledLife speedLimit;
    float limitDampen;
    // Whether any module moves particles beyond their own motion.
    bool moves;
} mvfxCompiledVelocityModules;

// The velocity modules as a step takes them, in the particles' space.
typedef struct mvfxResolvedVelocityModules
{
    const mvfxCompiledVelocityModules* compiled;
    // The emitter's space's scale in the particles' (1, or the instance's
    // scale for a world-space emitter), and the added direction, orbit
    // axis and centers mapped there.
    float sigma;
    float addedDirection[3];
    float orbitAxis[3];
    float orbitCenter[3];
    float radialCenter[3];
} mvfxResolvedVelocityModules;

// Whether velocity modules are valid: mvfx_errorLimit for a life curve,
// direction, center or dampening past its named bound, mvfx_errorInvalid
// for anything else wrong.
mvfxResult mvfxCheckVelocityModules(const mvfxVelocityModules* modules);

// The curve tables they need.
int32_t mvfxVelocityModuleCurves(const mvfxVelocityModules* modules);

void mvfxCompileVelocityModules(const mvfxVelocityModules* modules, mvfxTableSpace* space,
                                mvfxCompiledVelocityModules* out);

// Holds a particle's velocity to the speed limit at age over a span whose
// limit decay is decay (e^(-dampen h)).
void mvfxLimitSpeed(const mvfxResolvedVelocityModules* modules, float age, float decay,
                    float velocity[3]);

// Moves a particle that went from start to place over seconds by the
// modules at age: its motion scaled, the added and radial velocities, the
// orbit.
void mvfxMoveByModules(const mvfxResolvedVelocityModules* modules, float age, double seconds,
                       const float start[3], float place[3]);

#endif
