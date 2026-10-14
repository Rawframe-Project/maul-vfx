// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's modules beyond spawn and forces: values over a particle's
// life, and the velocity modules a step applies (a scale of each
// particle's motion, an added velocity, orbit and radial velocity, a
// speed limit).

#ifndef MAUL_VFX_MODULES_H
#define MAUL_VFX_MODULES_H

#include "maul-vfx/curve.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A value over a particle's life: value times the curve at the
    // particle's normalized age (its age over its life, held to [0, 1]);
    // a curve with no keys is 1. value is finite and at most MVFX_MAX_VALUE
    // in size, the curve's keys' times in [0, 1]; the effect bakes it.
    typedef struct mvfxLifeCurve
    {
        float value;
        mvfxCurve curve;
    } mvfxLifeCurve;

    // The velocity modules, taken in the emitter's space (turning and
    // scaling with the instance), each at the particle's age at the step's
    // start (or its birth) and held over its span. The speed limit changes
    // a particle's velocity; the others move it over the step and are not
    // kept, so they never build up.
    typedef struct mvfxVelocityModules
    {
        // Scales each particle's own motion over a step (under its velocity,
        // gravity, drag and forces): 1 leaves it, 0 lets only the added
        // velocities below move it, as setting the velocity would.
        mvfxLifeCurve motionScale;
        // A velocity added to the motion: addedDirection (normalized; any
        // when the speed is 0) times addedSpeed.
        float addedDirection[3];
        mvfxLifeCurve addedSpeed;
        // Turns each particle about the axis through orbitCenter at
        // orbitRate radians a second, exactly (on its circle).
        float orbitAxis[3];
        float orbitCenter[3];
        mvfxLifeCurve orbitRate;
        // Moves each particle away from radialCenter at radialSpeed (toward
        // it when negative; none at the center).
        float radialCenter[3];
        mvfxLifeCurve radialSpeed;
        // Whether speed is limited: the speed above speedLimit (from 0) is
        // lost as 1 - e^(-limitDampen t), limitDampen above 0 and at most
        // MVFX_MAX_DRAG a second, so the limit holds whatever the steps.
        bool limitSpeed;
        mvfxLifeCurve speedLimit;
        float limitDampen;
    } mvfxVelocityModules;

#ifdef __cplusplus
}
#endif

#endif
