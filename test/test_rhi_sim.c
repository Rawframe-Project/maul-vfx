// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned instances simulated on a real device (lavapipe on CI) beside the
// same effects on the CPU path, compared after every step (rhi_harness.h):
// spawning, motion, speeds held, forces, collision, depth, values,
// modules, kill rules, the host's particles, bounds; and what the part
// refuses. Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "emitter_step.h"
#include "half.h"
#include "instance.h"
#include "instance_api.h"
#include "rhi_harness.h"
#include "test_harness.h"

#include "kernels/depth_fill_container.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void TestSprite(Gpu* gpu, mvfxGpu* kernels)
{
    const mvfxEmitterDef sprite = Sprite();
    Compare(gpu, kernels, &(Run){.emitters = &sprite, .emitterCount = 1},
            "the CPU path's particles on the GPU, every step");
}

static void TestMoving(Gpu* gpu, mvfxGpu* kernels)
{
    // A world-space shell with a share of the instance's velocity and a
    // strong drag, and a local-space emitter of 100 with world gravity and
    // shape-normal velocities, under an instance moving past the rebase
    // distance, turning and growing; cleared at half time. The second
    // holds more than 64, the update's groups rounded up.
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[0].shape.thickness = 0.4f;
    emitters[0].inheritVelocity = 0.5f;
    emitters[0].motion.drag = 30.0f;
    emitters[1].capacity = 100;
    emitters[1].rate = 320.0f;
    emitters[1].lifetime = mvfxRangeSource(0.2f, 0.3f);
    emitters[1].space = mvfx_spaceLocal;
    emitters[1].motion.gravitySpace = mvfx_spaceWorld;
    emitters[1].velocity = (mvfxVelocity){.mode = mvfx_directionShapeNormal, .speed = {1, 2}};
    emitters[1].burstCount = 0;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .clearing = true},
            "two emitters under a moving instance, cleared");
    // Fixed bounds, a box in the instance's space, under the same motion.
    emitters[0].fixedBounds = true;
    emitters[0].boundsMin[0] = -1.0f;
    emitters[0].boundsMin[1] = -0.5f;
    emitters[0].boundsMin[2] = -2.0f;
    emitters[0].boundsMax[0] = 3.0f;
    emitters[0].boundsMax[1] = 4.0f;
    emitters[0].boundsMax[2] = 0.25f;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 1, .moving = true},
            "fixed bounds under a moving instance");
}

static void TestSpeedHeld(Gpu* gpu, mvfxGpu* kernels)
{
    // Set off at the fastest valid speed straight down under the strongest
    // constant force, 1e12 a second squared, and a drag of 0.5, whose
    // factors then carry most of the place: each step holds the speed at
    // MVFX_MAX_SPEED, from the births' first move on, in the instance's
    // space and in the world's under a growing instance (the limit times
    // its scale).
    static const mvfxForce strongest = {.kind = mvfx_forceConstant,
                                        .vector = {0, -MVFX_MAX_EXTENT, 0},
                                        .strength = MVFX_MAX_EXTENT};
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    for (int32_t e = 0; e < 2; ++e)
    {
        emitters[e].velocity = (mvfxVelocity){.mode = mvfx_directionFixed,
                                              .direction = {0, -1, 0},
                                              .speed = {MVFX_MAX_EXTENT, MVFX_MAX_EXTENT}};
        emitters[e].forces = &strongest;
        emitters[e].forceCount = 1;
    }
    emitters[0].space = mvfx_spaceLocal;
    emitters[1].space = mvfx_spaceWorld;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .moving = true},
            "speeds held at the limit");
    // Along a diagonal, held to the limit's speed along its own heading:
    // set off along (1, -0.5, 0.25) under a force along it, within 3e-4.
    // On lavapipe the paths part by 7.6e-5 in place (binary32 places a
    // second's 1e9 out under a moving instance); a hold past the largest
    // component instead of the length parts their velocities by 0.39, and
    // one clamping each axis by 3.6.
    static const mvfxForce slanted = {
        .kind = mvfx_forceConstant,
        .vector = {MVFX_MAX_EXTENT, -0.5f * MVFX_MAX_EXTENT, 0.25f * MVFX_MAX_EXTENT},
        .strength = MVFX_MAX_EXTENT};
    for (int32_t e = 0; e < 2; ++e)
    {
        emitters[e].velocity.direction[0] = 1.0f;
        emitters[e].velocity.direction[1] = -0.5f;
        emitters[e].velocity.direction[2] = 0.25f;
        emitters[e].forces = &slanted;
    }
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .tolerance = 3e-4f},
            "diagonal speeds held along their heading");
}

// Bursts larger than the capacity at one tick, and past it with the rate.
static const mvfxBurst s_floods[2] = {
    {.time = 0.1f, .minCount = 40, .maxCount = 40, .cycles = 1, .probability = 1.0f},
    {.time = 0.3f, .minCount = 150, .maxCount = 150, .cycles = 1, .probability = 1.0f}};

// A full emitter turned over in a half-second step: refused, then
// replaced once its particles die.
static const mvfxBurst s_turnover[3] = {
    {.time = 0.4375f, .minCount = 1000, .maxCount = 1000, .cycles = 1, .probability = 1.0f},
    {.time = 0.5625f, .minCount = 500, .maxCount = 500, .cycles = 1, .probability = 1.0f},
    {.time = 0.75f, .minCount = 1000, .maxCount = 1000, .cycles = 1, .probability = 1.0f}};
static const mvfxBurst s_turnoverLarge[3] = {
    {.time = 0.4375f, .minCount = 1 << 17, .maxCount = 1 << 17, .cycles = 1, .probability = 1.0f},
    {.time = 0.5625f, .minCount = 50000, .maxCount = 50000, .cycles = 1, .probability = 1.0f},
    {.time = 0.75f, .minCount = 1 << 17, .maxCount = 1 << 17, .cycles = 1, .probability = 1.0f}};

// An emitter of 32, bursts only, lives of a quarter of a second.
static mvfxEmitterDef Bursts(const mvfxBurst* bursts, int32_t burstCount);

static void TestForces(Gpu* gpu, mvfxGpu* kernels)
{
    // A constant force, a wind in a cone in the instance's space, an
    // attractor with a kill radius the particles fly through (both the
    // live and the newborn), in the instance's space, a vortex in
    // the instance's space, under a moving, turning, growing instance;
    // then a wind everywhere (the drag every particle shares), a constant
    // force in a tube, an attractor in a sphere, in the world.
    mvfxForce forces[4] = {{.kind = mvfx_forceConstant, .vector = {1, 0, 0}, .strength = 2.0f},
                           {.kind = mvfx_forceWind,
                            .space = mvfx_spaceLocal,
                            .vector = {0, 0, 3},
                            .strength = 0.5f,
                            .falloff = {.kind = mvfx_falloffCone,
                                        .center = {0, -0.5f, 0},
                                        .axis = {0, 1, 0},
                                        .radius = 3.0f,
                                        .angle = 0.6f,
                                        .exponent = 2.0f}},
                           {.kind = mvfx_forceAttractor,
                            .space = mvfx_spaceLocal,
                            .point = {0, 0.75f, 0},
                            .strength = 3.0f,
                            .softening = 0.5f,
                            .killRadius = 0.3f},
                           {.kind = mvfx_forceVortex,
                            .space = mvfx_spaceLocal,
                            .vector = {0, 1, 0},
                            .point = {0.5f, 0, 0},
                            .strength = 1.5f,
                            .pull = 0.25f}};
    mvfxEmitterDef emitter = Sprite();
    emitter.forces = forces;
    emitter.forceCount = 4;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "forces of four kinds under a moving instance");
    forces[0] = (mvfxForce){.kind = mvfx_forceWind, .vector = {2, 0, 0}, .strength = 1.5f};
    forces[1] = (mvfxForce){
        .kind = mvfx_forceConstant,
        .vector = {0, 0, 1},
        .strength = 4.0f,
        .falloff = {.kind = mvfx_falloffTube, .axis = {0, 1, 0}, .radius = 0.4f, .exponent = 1.0f}};
    forces[2] =
        (mvfxForce){.kind = mvfx_forceAttractor,
                    .point = {0.5f, 1.0f, 0},
                    .strength = -2.0f,
                    .softening = 0.25f,
                    .falloff = {.kind = mvfx_falloffSphere, .center = {0, 1, 0}, .radius = 1.5f}};
    emitter.forceCount = 3;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1},
            "a wind everywhere, falloffs of a tube and a sphere");
    // A strong attractor whose kill radius, out of a newborn's first
    // step's reach and smaller than a step, the living fly through: a
    // step can cross it with both ends outside.
    forces[0] = (mvfxForce){.kind = mvfx_forceAttractor,
                            .point = {0, 0.7f, 0},
                            .strength = 20.0f,
                            .softening = 0.2f,
                            .killRadius = 0.05f};
    emitter.forceCount = 1;
    emitter.rate = 3000.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .least = 500, .killing = true},
            "the living killed by an attractor");
}

