// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every analytic spawn shape, arc mode and velocity mode, planned instances
// on a real device (lavapipe on CI) beside the CPU path's, compared after
// every step (rhi_harness.h): the point, line, circle, hemisphere, box and
// cone by their thickness and volume, turned and offset; arcs drawn,
// looping, going and coming and spread over a burst, after a start delay
// and at ages past binary32's ticks; velocities from the center, blended
// toward random directions. Skips (77) without an adapter, unless
// MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite on a shape of a kind, turned a little about each axis and
// offset, under a moving instance.
static mvfxEmitterDef OnShape(mvfxShapeKind kind)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.shape = (mvfxShape){.kind = kind,
                                .rotation = {0.2f, -0.1f, 0.3f, 0.9273618f},
                                .offset = {0.1f, -0.2f, 0.3f},
                                .radius = 0.5f,
                                .length = 1.5f,
                                .halfExtents = {0.4f, 0.25f, 0.6f},
                                .angle = 0.5f,
                                .arc = 6.28f};
    return emitter;
}

static void Compared(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, const char* what)
{
    Compare(gpu, kernels, &(Run){.emitters = emitter, .emitterCount = 1, .moving = true}, what);
}

static void TestShapes(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = OnShape(mvfx_shapePoint);
    Compared(gpu, kernels, &emitter, "a point");
    emitter = OnShape(mvfx_shapeLine);
    Compared(gpu, kernels, &emitter, "a line");
    emitter = OnShape(mvfx_shapeCircle);
    emitter.shape.thickness = 0.5f;
    emitter.shape.arc = 3.0f;
    Compared(gpu, kernels, &emitter, "a half ring's part of a circle");
    emitter = OnShape(mvfx_shapeHemisphere);
    emitter.shape.thickness = 0.3f;
    Compared(gpu, kernels, &emitter, "a hemisphere's shell");
    // A box through its volume, on its surface, and in a shell.
    emitter = OnShape(mvfx_shapeBox);
    emitter.shape.thickness = 1.0f;
    Compared(gpu, kernels, &emitter, "a box's volume");
    emitter.shape.thickness = 0.0f;
    Compared(gpu, kernels, &emitter, "a box's surface");
    emitter.shape.thickness = 0.25f;
    Compared(gpu, kernels, &emitter, "a box's shell");
    // A flat box's surface: its faces of no area are never chosen.
    emitter.shape.thickness = 0.0f;
    emitter.shape.halfExtents[1] = 0.0f;
    Compared(gpu, kernels, &emitter, "a flat box's surface");
    // A cone on its base, through its volume, and from an apex.
    emitter = OnShape(mvfx_shapeCone);
    emitter.shape.thickness = 0.4f;
    Compared(gpu, kernels, &emitter, "a cone's base");
    emitter.shape.coneVolume = true;
    Compared(gpu, kernels, &emitter, "a cone's volume");
    emitter.shape.radius = 0.0f;
    Compared(gpu, kernels, &emitter, "a cone from its apex");
}

static void TestArcs(Gpu* gpu, mvfxGpu* kernels)
{
    // A circle's arc looping, going and coming, and spread over its burst,
    // after a start delay its age is from.
    mvfxEmitterDef emitter = OnShape(mvfx_shapeCircle);
    emitter.startDelay = 0.05f;
    emitter.shape.arcMode = mvfx_arcLoop;
    emitter.shape.arcSpeed = 2.5f;
    Compared(gpu, kernels, &emitter, "a looping arc");
    emitter.shape.arcMode = mvfx_arcPingPong;
    emitter.shape.arcSpeed = 1.7f;
    Compared(gpu, kernels, &emitter, "an arc going and coming");
    emitter.shape.arcMode = mvfx_arcBurstSpread;
    Compared(gpu, kernels, &emitter, "an arc spread over a burst");
    // A cone and a line read the arc as well.
    emitter = OnShape(mvfx_shapeCone);
    emitter.shape.arcMode = mvfx_arcPingPong;
    emitter.shape.arcSpeed = 0.75f;
    emitter.shape.arc = 2.0f;
    Compared(gpu, kernels, &emitter, "a cone's arc going and coming");
    emitter = OnShape(mvfx_shapeLine);
    emitter.shape.arcMode = mvfx_arcLoop;
    emitter.shape.arcSpeed = 0.3f;
    Compared(gpu, kernels, &emitter, "a line looping");
    // Steps of a second, 40 of them: the births the last steps keep are
    // past 2^24 ticks old (16 s), past binary32's integers, at nearly the
    // fastest arc speed, the arc's fraction from the exact product of
    // ticks and speed (binary32 turns would be off by a hundredth).
    SetStepTicks(MVFX_TICKS_PER_SECOND);
    emitter = OnShape(mvfx_shapeCircle);
    emitter.rate = 200.0f;
    emitter.burstCount = 0;
    emitter.shape.arcMode = mvfx_arcLoop;
    emitter.shape.arcSpeed = 997.3f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 20},
            "a fast looping arc past 2^24 ticks");
    emitter.shape.arcMode = mvfx_arcPingPong;
    emitter.shape.arcSpeed = 0.0123f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 20},
            "a slow arc going and coming past 2^24 ticks");
    SetStepTicks(STEP_TICKS);
}

static void TestVelocities(Gpu* gpu, mvfxGpu* kernels)
{
    // From the center of a box's volume, and of a point, whose place is
    // the center: a random direction instead.
    mvfxEmitterDef emitter = OnShape(mvfx_shapeBox);
    emitter.shape.thickness = 1.0f;
    emitter.velocity.mode = mvfx_directionFromCenter;
    Compared(gpu, kernels, &emitter, "velocities from a box's center");
    emitter = OnShape(mvfx_shapePoint);
    emitter.velocity.mode = mvfx_directionFromCenter;
    Compared(gpu, kernels, &emitter, "velocities from a point's center");
    // Blended toward a random direction: half way from the cone, and all
    // the way from a fixed one.
    emitter = OnShape(mvfx_shapeHemisphere);
    emitter.velocity.randomize = 0.5f;
    Compared(gpu, kernels, &emitter, "cone velocities half random");
    emitter.velocity.mode = mvfx_directionFixed;
    emitter.velocity.randomize = 1.0f;
    Compared(gpu, kernels, &emitter, "fixed velocities all random");
    emitter.velocity.mode = mvfx_directionShapeNormal;
    emitter.velocity.randomize = 0.25f;
    Compared(gpu, kernels, &emitter, "normal velocities a quarter random");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestShapes(gpu, kernels);
    TestArcs(gpu, kernels);
    TestVelocities(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
