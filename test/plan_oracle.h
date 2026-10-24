// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The oracles of a plan's resolved parts, in binary64, for the plan
// suites. plan.h gives each in the space the plan works in (an emitter
// plan's particles' space, a gas plan's box's frame, a water plan's
// surface's frame): forces (mvfxPlannedForce) with lengths scaled, points
// and vectors mapped and the strength taken from its parameter; colliders
// (mvfxPlannedCollider) grown by the particles' radius; and a gas plan's
// pours (mvfxPlannedPour). Each goes through a Frame, the map from a
// def's space into the plan's; results are compared to 1e-5 of their
// size.

#ifndef MAUL_VFX_TEST_PLAN_ORACLE_H
#define MAUL_VFX_TEST_PLAN_ORACLE_H

#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// A map into a plan's space: x' = turn(sigma (x - before)) + after.
typedef struct Frame
{
    double before[3];
    double after[3];
    double rotation[4];
    double sigma;
} Frame;

// v turned by the unit quaternion q.
static inline void Turn(const double q[4], const double v[3], double out[3])
{
    double t[3] = {2.0 * (q[1] * v[2] - q[2] * v[1]), 2.0 * (q[2] * v[0] - q[0] * v[2]),
                   2.0 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}
static inline void FramePoint(const Frame* f, const float p[3], double out[3])
{
    double local[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        local[k] = ((double)p[k] - f->before[k]) * f->sigma;
    }
    Turn(f->rotation, local, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] += f->after[k];
    }
}

// A point of the particles' space mapped back by a frame's inverse.
static inline void FrameBack(const Frame* f, const double p[3], double out[3])
{
    const double back[4] = {-f->rotation[0], -f->rotation[1], -f->rotation[2], f->rotation[3]};
    double d[3] = {p[0] - f->after[0], p[1] - f->after[1], p[2] - f->after[2]};
    double turned[3];
    Turn(back, d, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = turned[k] / f->sigma + f->before[k];
    }
}

static inline void FrameVector(const Frame* f, const float v[3], double sigma, double out[3])
{
    const double scaled[3] = {(double)v[0] * sigma, (double)v[1] * sigma, (double)v[2] * sigma};
    Turn(f->rotation, scaled, out);
}

// The frame's turn after a def's turn q.
static inline void FrameTurn(const Frame* f, const float turn[4], double out[4])
{
    const double* a = f->rotation;
    const double q[4] = {(double)turn[0], (double)turn[1], (double)turn[2], (double)turn[3]};
    out[0] = a[3] * q[0] + a[0] * q[3] + a[1] * q[2] - a[2] * q[1];
    out[1] = a[3] * q[1] - a[0] * q[2] + a[1] * q[3] + a[2] * q[0];
    out[2] = a[3] * q[2] + a[0] * q[1] - a[1] * q[0] + a[2] * q[3];
    out[3] = a[3] * q[3] - a[0] * q[0] - a[1] * q[1] - a[2] * q[2];
}

// Whether a binary32 result is a binary64 oracle's, to 1e-5 of its size.
static inline bool Near(float got, double want)
{
    return fabs((double)got - want) <= 1e-5 * (1.0 + fabs(want));
}

static inline bool Near3(const float got[3], const double want[3])
{
    return Near(got[0], want[0]) && Near(got[1], want[1]) && Near(got[2], want[2]);
}

static inline bool Near4(const float got[4], const double want[4])
{
    return Near3(got, want) && Near(got[3], want[3]);
}

static inline void Normalized(const float v[3], double out[3])
{
    const double d[3] = {(double)v[0], (double)v[1], (double)v[2]};
    double n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = d[k] / n;
    }
}

// Whether a planned force is a def's resolved by a frame with the given
// parameter's x: its lengths scaled, its points and vectors mapped (the
// vortex's axis and a falloff's axis turned, not scaled), an attractor's
// and a vortex's strength scaled as accelerations, the strength from its
// parameter held as effect.h says.
static inline bool SameForce(const mvfxPlannedForce* p, const mvfxForce* def, const Frame* f,
                             float parameter)
{
    double sigma = f->sigma;
    double strength = (double)def->strength;
    if (def->strengthFromParameter)
    {
        strength = fmin(fmax(strength * (double)parameter, -(double)MVFX_MAX_EXTENT),
                        (double)MVFX_MAX_EXTENT);
    }
    bool accelerates = def->kind == mvfx_forceAttractor || def->kind == mvfx_forceVortex;
    strength *= accelerates ? sigma : 1.0;
    bool same = p->kind == def->kind && p->falloff == def->falloff.kind &&
                p->fieldMode == def->fieldMode && Near(p->sigma, sigma) &&
                Near(p->strength, strength) && Near(p->softening, (double)def->softening * sigma) &&
                Near(p->killRadius, (double)def->killRadius * sigma) &&
                Near(p->pull, (double)def->pull * sigma) &&
                Near(p->radius, (double)def->falloff.radius * sigma) &&
                p->exponent == def->falloff.exponent && p->tightness == def->tightness &&
                Near4(p->rotation, f->rotation);
    if (def->falloff.kind == mvfx_falloffCone)
    {
        same = same && Near(p->cosine, cos((double)def->falloff.angle));
    }
    double vector[3];
    if (def->kind == mvfx_forceVortex)
    {
        double axis[3];
        Normalized(def->vector, axis);
        Turn(f->rotation, axis, vector);
    }
    else
    {
        FrameVector(f, def->vector, sigma, vector);
    }
    double point[3];
    double center[3];
    double axis[3] = {0, 0, 0};
    FramePoint(f, def->point, point);
    FramePoint(f, def->falloff.center, center);
    if (def->falloff.kind == mvfx_falloffTube || def->falloff.kind == mvfx_falloffCone)
    {
        double unit[3];
        Normalized(def->falloff.axis, unit);
        Turn(f->rotation, unit, axis);
    }
    return same && Near3(p->vector, vector) && Near3(p->point, point) && Near3(p->center, center) &&
           Near3(p->axis, axis);
}