static void TestTurbulence(Gpu* gpu, mvfxGpu* kernels)
{
    // Turbulence of three octaves scrolling in the instance's space, as an
    // acceleration, under a turning, growing instance moving at a hundredth
    // of the usual pace (at full pace the particles it leaves behind are
    // 1,400 units from their anchor, where binary32 places differ from the
    // CPU path's turbulence by 1e-4 of a velocity, as measured); then as a
    // velocity everywhere (the drag every particle shares) and one in a
    // sphere, in the world, seeded by the instance, past 4,096 units from
    // the origin where binary32 places lose the lattice.
    mvfxForce forces[2] = {
        {.kind = mvfx_forceTurbulence,
         .space = mvfx_spaceLocal,
         .strength = 3.0f,
         .noise = {.frequency = 1.5f,
                   .octaves = 3,
                   .lacunarity = 2.0f,
                   .gain = 0.5f,
                   .scroll = {0.4f, -0.3f, 0.2f},
                   .seed = 9}},
    };
    mvfxEmitterDef emitter = Sprite();
    emitter.forces = forces;
    emitter.forceCount = 1;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .pace = 0.01},
            "turbulence in the instance's space under a moving instance");
    forces[0] = (mvfxForce){.kind = mvfx_forceTurbulence,
                            .strength = 1.0f,
                            .fieldMode = mvfx_fieldVelocity,
                            .tightness = 2.0f,
                            .noise = {.frequency = 0.7f,
                                      .octaves = 2,
                                      .lacunarity = 3.0f,
                                      .gain = 0.7f,
                                      .seed = 3,
                                      .seedPerInstance = true}};
    forces[1] = (mvfxForce){
        .kind = mvfx_forceTurbulence,
        .strength = 4.0f,
        .fieldMode = mvfx_fieldVelocity,
        .tightness = 1.0f,
        .falloff = {.kind = mvfx_falloffSphere, .center = {0, 0.5f, 0}, .radius = 1.0f},
        .noise = {.frequency = 2.0f, .octaves = 1, .lacunarity = 1.0f, .gain = 1.0f, .seed = 4}};
    emitter.forceCount = 2;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .far = true},
            "turbulence as velocities, far from the origin");
}

// A smooth field: three components a point, on up to 4,096 points.
static float s_field[3 * 4096];

static mvfxGridDef FieldGrid(mvfxGridFormat format, int32_t x, int32_t y, int32_t z)
{
    for (int32_t p = 0; p < x * y * z; ++p)
    {
        float px = (float)(p % x);
        float py = (float)(p / x % y);
        float pz = (float)(p / (x * y));
        s_field[3 * p] = sinf(0.9f * py + 0.3f * pz);
        s_field[3 * p + 1] = 0.5f * cosf(0.7f * px - 0.4f * pz);
        s_field[3 * p + 2] = sinf(0.5f * px + 0.8f * py) - 0.25f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = x;
    def.size[1] = y;
    def.size[2] = z;
    def.components = 3;
    def.format = format;
    def.values = s_field;
    return def;
}

// 4,096 points along X whose first component turns sign at each.
static mvfxGridDef LongGrid(void)
{
    for (int32_t p = 0; p < 4096; ++p)
    {
        s_field[3 * p] = (p & 1) != 0 ? 1.0f : -1.0f;
        s_field[3 * p + 1] = 0.0f;
        s_field[3 * p + 2] = 0.0f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 4096;
    def.components = 3;
    def.values = s_field;
    return def;
}

static void TestVectorFields(Gpu* gpu, mvfxGpu* kernels)
{
    // A turned box repeating its grid, given new values at half time, in
    // the world; a long grid repeating; a binary16 grid clamped at the
    // faces of a box the particles leave, as velocities,
    // in the instance's space under a turning, growing instance at a
    // hundredth of the usual pace; a box with no field past it that the
    // particles fly out of.
    mvfxForce forces[1] = {{.kind = mvfx_forceVectorField,
                            .point = {0.2f, 0.5f, -0.1f},
                            .strength = 3.0f,
                            .boxSize = {1.5f, 1.0f, 1.2f},
                            .boxRotation = {0.0f, 0.38268343f, 0.0f, 0.92387953f},
                            .border = mvfx_borderRepeat}};
    mvfxEmitterDef emitter = Sprite();
    emitter.forces = forces;
    emitter.forceCount = 1;
    mvfxGridDef grid = FieldGrid(mvfx_gridFloat32, 4, 4, 3);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .grid = &grid, .animated = true},
            "a turned box repeating its grid, animated");
    // 4,096 points along an unturned box of 100 repeating, each cell a
    // sign's turn: the cells and shares of a fine grid.
    forces[0].boxSize[0] = 100.0f;
    forces[0].boxRotation[1] = 0.0f;
    forces[0].boxRotation[3] = 1.0f;
    forces[0].strength = 0.5f;
    grid = LongGrid();
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .grid = &grid},
            "a long grid repeating");
    forces[0] = (mvfxForce){.kind = mvfx_forceVectorField,
                            .space = mvfx_spaceLocal,
                            .point = {0, 0.3f, 0},
                            .strength = 2.0f,
                            .fieldMode = mvfx_fieldVelocity,
                            .tightness = 3.0f,
                            .boxSize = {0.8f, 0.8f, 0.8f},
                            .boxRotation = {0, 0, 0, 1},
                            .border = mvfx_borderClamp};
    // An odd count of halves (81): the last alone in its word.
    grid = FieldGrid(mvfx_gridFloat16, 3, 3, 3);
    Compare(
        gpu, kernels,
        &(Run){
            .emitters = &emitter, .emitterCount = 1, .moving = true, .pace = 0.01, .grid = &grid},
        "a binary16 grid clamped, as velocities, under a moving instance");
    forces[0] = (mvfxForce){.kind = mvfx_forceVectorField,
                            .point = {0, 0.2f, 0},
                            .strength = 6.0f,
                            .boxSize = {0.8f, 0.6f, 0.8f},
                            .boxRotation = {0, 0, 0, 1},
                            .border = mvfx_borderNone};
    grid = FieldGrid(mvfx_gridFloat32, 4, 4, 3);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .grid = &grid},
            "a box with no field past it");
}

