// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The integrator: particles move exactly under a constant acceleration
// and linear drag over a span h,
//
//   v1 = v0 e + a h phi1,  x1 = x0 + v0 h phi1 + a h^2 phi2,
//
// with e = exp(-k h), phi1 = (1 - e) / (k h) and phi2 = (1 - phi1) /
// (k h), each a series near k h = 0 (Hochbruck and Ostermann,
// "Exponential integrators", Acta Numerica 2010). Rotation moves alike
// under angular drag.

#ifndef MAUL_VFX_SRC_INTEGRATE_H
#define MAUL_VFX_SRC_INTEGRATE_H

#include "collide.h"
#include "force.h"
#include "instance.h"

// The factors drag gives a span: how much velocity is left (e), how far
// a velocity carries (h phi1) and how far an acceleration pushes
// (h^2 phi2).
typedef struct mvfxDragFactors
{
    float decay;
    float carry;
    float push;
} mvfxDragFactors;

// The factors of drag k a second over a span of seconds, in binary64.
mvfxDragFactors mvfxDragFactorsOf(double drag, double seconds);

// Resolves the emitter's forces, kill rules and velocity modules for the
// step, from the instance's transform at its start.
void mvfxResolveEmitter(mvfxInstance* instance, int32_t emitter);

// Resolves the emitter's forces alone, as mvfxResolveEmitter does: what a
// planned instance's step hands a simulation elsewhere.
void mvfxResolveForces(mvfxInstance* instance, int32_t emitter);

// The emitter's gravity in its particles' space.
void mvfxGravityOf(const mvfxInstance* instance, const mvfxCompiledEmitter* emitter, float out[3]);

// Particle i's own colliders over a step of its emitter: its contact
// plane from the scene (into plane), grown by its radius times sigma (the
// particles' space's scale), and its instance's depth image.
mvfxOwnColliders mvfxOwnCollidersOf(const mvfxInstance* instance, int32_t emitter, int32_t i,
                                    float sigma, mvfxResolvedCollider* plane);

// Holds velocity i of the arrays v (x, y, z) to a speed of at most
// fastest, keeping its heading: past it, scaled to fastest in binary64.
// A NaN or infinite component leaves it as it is, for the kill rules.
void mvfxHoldSpeed(float* const v[3], int32_t i, float fastest);

// Moves the particles the emitter holds over the whole step (those alive
// at its start, once the ended ones are gone), marking those killed, each
// speed held within MVFX_MAX_SPEED (times the particles' space's scale)
// after its contacts (mvfxHoldSpeed).
void mvfxIntegrateEmitter(mvfxInstance* instance, int32_t emitter);

// Moves particles first to end, from a tick on the instance's clock, over
// seconds, marking those killed: a substep of a solver's emitter (units,
// rigid bodies, a liquid), whose velocities are not held.
void mvfxIntegrateRange(mvfxInstance* instance, int32_t emitter, int32_t first, int32_t end,
                        int64_t tick, double seconds);

// Turns a unit quaternion q by the rotation vector theta (axis times
// angle), in the parent's frame: q' = r(theta) q, then normalized.
void mvfxTurnBy(float q[4], const float theta[3]);

// What rotation speed over life scales particle i's turn by at a tick on
// the instance's clock (1 without it), as the integrator takes it.
float mvfxTurnScale(const mvfxInstance* instance, int32_t emitter, int32_t i, int64_t tick);

// Moves a particle born within the step from its birth to the step's end,
// marking it if killed, its velocity held as mvfxIntegrateEmitter holds it
// where holds is set (not for a rigid body).
void mvfxIntegrateNewborn(mvfxInstance* instance, int32_t emitter, int32_t at, int64_t birthTick,
                          bool holds);

#endif