// Whether a turbulence's planned octaves are its noise at the plan's
// start: the particles' origin mapped back into the force's space,
// scrolled for time, times each octave's frequency, split into a 32-bit
// cell and a fraction (plan.h, mvfxPlannedForce).
static inline bool SameLattice(const mvfxPlannedForce* p, const mvfxForce* def, const Frame* f,
                               double time)
{
    static const double origin[3] = {0.0, 0.0, 0.0};
    double place[3];
    FrameBack(f, origin, place);
    bool same = p->octaves == def->noise.octaves && p->gain == def->noise.gain;
    double frequency = (double)def->noise.frequency;
    for (int32_t o = 0; same && o < p->octaves; ++o)
    {
        same = Near(p->frequency[o], frequency);
        for (int32_t k = 0; k < 3; ++k)
        {
            double x = (place[k] + (double)def->noise.scroll[k] * time) * frequency;
            double cell = floor(x);
            same = same && p->cell[o][k] == (uint32_t)(int64_t)cell &&
                   fabs((double)p->fraction[o][k] - (x - cell)) < 1e-4;
        }
        frequency *= (double)def->noise.lacunarity;
    }
    return same;
}
// Whether a planned collider is a def resolved by a frame and grown by
// the particles' radius (plan.h, mvfxPlannedCollider): a grid's keeps its
// box, its values scaled by the frame's scale and kept off by the radius;
// a plane moves out along its normal, a sphere outside grows and one
// inside shrinks (never below 0), a box grows or shrinks on every axis.
static inline bool SameCollider(const mvfxPlannedCollider* p, const mvfxCollider* def, bool hasGrid,
                                const Frame* f, double radius)
{
    double sigma = f->sigma;
    double center[3];
    double turn[4];
    FramePoint(f, def->center, center);
    FrameTurn(f, def->rotation, turn);
    bool same = p->kind == def->kind && p->inside == def->inside && Near4(p->rotation, turn);
    if (hasGrid)
    {
        same = same && Near3(p->center, center) && Near(p->scale, sigma) && Near(p->grow, radius) &&
               p->radius == 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            same = same && Near(p->halfExtents[k], (double)def->halfExtents[k] * sigma);
        }
        return same;
    }
    double grow = def->inside ? -radius : radius;
    double normal[3];
    FrameVector(f, def->normal, 1.0, normal);
    for (int32_t k = 0; def->kind == mvfx_colliderPlane && k < 3; ++k)
    {
        center[k] += radius * normal[k];
    }
    same = same && Near3(p->center, center) && Near3(p->normal, normal) &&
           Near(p->radius, fmax((double)def->radius * sigma + grow, 0.0));
    for (int32_t k = 0; k < 3; ++k)
    {
        same =
            same && Near(p->halfExtents[k], fmax((double)def->halfExtents[k] * sigma + grow, 0.0));
    }
    return same;
}

// The frame a space maps into a frame placed in the world at center,
// turned by rotation and scaled by scale, with the instance at place: a
// point x whose world place is turn(q, s x) + offset goes to
// turn(rotation^-1, (turn(q, s x) + offset - center) / scale). A local
// space's q, s and offset are the instance's; a world space's none, but
// for a world emitter's particles, offsets from its anchor
// (docs/user/instances.md).
static inline Frame FrameInto(const double center[3], const float rotation[4], float scale,
                              const mvfxPlanPlace* place, mvfxSimulationSpace space,
                              const double anchor[3])
{
    Frame f = {.sigma = 1.0 / (double)scale};
    const double back[4] = {-(double)rotation[0], -(double)rotation[1], -(double)rotation[2],
                            (double)rotation[3]};
    double offset[3] = {0.0, 0.0, 0.0};
    const float none[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float* q = none;
    if (space == mvfx_spaceLocal)
    {
        q = place->rotation;
        f.sigma *= (double)place->scale;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        offset[k] = space == mvfx_spaceLocal ? place->origin[k] : anchor != NULL ? anchor[k] : 0.0;
        offset[k] = (offset[k] - center[k]) / (double)scale;
    }
    Turn(back, offset, f.after);
    const Frame turn = {.rotation = {back[0], back[1], back[2], back[3]}};
    FrameTurn(&turn, q, f.rotation);
    return f;
}

// The frame a gas plan maps a space into (plan.h, mvfxGasPlan): the box's,
// which mvfxInstance_GetGasInfo places.
static inline Frame BoxFrame(const mvfxGasInfo* box, const mvfxPlanPlace* place,
                             mvfxSimulationSpace space, const double anchor[3])
{
    return FrameInto(box->center, box->rotation, box->scale, place, space, anchor);
}

// The frame a water plan maps a space into (plan.h, mvfxWaterPlan): the
// surface's, which mvfxInstance_GetWaterInfo places.
static inline Frame SurfaceFrame(const mvfxWaterInfo* surface, const mvfxPlanPlace* place,
                                 mvfxSimulationSpace space)
{
    return FrameInto(surface->center, surface->rotation, surface->scale, place, space, NULL);
}

// Whether a planned pour's map is a frame's (plan.h, mvfxPlannedPour: a
// place p goes to origin + rotation (sigma p)).
static inline bool SamePour(const mvfxPlannedPour* p, const Frame* f)
{
    return Near3(p->origin, f->after) && Near4(p->rotation, f->rotation) &&
           Near(p->sigma, f->sigma);
}

#endif