static void TestCollision(Gpu* gpu, mvfxGpu* kernels)
{
    // A floor, a turned box and an inside sphere around them, the
    // particles bouncing (restitution, friction, a rest speed many come
    // to rest at); then life
    // lost at each contact under a moving instance at a hundredth of the
    // usual pace, colliders in the instance's space and the instance's
    // own, apart (a particle rattling in a corner meets them by rounding);
    // then a contact that kills.
    static const mvfxCollider own[3] = {
        {.kind = mvfx_colliderPlane, .center = {0, -0.3f, 0}, .normal = {0, 1, 0.1f}},
        {.kind = mvfx_colliderBox,
         .center = {0.3f, 0.6f, 0},
         .halfExtents = {0.3f, 0.1f, 0.4f},
         .rotation = {0, 0, 0.38268343f, 0.92387953f}},
        {.kind = mvfx_colliderSphere, .inside = true, .center = {0, 0.2f, 0}, .radius = 1.0f}};
    mvfxEmitterDef emitter = Sprite();
    emitter.colliders = own;
    emitter.colliderCount = 3;
    emitter.collision = (mvfxCollision){
        .enable = true, .restitution = 0.6f, .friction = 0.2f, .restSpeed = 0.6f, .radius = 0.02f};
    emitter.lifetime = mvfxRangeSource(0.6f, 0.9f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .colliding = true},
            "a floor, a box and a sphere bounced off");
    static const mvfxCollider local[2] = {{.kind = mvfx_colliderPlane,
                                           .space = mvfx_spaceLocal,
                                           .center = {0, -0.2f, 0},
                                           .normal = {0, 1, 0}},
                                          {.kind = mvfx_colliderBox,
                                           .space = mvfx_spaceLocal,
                                           .center = {-0.4f, 0.4f, 0},
                                           .halfExtents = {0.15f, 0.15f, 0.15f},
                                           .rotation = {0, 0, 0, 1}}};
    static const mvfxCollider hosts[1] = {
        {.kind = mvfx_colliderSphere, .center = {0, 0.5f, 0.4f}, .radius = 0.25f}};
    emitter.colliders = local;
    emitter.colliderCount = 2;
    emitter.collision.hostColliders = true;
    emitter.collision.lifeLoss = 0.3f;
    // Spins read over life at the age a contact's lost life leaves.
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 0, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 8, .interpolation = mvfx_interpolationLinear}};
    emitter.spin = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {2, 4}};
    emitter.overLife.rotation = (mvfxRotationOverLife){
        .enable = true, .scale = {.value = 1, .curve = {.keys = keys, .keyCount = 2}}};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .pace = 0.01,
                   .colliding = true,
                   .hostColliders = hosts,
                   .hostColliderCount = 1},
            "life lost at contacts, the instance's colliders, a moving instance");
    emitter.colliders = own;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enable = true, .killOnCollision = true};
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .killing = true},
            "a contact that kills");
}

static void TestOldDeaths(Gpu* gpu, mvfxGpu* kernels)
{
    // Lives of 24 to 30 seconds in steps of half a second, the particles
    // sliding on a floor that takes all their speed into it, meeting it
    // each step (no rest speed, whose edge each path decides alone), a
    // share of their life lost each time: the GPU recomputes death ticks past 2^24 ticks of age,
    // within their bound of the CPU path's. Their spin's speed over life
    // reads the age the lost life leaves.
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -1, 0}, .normal = {0, 1, 0}};
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 0, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 8, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = Sprite();
    emitter.lifetime = mvfxRangeSource(24.0f, 30.0f);
    emitter.spin = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {1, 2}};
    emitter.overLife.rotation = (mvfxRotationOverLife){
        .enable = true, .scale = {.value = 1, .curve = {.keys = keys, .keyCount = 2}}};
    emitter.colliders = &floor;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enable = true, .lifeLoss = 0.005f};
    SetStepTicks(MVFX_TICKS_PER_SECOND / 2);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .steps = 64,
                   .colliding = true,
                   .deathTicks = true},
            "life lost past 16 seconds of age");
    SetStepTicks(STEP_TICKS);
}

static void TestValues(Gpu* gpu, mvfxGpu* kernels)
{
    // Every value a birth draws: a size between bounds, a colour from a
    // gradient at a random point, custom values over the emitter's loop
    // (a curve), its age through a gradient, and a parameter; a random
    // frame and mesh index; under an emitter of a 0.4-second loop and a
    // moving instance.
    static const mvfxCurveKey keys[3] = {
        {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
        {.time = 0.5f, .value = 3, .interpolation = mvfx_interpolationCubic},
        {.time = 1, .value = 0.5f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxColorKey colors[2] = {{.time = 0, .red = 1, .green = 0.2f, .blue = 0},
                                           {.time = 1, .red = 0, .green = 0.5f, .blue = 1}};
    static const mvfxAlphaKey alphas[2] = {{.time = 0, .alpha = 1}, {.time = 1, .alpha = 0.25f}};
    const mvfxGradient gradient = {
        .colorKeys = colors, .colorKeyCount = 2, .alphaKeys = alphas, .alphaKeyCount = 2};
    const mvfxCustomAttribute custom[3] = {
        {.components = 2,
         .initial = {.kind = mvfx_sourceCurve,
                     .curve = {.keys = keys, .keyCount = 3, .tangents = mvfx_tangentsSmooth},
                     .min = {1, 2},
                     .max = {2, 3}}},
        {.components = 4,
         .initial = {.kind = mvfx_sourceGradientOverAge,
                     .gradient = gradient,
                     .uniform = true,
                     .min = {0.5f, 0.5f, 0.5f, 0.5f},
                     .max = {1, 1, 1, 1}}},
        {.components = 3,
         .initial = {.kind = mvfx_sourceParameter,
                     .parameter = 1,
                     .min = {1, 1, 1},
                     .max = {1.5f, 2, 2.5f}}}};
    static const float parameters[8] = {0, 0, 0, 0, 0.25f, -2, 7, 1};
    mvfxEmitterDef emitter = Sprite();
    emitter.duration = 0.4f;
    // A negative size counts by its magnitude in the bounds.
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, -0.6f, 0.3f}, .max = {0.4f, -0.2f, 0.8f}};
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceGradient,
                                       .gradient = gradient,
                                       .min = {0.5f, 0.5f, 0.5f, 1},
                                       .max = {1, 1, 1, 1}};
    emitter.custom = custom;
    emitter.customCount = 3;
    // A wide range carries in the draw's product.
    emitter.frame = (mvfxFrameStart){.mode = mvfx_frameRandom, .first = 0, .last = 65000};
    emitter.meshIndex = (mvfxFrameStart){.mode = mvfx_frameRandom, .first = 2, .last = 5};
    // A size over life for each axis grows the bounds by the largest of
    // each axis's largest size times its factor: here the second's.
    emitter.overLife.size = (mvfxSizeOverLife){
        .enable = true, .perAxis = true, .size = {{.value = 1}, {.value = 3}, {.value = 2}}};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .parameters = parameters,
                   .parameterCount = 2},
            "every value a birth draws");
}

static void TestTurns(Gpu* gpu, mvfxGpu* kernels)
{
    // Angles turned by spin and orientations by angular velocity under an
    // angular drag, their speed scaled over life by a curve, the newborn
    // turned for the rest of their step: Euler orientations from a range
    // and spins from a curve over the loop; random orientations and
    // angles from a parameter. Each particle's drag scaled by its own
    // draw, with a wind everywhere and one in a sphere (a scale above the
    // most drag is held to it, one below 0 gives none).
    static const mvfxCurveKey keys[3] = {
        {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
        {.time = 0.5f, .value = 3, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = -0.5f, .interpolation = mvfx_interpolationLinear}};
    const mvfxCurve curve = {.keys = keys, .keyCount = 3};
    mvfxForce forces[2] = {
        {.kind = mvfx_forceWind, .vector = {1, 0, 0}, .strength = 0.75f},
        {.kind = mvfx_forceWind,
         .vector = {0, 0, -2},
         .strength = 1.5f,
         .falloff = {.kind = mvfx_falloffSphere, .radius = 1.0f, .exponent = 1.0f}}};
    static const float parameters[4] = {-1.5f, 0, 0, 0};
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[0].orientationMode = mvfx_orientationEuler;
    emitters[0].orientation =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-1, 0, 0.5f}, .max = {1, 2, 3}};
    emitters[0].angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-4, 1, 0}, .max = {4, 6, 2}};
    emitters[0].spin =
        (mvfxScalarSource){.kind = mvfx_sourceCurve, .curve = curve, .range = {2, 5}};
    emitters[0].motion.angularDrag = 1.5f;
    emitters[0].motion.dragScale = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {-1, 3}};
    emitters[0].overLife.rotation =
        (mvfxRotationOverLife){.enable = true, .scale = {.value = 2, .curve = curve}};
    emitters[0].forces = forces;
    emitters[0].forceCount = 2;
    emitters[1].orientationMode = mvfx_orientationRandom;
    emitters[1].angularVelocity = (mvfxVectorSource){.kind = mvfx_sourceRange, .max = {3, 0, -3}};
    emitters[1].angle =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .parameter = 0, .range = {1, 2}};
    emitters[1].motion.angularDrag = 0.25f;
    emitters[1].motion.drag = 400.0f;
    emitters[1].motion.dragScale =
        (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {0.001f, 4}};
    emitters[1].overLife.rotation =
        (mvfxRotationOverLife){.enable = true, .scale = {.value = 0.5f}};
    Compare(gpu, kernels,
            &(Run){.emitters = emitters,
                   .emitterCount = 2,
                   .moving = true,
                   .parameters = parameters,
                   .parameterCount = 1},
            "angles, orientations and drag scales");
}

