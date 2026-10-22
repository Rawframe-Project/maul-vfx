// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's C blocks (docs/guide.md), each in a function as it stands
// there, run: tools/check_guide.py checks that every block appears here
// unchanged, so a block that no longer builds or runs fails the tests.

#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/file.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"
#include "maul-vfx/water.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// A first effect: a fountain stepped for a second and written out.
static bool FirstEffect(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }

    // One emitter: 50 particles a second, each living two seconds, thrown
    // up at 3 to 4 units a second under gravity.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 128;
    emitter.rate = 50.0f;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {0.0f, 1.0f, 0.0f},
                                      .coneAngle = 0.2f,
                                      .speed = {3.0f, 4.0f}};
    emitter.motion.gravity[1] = -9.8f;

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

    // A second in steps of a sixtieth.
    for (int32_t step = 0; made && step < 60; ++step)
    {
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    }

    // Each particle as 28 bytes: its place, then its colour.
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 12},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 28;
    mvfxLayoutId layout = {0};
    made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success;

    static float records[128][7];
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t count = 0;
    made = made && mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0,
                                               records, 128, &count) == mvfx_success;
    // About 50 live: a second at 50 a second, none yet old enough to die.
    bool fountain = made && count >= 45 && count <= 55;
    mvfxDestroyContext(context);
    return fountain;
}

// Effects in files: written, created from the bytes, a damaged file refused.
static bool EffectFile(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 64;
    emitter.rate = 20.0f;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;

    // Its size first, then the bytes.
    int64_t size = 0;
    mvfxFileResult sized = mvfxWriteEffect(&def, NULL, 0, NULL, 0, &size);
    static unsigned char file[4096];
    if (sized.result != mvfx_errorCapacity || size > (int64_t)sizeof file ||
        mvfxWriteEffect(&def, NULL, 0, file, size, &size).result != mvfx_success)
    {
        return false;
    }

    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    mvfxEffectId effect = {0};
    bool made =
        mvfxCreateEffectFromBytes(context, file, size, NULL, 0, &effect).result == mvfx_success;

    // A byte changed: refused, at an offset in the file.
    file[size / 2] ^= 0x40u;
    mvfxEffectId damaged = {0};
    mvfxFileResult refused = mvfxCreateEffectFromBytes(context, file, size, NULL, 0, &damaged);
    made = made && refused.result == mvfx_errorInvalid && refused.offset >= 0;
    mvfxDestroyContext(context);
    return made;
}

// Instances: placed far off, turned and scaled, bounded, stopped and
// finished.
static bool Instances(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 64;
    emitter.rate = 30.0f;
    emitter.lifetime = mvfxRangeSource(0.5f, 0.5f);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

    // Far from the world's origin, turned a quarter about Y, twice the size;
    // placed there at once.
    const double far[3] = {100000.0, 0.0, -25000.0};
    const float turn[4] = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
    made =
        made && mvfxInstance_SetTransform(context, instance, far, turn, 2.0f, true) == mvfx_success;
    for (int32_t step = 0; made && step < 30; ++step)
    {
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    }
    mvfxBounds bounds;
    made = made && mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success &&
           !bounds.empty && bounds.min[0] <= far[0] && bounds.max[0] >= far[0];

    // Stopped: no more births; the last particles live out their half second.
    made = made && mvfxInstance_Stop(context, instance, mvfx_stopEmitting) == mvfx_success;
    int32_t steps = 0;
    while (made && !mvfxInstance_IsFinished(context, instance) && steps < 120)
    {
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
        ++steps;
    }
    made = made && mvfxInstance_IsFinished(context, instance) && steps <= 31;
    mvfxDestroyContext(context);
    return made;
}

