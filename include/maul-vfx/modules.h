// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's modules beyond spawn and forces: values over a particle's
// life, the velocity modules a step applies (a scale of each particle's
// motion, an added velocity, orbit and radial velocity, a speed limit),
// and the values over life its particles show (colour, alpha, size,
// flipbook frame, rotation speed).

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

    // What an over-life value is taken over: the particle's normalized age,
    // or its speed between speedRange's ends (in the emitter's units, 0 at
    // the first and 1 at the second, held to [0, 1]).
    typedef uint8_t mvfxOverLifeInput;
    enum
    {
        mvfx_overAge = 0,
        mvfx_overSpeed = 1,
    };

    // Colour over life: the particle's colour (its initial value, or white)
    // times the gradient, and its alpha times alpha over age besides
    // (Godot's split of alpha from colour). Both are taken where a colour is
    // read (output, events, ribbons), never written back.
    typedef struct mvfxColorOverLife
    {
        bool enabled;
        mvfxOverLifeInput input;
        // Finite, the first below the second, at most MVFX_MAX_EXTENT.
        float speedRange[2];
        // The colour's factor; no keys is white.
        mvfxGradient gradient;
        // Whether alpha is scaled over age too, by alpha.
        bool scaleAlpha;
        mvfxLifeCurve alpha;
    } mvfxColorOverLife;

    // Size over life: the particle's size (its initial value, or 1) times
    // size[0] on every axis, or size[0], [1] and [2] on X, Y and Z.
    typedef struct mvfxSizeOverLife
    {
        bool enabled;
        mvfxOverLifeInput input;
        float speedRange[2];
        bool perAxis;
        mvfxLifeCurve size[3];
    } mvfxSizeOverLife;

    // How a flipbook frame moves over life.
    typedef uint8_t mvfxFrameAnimation;
    enum
    {
        // The frame it starts at, always.
        mvfx_frameStill = 0,
        // Through the frames cycles times over its life.
        mvfx_frameOverLife = 1,
        // At frameRate frames a second of its age.
        mvfx_frameAtRate = 2,
    };

    // A flipbook over life: its frame is its start frame (or 0) plus the
    // frames it has moved, wrapped to the frames (looping) or held at the
    // last.
    typedef struct mvfxFrameOverLife
    {
        mvfxFrameAnimation animation;
        // The flipbook's frames, from 1 to MVFX_MAX_FRAMES.
        int32_t frameCount;
        // Over life: above 0 and at most 1,000. At a rate: above 0 and at most
        // 10,000 frames a second.
        float cycles;
        float frameRate;
        bool loop;
    } mvfxFrameOverLife;

    // A rotation speed over life: a sprite's spin and a mesh particle's
    // angular velocity times scale at its age, each step (an angle sums
    // over steps, so this one is not taken where it is read).
    typedef struct mvfxRotationOverLife
    {
        bool enabled;
        mvfxLifeCurve scale;
    } mvfxRotationOverLife;

    // The values over life an emitter's particles show.
    typedef struct mvfxOverLife
    {
        mvfxColorOverLife color;
        mvfxSizeOverLife size;
        mvfxFrameOverLife frame;
        mvfxRotationOverLife rotation;
    } mvfxOverLife;

// The most kill rules an emitter has.
#define MVFX_MAX_KILL_RULES 8

    // What a kill rule kills. Region rules test the straight segment a
    // particle covered over a step, so a particle is caught however fast it
    // went; how a curving path is cut into steps can still decide it.
    typedef uint8_t mvfxKillKind;
    enum
    {
        // Passing through a sphere of radius about center.
        mvfx_killInsideSphere = 0,
        // Ending a step outside it.
        mvfx_killOutsideSphere = 1,
        // Passing through a box of halfExtents about center, turned by
        // rotation.
        mvfx_killInsideBox = 2,
        // Ending a step outside it.
        mvfx_killOutsideBox = 3,
        // Ending a step past the plane through center, on the side normal
        // points to.
        mvfx_killPastPlane = 4,
        // Ending a step slower than speed, once older than minimumAge
        // seconds.
        mvfx_killSlower = 5,
    };

    // A kill rule, tested at the end of each step; a particle it kills is
    // gone from then. Its places and lengths are in its space, the world's or
    // the emitter's (turning and scaling with the instance); each value
    // finite and at most MVFX_MAX_EXTENT in size, fields its kind does not
    // read not checked.
    typedef struct mvfxKillRule
    {
        mvfxKillKind kind;
        // A mvfxSimulationSpace: 0 the world's, 1 the emitter's.
        uint8_t space;
        float center[3];
        // A sphere's radius, above 0.
        float radius;
        // A box's half sizes, each above 0, and its rotation, a unit
        // quaternion within 0.001.
        float halfExtents[3];
        float rotation[4];
        // A plane's normal, not of length 0.
        float normal[3];
        // The speed, at least 0, and the age before which it does not kill,
        // from 0 to MVFX_MAX_LIFETIME seconds.
        float speed;
        float minimumAge;
    } mvfxKillRule;

#ifdef __cplusplus
}
#endif

#endif