static void TestFastTurns(Gpu* gpu, mvfxGpu* kernels)
{
    // Turns far past the range WGSL bounds its own sine on, which the core
    // reduces exactly: an orbit at the fastest rate a life curve takes,
    // 8333 radians a half step, and orientations turning at up to the
    // fastest spin about each axis, up to 1.4e4 radians a half step.
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[0].velocityModules.orbitAxis[1] = 1.0f;
    emitters[0].velocityModules.orbitCenter[0] = 0.3f;
    emitters[0].velocityModules.orbitRate = (mvfxLifeCurve){.value = MVFX_MAX_LIFE_SCALE};
    emitters[1].orientationMode = mvfx_orientationRandom;
    emitters[1].angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange,
                           .min = {-MVFX_MAX_SPIN, -MVFX_MAX_SPIN, -MVFX_MAX_SPIN},
                           .max = {MVFX_MAX_SPIN, MVFX_MAX_SPIN, MVFX_MAX_SPIN}};
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2},
            "an orbit and spins far past a turn a step");
    // Orientations from Euler angles drawn far past 2^24 radians about each
    // axis, the largest near 1e30.
    mvfxEmitterDef euler = Sprite();
    euler.orientationMode = mvfx_orientationEuler;
    euler.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {-1e30f, 3e7f, -5e9f}, .max = {1e30f, 9e7f, 5e9f}};
    Compare(gpu, kernels, &(Run){.emitters = &euler, .emitterCount = 1},
            "orientations from Euler angles past 2^24");
}

static void TestModules(Gpu* gpu, mvfxGpu* kernels)
{
    // Velocity modules of every kind, each over life: the speed limited by
    // a curve (below most newborns' speeds), the motion scaled by one,
    // added and radial speeds and an orbit; in the instance's space and in
    // the world's (scaled by a growing instance), under a moving instance.
    static const mvfxCurveKey keys[3] = {
        {.time = 0, .value = 2, .interpolation = mvfx_interpolationLinear},
        {.time = 0.5f, .value = 0.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 1, .interpolation = mvfx_interpolationLinear}};
    const mvfxCurve curve = {.keys = keys, .keyCount = 3};
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    for (int32_t e = 0; e < 2; ++e)
    {
        emitters[e].velocityModules =
            (mvfxVelocityModules){.motionScale = {.value = 0.75f, .curve = curve},
                                  .addedDirection = {1, 0, 0.5f},
                                  .addedSpeed = {.value = 1.5f},
                                  .orbitAxis = {0, 1, 0},
                                  .orbitCenter = {0.3f, 0, 0},
                                  .orbitRate = {.value = 2.5f, .curve = curve},
                                  .radialCenter = {0, -0.5f, 0},
                                  .radialSpeed = {.value = 0.8f},
                                  .limitSpeed = true,
                                  .speedLimit = {.value = 1.5f, .curve = curve},
                                  .limitDampen = 3};
    }
    emitters[1].space = mvfx_spaceWorld;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .pace = 0.1},
            "velocity modules in both spaces");
}

static void TestSpawning(Gpu* gpu, mvfxGpu* kernels)
{
    // Births by a rate curve over a loop of 0.3 seconds, and by the
    // distance a moving instance travels, in the world's space.
    static const mvfxCurveKey keys[3] = {
        {.time = 0, .value = 0.2f, .interpolation = mvfx_interpolationLinear},
        {.time = 0.4f, .value = 2, .interpolation = mvfx_interpolationCubic},
        {.time = 1, .value = 0, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = Sprite();
    emitter.duration = 0.3f;
    emitter.rateCurve = (mvfxCurve){.keys = keys, .keyCount = 3};
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "births by a rate curve");
    emitter = Sprite();
    emitter.rate = 0.0f;
    emitter.burstCount = 0;
    emitter.space = mvfx_spaceWorld;
    emitter.ratePerDistance = 60.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .pace = 0.1},
            "births by distance");
}

static void TestHostSpawn(Gpu* gpu, mvfxGpu* kernels)
{
    // The host's particles beside the emitter's own each step: in the
    // world's space with their velocities, colours and sizes given, near
    // the origin and far from it; in the instance's space with velocities
    // drawn along their direction from the instance's origin (one at the
    // origin at the first step, whose direction is drawn too) and sizes
    // given, the colours the emitter stores drawn.
    static const double positions[3 * 8] = {0.5,  0,   0,    -0.5, 0.2, 0,   0,   0.3,
                                            0.4,  0,   0,    0,    0.1, 0.1, 0.1, 0.3,
                                            -0.2, 0.2, -0.3, 0.6,  0,   0,   0.2, -0.4};
    static const float velocities[3 * 8] = {1, 2, 0,  0, 3, 1, -1, 2, 0, 0,  1, 0,
                                            2, 2, -1, 0, 1, 1, 1,  1, 1, -2, 3, 0};
    static const float colors[4 * 8] = {1, 0, 0, 1, 0,    1,    0,    1,    0,    0,   1,
                                        1, 1, 1, 0, 0.5f, 0.5f, 0.5f, 0.5f, 1,    0,   1,
                                        1, 1, 1, 0, 1,    1,    0.2f, 0.4f, 0.6f, 0.8f};
    static const float sizes[3 * 8] = {0.1f, 0.2f, 0.3f, 0.5f, 0.5f, 0.5f, 1,    1,
                                       1,    0.2f, 0.1f, 0.2f, 0.3f, 0.3f, 0.1f, 0.4f,
                                       0.2f, 0.4f, 0.6f, 0.6f, 0.6f, 0.1f, 0.1f, 0.1f};
    mvfxEmitterDef emitter = Sprite();
    emitter.space = mvfx_spaceWorld;
    emitter.hostSpawnCapacity = 16;
    emitter.color =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, 0, 1}, .max = {1, 1, 1, 1}};
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.1f, 0.1f}, .max = {0.2f, 0.2f, 0.2f}};
    const mvfxSpawnRequest given = {.count = 8,
                                    .positions = positions,
                                    .velocities = velocities,
                                    .colors = colors,
                                    .sizes = sizes};
    Compare(
        gpu, kernels,
        &(Run){
            .emitters = &emitter, .emitterCount = 1, .moving = true, .pace = 0.1, .spawn = &given},
        "the host's particles with their values, in the world's space");
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .far = true, .spawn = &given},
            "the host's particles in the world's space, far from the origin");
    emitter.space = mvfx_spaceLocal;
    emitter.velocity.mode = mvfx_directionShapeNormal;
    const mvfxSpawnRequest drawn = {.count = 8, .positions = positions, .sizes = sizes};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .spawn = &drawn},
            "the host's particles with drawn velocities, in the instance's space");
}