// Output: records sorted back to front for a view, and the same particles
// expanded into quads.
static bool Output(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 32;
    emitter.rate = 60.0f;
    emitter.velocity = (mvfxVelocity){
        .mode = mvfx_directionFixed, .direction = {0.0f, 0.0f, 1.0f}, .speed = {1.0f, 3.0f}};
    emitter.sortMode = mvfx_sortBackToFront;
    emitter.sortMeasure = mvfx_sortByDepth;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success &&
           mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success;

    // A view looking down -Z from 10 units out: the farthest first.
    const mvfxView view = {
        .eye = {0.0, 0.0, 10.0}, .forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    static const mvfxLayoutField place = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &place;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    int64_t scratchBytes = 0;
    made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
           mvfxInstance_GetSortScratchBytes(context, instance, 0, &scratchBytes) == mvfx_success;
    static uint32_t scratch[32 * 4];
    static float records[32][3];
    int64_t count = 0;
    made =
        made && scratchBytes <= (int64_t)sizeof scratch &&
        mvfxInstance_WriteParticles(context, instance, 0, layout, &view, scratch,
                                    (int64_t)sizeof scratch, records, 32, &count) == mvfx_success;
    for (int64_t i = 1; made && i < count; ++i)
    {
        // Back to front: each no nearer the eye along Z than the one after.
        made = records[i - 1][2] <= records[i][2];
    }

    // The same particles as camera-facing quads: a vertex's place and its
    // corner.
    static const mvfxLayoutField corners[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 12},
    };
    layoutDef.fields = corners;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 20;
    mvfxLayoutId vertexLayout = {0};
    static float vertices[4 * 32][5];
    static uint32_t indices[6 * 32];
    const mvfxExpandBuffers buffers = {.vertices = vertices,
                                       .vertexCapacity = 4 * 32,
                                       .indices = indices,
                                       .indexCapacity = 6 * 32,
                                       .scratch = scratch,
                                       .scratchBytes = (int64_t)sizeof scratch};
    int64_t quads = 0;
    made = made && mvfxCreateLayout(context, &layoutDef, &vertexLayout) == mvfx_success &&
           mvfxInstance_ExpandSprites(context, instance, 0, vertexLayout, &view, &buffers,
                                      &quads) == mvfx_success &&
           quads == count && count > 0;
    mvfxDestroyContext(context);
    return made;
}

// Ribbons: a trail behind a moving source, expanded into a strip.
static bool Ribbons(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    // A trail: a point every thirtieth of a second, living a second, chained
    // into one ribbon whose head is pinned to the source.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 64;
    emitter.rate = 30.0f;
    emitter.lifetime = mvfxRangeSource(1.0f, 1.0f);
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.ribbon.pieces = 2;
    emitter.ribbon.pinHead = true;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

    // The source moves along X; its trail follows.
    static const float still[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int32_t step = 0; made && step < 30; ++step)
    {
        const double at[3] = {0.1 * (double)step, 0.0, 0.0};
        made = mvfxInstance_SetTransform(context, instance, at, still, 1.0f, step == 0) ==
                   mvfx_success &&
               mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 30) == mvfx_success;
    }

    // The strip: each vertex's place and its coordinate along the ribbon.
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 12},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 16;
    mvfxLayoutId layout = {0};
    static float vertices[512][4];
    static uint32_t indices[1536];
    static uint32_t scratch[64 * 4];
    const mvfxExpandBuffers buffers = {.vertices = vertices,
                                       .vertexCapacity = 512,
                                       .indices = indices,
                                       .indexCapacity = 1536,
                                       .scratch = scratch,
                                       .scratchBytes = (int64_t)sizeof scratch};
    const mvfxView view = {
        .eye = {1.5, 0.0, 10.0}, .forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
           mvfxInstance_ExpandRibbons(context, instance, 0, layout, &view, &buffers, &vertexCount,
                                      &indexCount) == mvfx_success;
    // About 30 points, two pieces a segment: a strip of two vertices a point.
    made =
        made && vertexCount > 100 && vertexCount <= 512 && indexCount > 0 && vertexCount % 2 == 0;
    mvfxDestroyContext(context);
    return made;
}

