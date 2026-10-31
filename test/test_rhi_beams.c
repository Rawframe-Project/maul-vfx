// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Beams on a real device (lavapipe on CI) beside the CPU path's, compared
// after every step (rhi_harness.h): each point's place, its velocity at
// rest and its distance along the beam, along a line, a quadratic and a
// cubic curve aimed by parameters, noise moving with time, in the
// instance's space and the world's under a moving instance, struck again
// after death, a kill rule taking points, the most segments. Skips (77)
// without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// The parameters a beam is aimed by: a control and an end.
static const float s_parameters[8] = {1.0f, 2.0f, 0.5f, 0.0f, 4.0f, 0.0f, -1.0f, 0.0f};

// A beam of segments from the origin to (4, 0, 0) with noise moving with
// time, its points living 0.25 seconds, tiled by distance (so each point
// stores its distance), under gravity (moved, then placed again).
static mvfxEmitterDef Beam(int32_t segments)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = segments + 1;
    emitter.lifetime = mvfxMakeRangeSource(0.2f, 0.3f);
    emitter.motion.gravity[1] = -9.8f;
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.ribbon.texture = mvfx_ribbonTileByDistance;
    emitter.ribbon.tileLength = 0.25f;
    emitter.beam = (mvfxBeamDef){.enable = true,
                                 .segments = segments,
                                 .start = {.point = {0, 0, 0}},
                                 .end = {.point = {4, 0, 0}},
                                 .noiseAmplitude = 0.3f,
                                 .noiseFrequency = 3.0f,
                                 .noiseSpeed = 2.0f};
    return emitter;
}

static void TestLine(Gpu* gpu, mvfxGpu* kernels)
{
    // In the instance's space under a moving instance, struck again as
    // each strike dies.
    mvfxEmitterDef emitter = Beam(8);
    emitter.space = mvfx_spaceLocal;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .least = 9, .moving = true},
            "a line in the instance's space");
    // In the world's space: the transform at the step's end less the
    // anchor.
    emitter.space = mvfx_spaceWorld;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .least = 9, .moving = true},
            "a line in the world's space");
    // Still noise, and none.
    emitter.beam.noiseSpeed = 0.0f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 9},
            "a line with still noise");
    emitter.beam.noiseAmplitude = 0.0f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 9},
            "a line without noise");
}

static void TestCurves(Gpu* gpu, mvfxGpu* kernels)
{
    // A quadratic curve through a control, and a cubic one with its end
    // too, aimed by parameters the host turns each step.
    mvfxEmitterDef emitter = Beam(24);
    emitter.beam.controlCount = 1;
    emitter.beam.controls[0] = (mvfxBeamPoint){.fromParameter = true, .parameter = 0};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .least = 25,
                   .moving = true,
                   .parameters = s_parameters,
                   .parameterCount = 2,
                   .aiming = true},
            "a quadratic curve aimed by a parameter");
    emitter.beam.controlCount = 2;
    emitter.beam.controls[1] = (mvfxBeamPoint){.point = {3.0f, -1.0f, 1.0f}};
    emitter.beam.end = (mvfxBeamPoint){.fromParameter = true, .parameter = 1};
    emitter.space = mvfx_spaceLocal;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .least = 25,
                   .moving = true,
                   .parameters = s_parameters,
                   .parameterCount = 2,
                   .aiming = true},
            "a cubic curve aimed by parameters");
}

static void TestKilled(Gpu* gpu, mvfxGpu* kernels)
{
    // A box across the beam's middle kills the points in it: those left
    // close up, placed again from the start along the curve.
    static const mvfxKillRule box = {.kind = mvfx_killInsideBox,
                                     .space = mvfx_spaceLocal,
                                     .center = {2.0f, 0.0f, 0.0f},
                                     .halfExtents = {0.3f, 0.6f, 0.6f},
                                     .rotation = {0, 0, 0, 1}};
    mvfxEmitterDef emitter = Beam(8);
    emitter.space = mvfx_spaceLocal;
    emitter.killRules = &box;
    emitter.killRuleCount = 1;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .least = 9, .killing = true},
            "a beam's points killed in a box");
}

static void TestMost(Gpu* gpu, mvfxGpu* kernels)
{
    // The most segments: 1,025 points, five a run of the workgroup.
    mvfxEmitterDef emitter = Beam(MVFX_MAX_BEAM_SEGMENTS);
    emitter.lifetime = mvfxMakeRangeSource(0.05f, 0.1f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .steps = 20,
                   .least = MVFX_MAX_BEAM_SEGMENTS + 1,
                   .moving = true},
            "a beam of the most segments");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestLine(gpu, kernels);
    TestCurves(gpu, kernels);
    TestKilled(gpu, kernels);
    TestMost(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