static void TestKillRules(Gpu* gpu, mvfxGpu* kernels)
{
    // Each kind of kill rule alone kills, the particles' paths through
    // its region (a turned box the newborn cross too), a speed they fall
    // below once old enough, a ceiling they meet; in the world's space and
    // the instance's, under a slowly moving instance. A rule may kill most
    // of them: 20 held at some step is enough.
    static const mvfxCollider ceiling = {
        .kind = mvfx_colliderPlane, .center = {0, 0.55f, 0}, .normal = {0, -1, 0}};
    const mvfxKillRule rules[7] = {
        {.kind = mvfx_killInsideSphere, .center = {0.1f, 0.4f, 0}, .radius = 0.3f},
        {.kind = mvfx_killOutsideSphere,
         .space = mvfx_spaceLocal,
         .center = {0, 0.3f, 0},
         .radius = 0.7f},
        {.kind = mvfx_killInsideBox,
         .center = {0.1f, 0.45f, 0},
         .halfExtents = {0.4f, 0.05f, 0.4f},
         .rotation = {0, 0, 0.19509032f, 0.98078528f}},
        {.kind = mvfx_killOutsideBox,
         .space = mvfx_spaceLocal,
         .center = {0, 0.4f, 0},
         .halfExtents = {0.6f, 0.4f, 0.6f},
         .rotation = {0, 0.6f, 0, 0.8f}},
        {.kind = mvfx_killPastPlane, .center = {0, 0.8f, 0}, .normal = {0.2f, 1, 0}},
        {.kind = mvfx_killSlower, .speed = 2.0f, .minimumAge = 0.1f},
        {.kind = mvfx_killCollisions, .collisions = 1}};
    static const char* names[7] = {"passing through a sphere",
                                   "ending outside a sphere",
                                   "passing through a turned box",
                                   "ending outside a turned box",
                                   "ending past a plane",
                                   "slower once old enough",
                                   "a collision"};
    for (int32_t r = 0; r < 7; ++r)
    {
        mvfxEmitterDef emitter = Sprite();
        emitter.killRules = &rules[r];
        emitter.killRuleCount = 1;
        if (rules[r].kind == mvfx_killCollisions)
        {
            emitter.colliders = &ceiling;
            emitter.colliderCount = 1;
            emitter.collision = (mvfxCollision){.enable = true, .restitution = 0.5f};
        }
        Compare(gpu, kernels,
                &(Run){.emitters = &emitter,
                       .emitterCount = 1,
                       .least = 20,
                       .moving = true,
                       .pace = 0.001,
                       .killing = true},
                names[r]);
    }
    // Straight paths up from a tiny sphere and a wide one, the instance at
    // rest: a sphere small enough that most paths cross it within a step
    // and end past it, and a box beside the middle that paths with no
    // motion along X meet only where they lie inside it along X.
    const mvfxKillRule exact[2] = {
        {.kind = mvfx_killInsideSphere, .center = {0, 0.3f, 0}, .radius = 0.012f},
        {.kind = mvfx_killInsideBox,
         .center = {0.25f, 0.3f, 0},
         .halfExtents = {0.1f, 0.1f, 0.5f},
         .rotation = {0, 0, 0, 1}}};
    static const char* exactNames[2] = {"crossing a small sphere within a step",
                                        "meeting a box along straight paths"};
    for (int32_t r = 0; r < 2; ++r)
    {
        mvfxEmitterDef emitter = Sprite();
        emitter.shape.radius = r == 0 ? 0.001f : 0.5f;
        emitter.velocity.coneAngle = 0;
        emitter.killRules = &exact[r];
        emitter.killRuleCount = 1;
        Compare(gpu, kernels,
                &(Run){.emitters = &emitter, .emitterCount = 1, .least = 20, .killing = true},
                exactNames[r]);
    }
}

static void TestDepth(Gpu* gpu, mvfxGpu* kernels)
{
    // A wall square to a perspective camera's axis, a nearer square on it
    // (one device depth each), particles flying into it, normals made
    // from the neighbors; then reversed depth under a camera turned 10
    // degrees about Y, normals given, killing those that leave a narrow
    // view; then terraces under an orthographic camera looking down,
    // particles falling onto them.
    static const float perspective[16] = {1.7320508f, 0,          0,  0, 0, 1.7320508f,  0, 0, 0,
                                          0,          -1.002004f, -1, 0, 0, -0.1002004f, 0};
    mvfxDepthImage image = Terraces(perspective, -5.5f, -5.0f, NULL, NULL);
    image.origin[1] = 0.5;
    image.origin[2] = 4.0;
    mvfxEmitterDef emitter = Sprite();
    emitter.velocity.direction[1] = 0;
    emitter.velocity.direction[2] = -1;
    emitter.velocity.coneAngle = 0.3f;
    emitter.velocity.speed = (mvfxRange){3, 5};
    emitter.lifetime = mvfxRangeSource(0.6f, 0.9f);
    emitter.collision = (mvfxCollision){.enable = true,
                                        .restitution = 0.5f,
                                        .friction = 0.1f,
                                        .radius = 0.02f,
                                        .enableDepthImage = true,
                                        .depthThickness = 0.3f,
                                        .depthSamples = 16};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .colliding = true, .depth = &image},
            "a wall and a square of a perspective depth image");
    static const float reversed[16] = {3, 0, 0,         0,  0, 3, 0,          0,
                                       0, 0, 0.002004f, -1, 0, 0, 0.1002004f, 0};
    static const float tilted[3] = {0, 0.6f, 0.8f};
    static const float square[3] = {0, 0, 1};
    image = Terraces(reversed, -5.5f, -5.0f, tilted, square);
    image.reversed = true;
    image.origin[0] = 0.7;
    image.origin[1] = 0.5;
    image.origin[2] = 4.0;
    image.rotation[1] = 0.08715574f;
    image.rotation[3] = 0.99619470f;
    emitter.collision.killOutsideView = true;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .colliding = true,
                   .killing = true,
                   .depth = &image},
            "reversed depth, normals given, killed out of view");
    static const float orthographic[16] = {0.5f, 0,           0, 0, 0, 0.5f,         0, 0, 0,
                                           0,    -0.1010101f, 0, 0, 0, -0.01010101f, 1};
    image = Terraces(orthographic, -3.6f, -3.3f, NULL, NULL);
    image.origin[1] = 3.0;
    image.rotation[0] = -0.70710678f;
    image.rotation[3] = 0.70710678f;
    emitter = Sprite();
    emitter.lifetime = mvfxRangeSource(0.6f, 0.9f);
    emitter.collision = (mvfxCollision){.enable = true,
                                        .restitution = 0.4f,
                                        .radius = 0.02f,
                                        .enableDepthImage = true,
                                        .depthThickness = 0.3f,
                                        .depthSamples = 16};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .colliding = true, .depth = &image},
            "terraces of an orthographic depth image");
    // A pole of two columns before a far wall, particles crossing it fast
    // between the two, behind it by less than the thickness and before the
    // wall: a step's one sample by its length (a thickness of 2) misses
    // the pole, its samples a pixel apart meet it.
    image = Terraces(perspective, -7.0f, -7.0f, NULL, NULL);
    float pole = DeviceDepth(perspective, -4.0f);
    for (int32_t r = 0; r < DEPTH_SIDE; ++r)
    {
        SetDepthPixel(30, r, pole);
        SetDepthPixel(31, r, pole);
    }
    image.origin[2] = 5.0;
    emitter = Sprite();
    emitter.velocity.direction[0] = 1;
    emitter.velocity.direction[1] = 0;
    emitter.velocity.coneAngle = 0.05f;
    emitter.velocity.speed = (mvfxRange){20, 25};
    emitter.motion.gravity[1] = 0;
    emitter.collision = (mvfxCollision){.enable = true,
                                        .restitution = 0.5f,
                                        .radius = 0.02f,
                                        .enableDepthImage = true,
                                        .depthThickness = 2.0f,
                                        .depthSamples = 64};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .colliding = true, .depth = &image},
            "a pole crossed fast, sampled a pixel apart");
    // A step whose emitter meets the instance's depth image with no depth
    // texture given: refused, no pass added.
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = &emitter, .emitterCount = 1}, &effect);
    mvfxDepthImage bare = image;
    bare.pixels = NULL;
    bare.byteCount = 0;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mvfxInstance_SetDepthImage(RunContext(), PlannedInstance(), &bare) == mvfx_success &&
              mvfxContext_StepTicks(RunContext(), StepTicks()) == mvfx_success &&
              mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
              mvfxGpuSim_AddPasses(sim) == mvfx_errorState &&
              mvfxGpuSim_SetDepthTextures(NULL, (mrhiResourceId){0}, (mrhiResourceId){0}) ==
                  mvfx_errorInvalid,
          "a depth image without its texture refused");
    (void)mrhiDevice_DropFrame(gpu->device);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