// Events: rockets bursting into sparks, their deaths read back, and the
// host's own markers.
static bool Events(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    // Rockets: two a second, rising for a second, writing their deaths.
    mvfxEmitterDef emitters[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef()};
    emitters[0].capacity = 8;
    emitters[0].rate = 2.0f;
    emitters[0].lifetime = mvfxRangeSource(1.0f, 1.0f);
    emitters[0].velocity = (mvfxVelocity){
        .mode = mvfx_directionFixed, .direction = {0.0f, 1.0f, 0.0f}, .speed = {10.0f, 10.0f}};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 8;
    // Sparks: 20 from each rocket's death, with half its velocity.
    emitters[1].capacity = 256;
    emitters[1].rate = 0.0f;
    emitters[1].lifetime = mvfxRangeSource(0.5f, 0.8f);
    emitters[1].velocity.speed = (mvfxRange){2.0f, 4.0f};
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 20,
                                              .maxCount = 20,
                                              .inheritVelocity = 0.5f,
                                              .capacity = 64};
    // Markers the host places itself.
    emitters[2].capacity = 4;
    emitters[2].rate = 0.0f;
    emitters[2].hostSpawnCapacity = 4;
    emitters[2].lifetime = mvfxRangeSource(10.0f, 10.0f);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = 3;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

    static const double marks[6] = {1.0, 0.0, 0.0, -1.0, 0.0, 0.0};
    const mvfxSpawnRequest request = {.count = 2, .positions = marks};
    made = made && mvfxInstance_Spawn(context, instance, 2, &request) == mvfx_success;

    // Two seconds: each step's deaths read back after it.
    int32_t deaths = 0;
    for (int32_t step = 0; made && step < 120; ++step)
    {
        const mvfxEvent* events = NULL;
        int32_t count = 0;
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
               mvfxInstance_GetEvents(context, instance, 0, &events, &count) == mvfx_success;
        deaths += count;
    }
    mvfxEmitterStats sparks;
    mvfxEmitterStats markers;
    made = made && mvfxInstance_GetEmitterStats(context, instance, 1, &sparks) == mvfx_success &&
           mvfxInstance_GetEmitterStats(context, instance, 2, &markers) == mvfx_success;
    // The first rockets died, each into 20 sparks; both markers live.
    made = made && deaths >= 1 && sparks.live > 0 && markers.live == 2;
    mvfxDestroyContext(context);
    return made;
}

// Interaction: a flock turned one way by alignment.
static bool Flock(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    // 32 birds at once, in a ball of radius 2, flying at 1 within 1.5
    // radians of +X.
    static const mvfxBurst burst = {
        .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 32;
    emitter.rate = 0.0f;
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.shape.kind = mvfx_shapeSphere;
    emitter.shape.radius = 2.0f;
    emitter.shape.thickness = 1.0f;
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {1.0f, 0.0f, 0.0f},
                                      .coneAngle = 1.5f,
                                      .speed = {1.0f, 1.0f}};
    // Each sees the rest of its own emitter within 4, in every direction.
    emitter.neighbors =
        (mvfxNeighborDef){.enabled = true, .source = -1, .radius = 4.0f, .maxNeighbors = 32};
    emitter.steering.enabled = true;
    emitter.steering.separation = 1.0f;
    emitter.steering.separationRadius = 0.5f;
    emitter.steering.alignment = 2.0f;
    emitter.steering.alignmentRadius = 4.0f;
    emitter.steering.cohesion = 0.5f;
    emitter.steering.cohesionRadius = 4.0f;
    emitter.steering.viewAngle = 3.14159265f;
    emitter.steering.maxForce = 4.0f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

    static const mvfxLayoutField field = {
        .field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &field;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success;

    // The order after the first step, then after three seconds more.
    double order[2] = {0.0, 0.0};
    for (int32_t pass = 0; made && pass < 2; ++pass)
    {
        for (int32_t step = 0; made && step < (pass == 0 ? 1 : 180); ++step)
        {
            made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
        }
        static float velocities[32][3];
        const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
        int64_t count = 0;
        made = made &&
               mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, velocities,
                                           32, &count) == mvfx_success &&
               count == 32;
        double sum[3] = {0.0, 0.0, 0.0};
        double speeds = 0.0;
        for (int64_t i = 0; made && i < count; ++i)
        {
            const double x = (double)velocities[i][0];
            const double y = (double)velocities[i][1];
            const double z = (double)velocities[i][2];
            sum[0] += x;
            sum[1] += y;
            sum[2] += z;
            speeds += sqrt(x * x + y * y + z * z);
        }
        order[pass] =
            speeds > 0.0 ? sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]) / speeds : 0.0;
    }
    // Spread at first; one way after three seconds.
    made = made && order[0] < 0.7 && order[1] > 0.9;
    mvfxDestroyContext(context);
    return made;
}