// Heights over 9 by 9 points, a gentle swell.
static float s_heights[81];
// Signed distances over 8 points each way to a sphere of radius 0.25 at
// the middle of a box of half size 0.4.
static float s_distances[512];

static mvfxGridDef HeightGrid(mvfxGridFormat format)
{
    for (int32_t p = 0; p < 81; ++p)
    {
        s_heights[p] = 0.15f * sinf(1.3f * (float)(p % 9)) + 0.1f * cosf(0.9f * (float)(p / 9));
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 9;
    def.size[2] = 9;
    def.format = format;
    def.values = s_heights;
    return def;
}

static mvfxGridDef DistanceGrid(mvfxGridFormat format)
{
    for (int32_t p = 0; p < 512; ++p)
    {
        float x = -0.4f + 0.8f * (float)(p % 8) / 7.0f;
        float y = -0.4f + 0.8f * (float)(p / 8 % 8) / 7.0f;
        float z = -0.4f + 0.8f * (float)(p / 64) / 7.0f;
        s_distances[p] = sqrtf(x * x + y * y + z * z) - 0.25f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 8;
    def.size[1] = 8;
    def.size[2] = 8;
    def.format = format;
    def.values = s_distances;
    return def;
}

// A comb: heights up and down at each point along X.
static float s_comb[81];

static mvfxGridDef CombGrid(void)
{
    for (int32_t p = 0; p < 81; ++p)
    {
        s_comb[p] = (p % 9) % 2 != 0 ? 0.2f : -0.2f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 9;
    def.size[2] = 9;
    def.values = s_comb;
    return def;
}

static void TestGridColliders(Gpu* gpu, mvfxGpu* kernels)
{
    // A heightfield floor, a narrower one as a ceiling (inside: the
    // particles kept under it) and a ball of a distance grid in the
    // particles' way, the emitter's own, in the instance's space under a
    // moving instance that grows (many births start above the ceiling or
    // in the ball, put back on the surface); then the same of the
    // instance's, in binary16, the simulation's room for their grids
    // exact (the floor's and the ceiling's one grid placed once), the
    // order swapped at half time (each grid then at the other's place).
    const mvfxGridDef grids[2] = {HeightGrid(mvfx_gridFloat32), DistanceGrid(mvfx_gridFloat16)};
    static const mvfxCollider own[3] = {{.kind = mvfx_colliderHeightfield,
                                         .space = mvfx_spaceLocal,
                                         .center = {0, -0.4f, 0},
                                         .halfExtents = {1, 0.1f, 1},
                                         .rotation = {0, 0, 0, 1},
                                         .grid = {.index1 = 1}},
                                        {.kind = mvfx_colliderDistance,
                                         .space = mvfx_spaceLocal,
                                         .center = {-0.3f, 0.1f, 0.2f},
                                         .halfExtents = {0.4f, 0.4f, 0.4f},
                                         .rotation = {0, 0.19509032f, 0, 0.98078528f},
                                         .grid = {.index1 = 2}},
                                        {.kind = mvfx_colliderHeightfield,
                                         .space = mvfx_spaceLocal,
                                         .inside = true,
                                         .center = {0.1f, 0.35f, 0},
                                         .halfExtents = {0.6f, 1, 0.6f},
                                         .rotation = {0, 0, 0, 1},
                                         .grid = {.index1 = 1}}};
    mvfxEmitterDef emitter = Sprite();
    emitter.colliders = own;
    emitter.colliderCount = 3;
    emitter.collision = (mvfxCollision){
        .enable = true, .restitution = 0.5f, .friction = 0.1f, .restSpeed = 0.3f, .radius = 0.02f};
    emitter.lifetime = mvfxRangeSource(0.6f, 0.9f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .pace = 0.01,
                   .colliding = true,
                   .colliderGrids = grids,
                   .colliderGridCount = 2,
                   .tolerance = 1e-3f},
            "heightfields and a distance grid bounced off, a growing instance");
    const mvfxGridDef halves[2] = {HeightGrid(mvfx_gridFloat16), DistanceGrid(mvfx_gridFloat16)};
    static const mvfxCollider swapped[3] = {own[1], own[0], own[2]};
    emitter.colliders = NULL;
    emitter.colliderCount = 0;
    emitter.collision.hostColliders = true;
    // 81 halves rounded up to 41 words, and 512 halves.
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .colliding = true,
                   .colliderGrids = halves,
                   .colliderGridCount = 2,
                   .hostColliders = own,
                   .hostColliderCount = 3,
                   .hostGridBytes = 4 * 41 + 1024,
                   .laterHostColliders = swapped,
                   .laterHostColliderCount = 3,
                   .tolerance = 1e-3f},
            "the instance's grid colliders, swapped at half time");
    // Particles flying along a comb's teeth at 15 to 20 a second, a step
    // longer than a cell: sampled each half cell, a tooth's tip is met
    // that a sample each cell would pass. A step crossing two teeth may
    // be halved toward a crossing on another part of a facet (the
    // samples' bracket placed a hair apart): places within 5e-3.
    const mvfxGridDef comb = CombGrid();
    static const mvfxCollider teeth = {.kind = mvfx_colliderHeightfield,
                                       .center = {0, -0.1f, 0},
                                       .halfExtents = {1, 0.1f, 1},
                                       .rotation = {0, 0, 0, 1},
                                       .grid = {.index1 = 1}};
    emitter = Sprite();
    emitter.velocity.direction[0] = 1;
    emitter.velocity.direction[1] = 0.1f;
    emitter.velocity.coneAngle = 0.2f;
    emitter.velocity.speed = (mvfxRange){15, 20};
    emitter.colliders = &teeth;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enable = true, .restitution = 0.5f, .radius = 0.02f};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .colliding = true,
                   .colliderGrids = &comb,
                   .colliderGridCount = 1,
                   .tolerance = 5e-3f},
            "a comb's teeth met by fast particles");
}

static void TestFull(Gpu* gpu, mvfxGpu* kernels)
{
    // An emitter of 64 owed about 180 at a time, and a burst of 150 at one
    // tick: full every step, the room for each batch as the CPU path's
    // heap gives it.
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 64;
    emitter.bursts = s_floods;
    emitter.burstCount = 2;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 64},
            "an emitter full every step");
    // Scans of many blocks of 1,024: about 2,400 live of 3,000, deaths
    // scattered among them by lives of 0.1 to 0.3 seconds; then 1,500
    // owed 500 a step, full, refusing across blocks.
    emitter = Sprite();
    emitter.capacity = 3000;
    emitter.rate = 12000.0f;
    emitter.lifetime = mvfxRangeSource(0.1f, 0.3f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 2000},
            "an emitter of many blocks");
    emitter.capacity = 1500;
    emitter.rate = 30000.0f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 1500},
            "an emitter of many blocks, full");
    // 4,096 owed a step of an emitter of 4,096 living half a second to a
    // second: full from the first steps, its room only its deaths', no
    // birth dying in its step (the room decided by the allowance).
    emitter = Sprite();
    emitter.capacity = 4096;
    emitter.burstCount = 0;
    emitter.rate = 4096.0f * 60.0f;
    emitter.lifetime = mvfxRangeSource(0.5f, 1.0f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 12, .least = 4096},
            "an emitter owing its capacity a step, full");
    // Lives past 2^32 ticks (4,096 s and 25 ms) of an emitter full from
    // its first step: none dies in a plan, though their deaths' low words
    // fall within the step.
    emitter = Sprite();
    emitter.capacity = 64;
    emitter.burstCount = 0;
    emitter.rate = 64.0f * 120.0f;
    emitter.lifetime = mvfxRangeSource(4096.025f, 4096.025f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 64},
            "an emitter full of lives past 2^32 ticks");
    // Half-second steps: a full emitter whose particles all die in the
    // second step, which owes half its capacity refused before they die
    // and its capacity after: a sequence past twice the capacity, its
    // last kept beyond it.
    SetStepTicks(MVFX_TICKS_PER_SECOND / 2);
    emitter = Bursts(s_turnover, 3);
    emitter.capacity = 1000;
    emitter.lifetime = mvfxRangeSource(0.3f, 0.3f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 1000},
            "a sequence past twice the capacity");
    // The same at 2^17: more than 256 blocks, the offsets carried across
    // chunks.
    emitter.capacity = 1 << 17;
    emitter.bursts = s_turnoverLarge;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 1 << 17},
            "more than 256 blocks");
    SetStepTicks(STEP_TICKS);
}

// Bursts on ticks of exact eighths of a second; lives of a quarter.
static const mvfxBurst s_exact[3] = {
    {.time = 0.125f, .minCount = 40, .maxCount = 40, .cycles = 1, .probability = 1.0f},
    {.time = 0.375f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f},
    {.time = 0.625f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f}};
// Bursts across two half-second plans: the second plan's first batch is all
// refused, held out by the first plan's 32, and dies on its last batch's
// tick.
static const mvfxBurst s_shared[4] = {
    {.time = 0.375f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f},
    {.time = 0.5625f, .minCount = 16, .maxCount = 16, .cycles = 1, .probability = 1.0f},
    {.time = 0.625f, .minCount = 24, .maxCount = 24, .cycles = 1, .probability = 1.0f},
    {.time = 0.8125f, .minCount = 16, .maxCount = 16, .cycles = 1, .probability = 1.0f}};

// An emitter of 32, bursts only, lives of a quarter of a second.
static mvfxEmitterDef Bursts(const mvfxBurst* bursts, int32_t burstCount)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 32;
    emitter.rate = 0.0f;
    emitter.bursts = bursts;
    emitter.burstCount = burstCount;
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    return emitter;
}

static void TestBoundaries(Gpu* gpu, mvfxGpu* kernels)
{
    // Steps of a quarter: each burst's 32 die at the next burst's tick in
    // the next plan, making it room.
    mvfxEmitterDef emitter = Bursts(s_exact, 3);
    SetStepTicks(MVFX_TICKS_PER_SECOND / 4);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 4, .least = 32},
            "live particles dying on a batch's tick");
    // Steps of half a second: the first burst's 40 leaves 8 the plan
    // refuses; its 32 die within the plan at the second's tick.
    SetStepTicks(MVFX_TICKS_PER_SECOND / 2);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 32},
            "births dying on a later batch's tick of their plan");
    // The refused 16 die on the last batch's tick, where the 24 still live:
    // they leave it room for 8, not 24.
    emitter = Bursts(s_shared, 4);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 32},
            "refused births taking no room");
    SetStepTicks(STEP_TICKS);
    // Lives of 4,096 seconds, 2^32 ticks, born early in their steps:
    // deaths just short of 2^32 ticks after the plan's start. A life 1,024
    // ticks longer: deaths whose high word is 1 and low word falls within
    // the plan. And longer ones.
    emitter = Sprite();
    emitter.rate = 60.0f;
    emitter.lifetime = mvfxRangeSource(4096.0f, 4096.0f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 50},
            "lives of 2^32 ticks");
    emitter.lifetime = mvfxRangeSource(4096.0009765625f, 4096.0009765625f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 50},
            "lives of 2^32 ticks and 1,024");
    emitter.lifetime = mvfxRangeSource(5000.0f, 6000.0f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 50},
            "lives past 2^32 ticks");
    // Steps of a second to ages past 2^32 ticks, the plan's start's low
    // word wrapping past births' (spin read over life at those ages),
    // without gravity or drag; records of the age and of frames at a rate,
    // exactly the CPU path's.
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 9, .interpolation = mvfx_interpolationLinear}};
    emitter.rate = 0.1f;
    emitter.burstCount = 0;
    emitter.motion.gravity[1] = 0.0f;
    emitter.motion.drag = 0.0f;
    emitter.velocity.speed = (mvfxRange){0.001f, 0.002f};
    emitter.spin = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {1e-3f, 2e-3f}};
    emitter.overLife.rotation = (mvfxRotationOverLife){
        .enable = true, .scale = {.value = 1, .curve = {.keys = keys, .keyCount = 2}}};
    emitter.overLife.frame = (mvfxFrameOverLife){
        .animation = mvfx_frameAtRate, .frameCount = 1000, .frameRate = 7.3f, .loop = true};
    static const mvfxLayoutField fields[3] = {
        {.field = mvfx_recordAge, .format = mvfx_formatFloat32, .components = 1},
        {.field = mvfx_recordFrame, .format = mvfx_formatUint32, .components = 1, .offset = 4},
        {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 8}};
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = fields;
    layout.fieldCount = 3;
    layout.stride = 12;
    SetStepTicks(MVFX_TICKS_PER_SECOND);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .steps = 4200,
                   .least = 50,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .exact = true},
            "ages past 2^32 ticks");
    SetStepTicks(STEP_TICKS);
}

static void TestBoundsRefused(mvfxGpu* kernels)
{
    // Reading a record: refused for a NULL argument or an emitter out of
    // range, the bounds empty; a record with no particles is empty.
    mvfxEmitterDef emitter = Sprite();
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = &emitter, .emitterCount = 1}, &effect);
    uint32_t record[MVFX_GPU_STATISTICS_WORDS] = {0};
    mvfxBounds box = {.empty = false};
    CHECK(mvfxGpuSim_GetBounds(sim, 0, record, &box) == mvfx_success && box.empty,
          "no particles, empty");
    box.empty = false;
    CHECK(mvfxGpuSim_GetBounds(sim, 1, record, &box) == mvfx_errorInvalid && box.empty, "past");
    box.empty = false;
    CHECK(mvfxGpuSim_GetBounds(sim, -1, record, &box) == mvfx_errorInvalid && box.empty,
          "negative");
    box.empty = false;
    CHECK(mvfxGpuSim_GetBounds(NULL, 0, record, &box) == mvfx_errorInvalid && box.empty,
          "no simulation");
    box.empty = false;
    CHECK(mvfxGpuSim_GetBounds(sim, 0, NULL, &box) == mvfx_errorInvalid && box.empty, "no record");
    CHECK(mvfxGpuSim_GetBounds(sim, 0, record, NULL) == mvfx_errorInvalid, "no bounds");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