// Fluids: smoke from a hot source rises.
static bool Smoke(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    // A 2D gas of 64 by 64 cells of 0.1 about the origin, a hot source low in
    // it, heat lifting against gravity.
    static const mvfxGasSource source = {.shape = mvfx_gasSphere,
                                         .center = {0.0f, -2.5f, 0.0f},
                                         .radius = 0.4f,
                                         .density = 1.0f,
                                         .temperature = 5.0f};
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.sources = &source;
    gas.sourceCount = 1;
    gas.heatLift = 1.0f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    for (int32_t step = 0; made && step < 60; ++step)
    {
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    }

    // Above the source, and off to its side.
    static const double points[6] = {0.0, -1.5, 0.0, 2.5, -2.5, 0.0};
    float density[2] = {0.0f, 0.0f};
    float rise[2] = {0.0f, 0.0f};
    made = made &&
           mvfxInstance_SampleGas(context, instance, 0, mvfx_gasDensity, points, 2, density) ==
               mvfx_success &&
           mvfxInstance_SampleGas(context, instance, 0, mvfx_gasVelocityY, points, 2, rise) ==
               mvfx_success;
    // The smoke went up, not sideways.
    made = made && density[0] > 0.01f && rise[0] > 0.0f && density[1] < MVFX_GAS_EMPTY;
    mvfxDestroyContext(context);
    return made;
}