static void TestHostGridRefused(Gpu* gpu, mvfxGpu* kernels)
{
    // The instance given a heightfield whose grid (16 bytes) passes the
    // room the simulation keeps (12): the step adds no pass, not even
    // the first emitter's, which meets none of the instance's colliders
    // (its particles stay as the step before left them). The bytes a set of colliders needs: a
    // grid named twice counted once, a sphere none.
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[1].collision = (mvfxCollision){.enable = true, .hostColliders = true};
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(
        kernels, &(Run){.emitters = emitters, .emitterCount = 2, .hostGridBytes = 12}, &effect);
    static const float heights[4] = {0, 0.1f, 0.2f, 0.3f};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[2] = 2;
    gridDef.values = heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(RunContext(), &gridDef, &grid) == mvfx_success, "a grid");
    const mvfxCollider colliders[3] = {{.kind = mvfx_colliderHeightfield,
                                        .halfExtents = {1, 1, 1},
                                        .rotation = {0, 0, 0, 1},
                                        .grid = grid},
                                       {.kind = mvfx_colliderSphere, .radius = 1, .grid = grid},
                                       {.kind = mvfx_colliderDistance,
                                        .halfExtents = {1, 1, 1},
                                        .rotation = {0, 0, 0, 1},
                                        .grid = grid}};
    int64_t bytes = -1;
    CHECK(mvfxContext_GetGpuHostGridBytes(RunContext(), colliders, 3, &bytes) == mvfx_success &&
              bytes == 16 &&
              mvfxContext_GetGpuHostGridBytes(RunContext(), colliders + 1, 1, &bytes) ==
                  mvfx_success &&
              bytes == 0 &&
              mvfxContext_GetGpuHostGridBytes(RunContext(), NULL, 0, &bytes) == mvfx_success,
          "the bytes a set of colliders needs");
    const mvfxCollider gone = {.kind = mvfx_colliderHeightfield,
                               .halfExtents = {1, 1, 1},
                               .rotation = {0, 0, 0, 1},
                               .grid = {.index1 = 999, .generation = 1}};
    CHECK(mvfxContext_GetGpuHostGridBytes(RunContext(), &gone, 1, &bytes) == mvfx_errorStale &&
              bytes == 0 &&
              mvfxContext_GetGpuHostGridBytes(NULL, colliders, 1, &bytes) == mvfx_errorInvalid &&
              mvfxContext_GetGpuHostGridBytes(RunContext(), colliders, 1, NULL) ==
                  mvfx_errorInvalid &&
              mvfxContext_GetGpuHostGridBytes(RunContext(), NULL, 1, &bytes) == mvfx_errorInvalid &&
              mvfxContext_GetGpuHostGridBytes(RunContext(), colliders, -1, &bytes) ==
                  mvfx_errorInvalid &&
              mvfxContext_GetGpuHostGridBytes(RunContext(), colliders, MVFX_MAX_COLLIDERS + 1,
                                              &bytes) == mvfx_errorLimit,
          "the bytes refused");
    CHECK(mvfxContext_StepTicks(RunContext(), StepTicks()) == mvfx_success && Simulate(gpu, sim) &&
              ReadBack(gpu, sim, 0) && StatisticsRecord()[0] > 0,
          "a step before");
    int32_t before = (int32_t)StatisticsRecord()[0];
    CHECK(mvfxInstance_SetColliders(RunContext(), PlannedInstance(), colliders, 1) ==
                  mvfx_success &&
              mvfxContext_StepTicks(RunContext(), StepTicks()) == mvfx_success,
          "stepped with a heightfield");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
              mvfxGpuSim_AddPasses(sim) == mvfx_errorCapacity,
          "a heightfield of the instance past the room refused");
    CHECK(mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
              mvfxGpuSim_Record(sim) == mvfx_success && Finish(gpu) && ReadBack(gpu, sim, 0) &&
              StatisticsRecord()[0] == (uint32_t)before,
          "a refused step adds no pass");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

// A host module that leaves particles as they are.
static void Untouched(const mvfxModuleCall* call, void* user)
{
    (void)call;
    (void)user;
}

static void TestRefused(mvfxGpu* kernels)
{
    // An emitter it does not run, after one it does: refused, named (a
    // feature it does not run, the host's modules); a gas.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    const mvfxHostModule module = {.stage = mvfx_stageAfterMotion, .function = Untouched};
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[1].hostModules = &module;
    emitters[1].hostModuleCount = 1;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId cpu = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    mvfxInstanceId planned = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success, "planned");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = planned;
    mvfxGpuSim* sim = (mvfxGpuSim*)&sim;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorUnsupported && sim == NULL &&
              refusal.emitter == 1 && refusal.features == MVFX_FEATURE_HOST_MODULES,
          "an emitter it does not run, named");
    simDef.instance = cpu;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorInvalid && refusal.emitter == -1,
          "an instance that is not planned");
    // A smoke gas about an obstacle: its effect is planned and its gas
    // run, no gas named.
    static const mvfxCollider ball = {
        .kind = mvfx_colliderSphere, .radius = 0.1f, .rotation = {0, 0, 0, 1}};
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 4;
    gas.size[1] = 4;
    gas.size[2] = 4;
    gas.obstacles = &ball;
    gas.obstacleCount = 1;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "a gas's effect");
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success, "a gas planned");
    simDef.effect = effect;
    simDef.instance = planned;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success && sim != NULL &&
              refusal.emitter == -1 && refusal.gas == -1 && refusal.features == 0,
          "a gas with an obstacle run");
    mvfxDestroyGpuSim(sim);
    // A surface gas, planned: run.
    gas.obstacleCount = 0;
    gas.kind = mvfx_gasSurface;
    gas.surface.emitter = 0;
    gas.surface.radius = 0.1f;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success,
          "a surface gas's effect");
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success,
          "a surface gas planned");
    simDef.effect = effect;
    simDef.instance = planned;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success && sim != NULL &&
              refusal.emitter == -1 && refusal.gas == -1,
          "a surface gas run");
    mvfxDestroyGpuSim(sim);
    // Emitters coupled to a surface: run, whether their particles only
    // feel it, push it, pour into it or write water events.
    mvfxWaterDef waters[2] = {mvfxDefaultWaterDef(), mvfxDefaultWaterDef()};
    waters[1].model = mvfx_waterShallow;
    effectDef.gasCount = 0;
    effectDef.waters = waters;
    effectDef.waterCount = 2;
    mvfxEmitterDef coupled[4] = {Sprite(), Sprite(), Sprite(), Sprite()};
    for (int32_t c = 0; c < 4; ++c)
    {
        coupled[c].water = (mvfxWaterCoupling){
            .enable = true, .water = c == 2 ? 1 : 0, .radius = 0.05f, .drag = 1.0f};
    }
    coupled[1].water.push = 0.1f;
    coupled[2].water.pour = true;
    coupled[3].events = mvfx_eventMaskWater;
    coupled[3].eventCapacity = 16;
    bool runs = true;
    for (int32_t c = 0; c < 4; ++c)
    {
        effectDef.emitters = &coupled[c];
        bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
        instanceDef.effect = effect;
        made = made && mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success;
        simDef.effect = effect;
        simDef.instance = planned;
        mvfxResult result = mvfxCreateGpuSim(&simDef, &sim, &refusal);
        runs = runs && made && result == mvfx_success && refusal.features == 0;
        mvfxDestroyGpuSim(sim);
    }
    CHECK(runs, "emitters coupled to a surface run");
    mvfxDestroyContext(context);
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestSprite(gpu, kernels);
    TestMoving(gpu, kernels);
    TestSpeedHeld(gpu, kernels);
    TestForces(gpu, kernels);
    TestTurbulence(gpu, kernels);
    TestVectorFields(gpu, kernels);
    TestCollision(gpu, kernels);
    TestGridColliders(gpu, kernels);
    TestOldDeaths(gpu, kernels);
    TestDepth(gpu, kernels);
    TestValues(gpu, kernels);
    TestTurns(gpu, kernels);
    TestFastTurns(gpu, kernels);
    TestModules(gpu, kernels);
    TestKillRules(gpu, kernels);
    TestSpawning(gpu, kernels);
    TestHostSpawn(gpu, kernels);
    TestFull(gpu, kernels);
    TestBoundaries(gpu, kernels);
    TestRefused(kernels);
    TestBoundsRefused(kernels);
    TestHostGridRefused(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