// Fluids: a disc of particles wrapped and meshed.
static bool SurfaceMesh(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    // 400 particles at rest on a disc of radius 1.
    static const mvfxBurst burst = {
        .minCount = 400, .maxCount = 400, .cycles = 1, .probability = 1.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 400;
    emitter.rate = 0.0f;
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.shape.kind = mvfx_shapeCircle;
    emitter.shape.radius = 1.0f;
    emitter.shape.thickness = 1.0f;
    // Their surface in a 2D gas, each particle of radius 0.1.
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasSurface;
    gas.surface.emitter = 0;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    made = made && mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;

    int64_t scratchBytes = 0;
    made = made && mvfxInstance_GetSurfaceMeshScratchBytes(context, instance, 0, &scratchBytes) ==
                       mvfx_success;
    static uint32_t scratch[1024];
    static float positions[512][3];
    static uint32_t indices[1024];
    const mvfxSurfaceMeshBuffers buffers = {.positions = &positions[0][0],
                                            .vertexCapacity = 512,
                                            .indices = indices,
                                            .indexCapacity = 1024,
                                            .scratch = scratch,
                                            .scratchBytes = sizeof scratch};
    const double origin[3] = {0.0, 0.0, 0.0};
    int64_t vertices = 0;
    int64_t indexCount = 0;
    made = made && scratchBytes <= (int64_t)sizeof scratch &&
           mvfxInstance_WriteSurfaceMesh(context, instance, 0, origin, 0.0f, &buffers, &vertices,
                                         &indexCount) == mvfx_success;
    // A closed loop, every vertex near the disc's rim.
    bool rim = made && vertices > 20 && vertices <= 512 && indexCount == 2 * vertices;
    for (int64_t i = 0; rim && i < vertices; ++i)
    {
        const float x = positions[i][0];
        const float y = positions[i][1];
        rim = x * x + y * y > 0.8f * 0.8f && x * x + y * y < 1.2f * 1.2f;
    }
    mvfxDestroyContext(context);
    return rim;
}

// Fluids: a stone pushed into still water.
static bool Water(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    // 64 by 64 cells of 0.1 about the origin, waves at 1 a second, closed
    // walls.
    mvfxWaterDef water = mvfxDefaultWaterDef();
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.waters = &water;
    effectDef.waterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

    // The stone sinks the water 0.1 at the center, within 0.5, at the next
    // step's start.
    const mvfxWaterPush stone = {.point = {0.0, 0.0, 0.0}, .radius = 0.5f, .depth = 0.1f};
    made = made && mvfxInstance_PushWater(context, instance, 0, &stone, 1) == mvfx_success;
    made = made && mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;

    // The center, and a point 1 away the waves have not reached.
    static const double points[6] = {0.0, 5.0, 0.0, 1.0, 5.0, 0.0};
    mvfxWaterSample samples[2];
    made =
        made && mvfxInstance_SampleWater(context, instance, 0, points, 2, samples) == mvfx_success;
    const bool pushed = made && samples[0].surface[1] < -0.05 && samples[1].surface[1] > 0.0;

    // A second on, the hollow has filled in and its ring has come 1 out.
    for (int32_t step = 0; made && step < 60; ++step)
    {
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    }
    made =
        made && mvfxInstance_SampleWater(context, instance, 0, points, 2, samples) == mvfx_success;
    made = made && pushed && samples[0].inside && samples[0].surface[1] > -0.02 &&
           samples[1].surface[1] < -0.005;
    mvfxDestroyContext(context);
    return made;
}

// Collision: rain on the host's ground and an emitter's ball.
static bool Rain(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    // 64 drops at once over a square 3 up, falling.
    static const mvfxBurst burst = {
        .minCount = 64, .maxCount = 64, .cycles = 1, .probability = 1.0f};
    static const mvfxCollider ball = {
        .kind = mvfx_colliderSphere, .center = {0.0f, 1.0f, 0.0f}, .radius = 0.5f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 64;
    emitter.rate = 0.0f;
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.shape.kind = mvfx_shapeBox;
    emitter.shape.offset[1] = 3.0f;
    emitter.shape.halfExtents[0] = 1.0f;
    emitter.shape.halfExtents[2] = 1.0f;
    emitter.motion.gravity[1] = -9.8f;
    emitter.colliders = &ball;
    emitter.colliderCount = 1;
    emitter.collision.enabled = true;
    emitter.collision.restitution = 0.3f;
    emitter.collision.friction = 0.5f;
    emitter.collision.sceneQueries = true;
    emitter.collision.queryCapacity = 64;
    emitter.collision.lookahead = 0.25f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

    // Two seconds; after each step the host answers every query with its
    // ground, the plane y = 0 (a real host casts each query's segment).
    for (int32_t step = 0; made && step < 120; ++step)
    {
        const mvfxSceneQuery* queries = NULL;
        int32_t count = 0;
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
               mvfxInstance_GetSceneQueries(context, instance, 0, &queries, &count) == mvfx_success;
        static mvfxSceneAnswer answers[64];
        for (int32_t i = 0; i < count; ++i)
        {
            answers[i] = (mvfxSceneAnswer){.hit = true, .normal = {0.0f, 1.0f, 0.0f}};
        }
        made = made && mvfxInstance_AnswerSceneQueries(context, instance, 0, answers, count) ==
                           mvfx_success;
    }

    static const mvfxLayoutField field = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &field;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success;
    static float places[64][3];
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t count = 0;
    made = made &&
           mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, places, 64,
                                       &count) == mvfx_success &&
           count == 64;
    // Every drop came to rest on the ground; those that struck the ball were
    // thrown off it, some past the square they fell from.
    int32_t thrown = 0;
    for (int64_t i = 0; made && i < count; ++i)
    {
        made = places[i][1] > -1e-3f && places[i][1] < 1e-2f;
        thrown += fabsf(places[i][0]) > 1.0f || fabsf(places[i][2]) > 1.0f ? 1 : 0;
    }
    made = made && thrown > 0;
    mvfxDestroyContext(context);
    return made;
}

int main(void)
{
    CHECK(FirstEffect(), "a first effect");
    CHECK(EffectFile(), "an effect in a file");
    CHECK(Instances(), "an instance placed, stopped and finished");
    CHECK(Output(), "records sorted and quads expanded");
    CHECK(Ribbons(), "a trail expanded into a strip");
    CHECK(Events(), "rockets bursting into sparks");
    CHECK(Flock(), "a flock turned one way");
    CHECK(Smoke(), "smoke rising from a hot source");
    CHECK(SurfaceMesh(), "a disc of particles meshed");
    CHECK(Water(), "a stone pushed into still water");
    CHECK(Rain(), "rain on a ground and a ball");
    return s_failures == 0 ? 0 : 1;
}
