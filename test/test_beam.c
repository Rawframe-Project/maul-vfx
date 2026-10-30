// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Beams: points along a line under a moved instance, aimed by
// parameters, along quadratic and cubic Bezier curves; noise across the
// beam faded at its ends, still or animated; strikes at the tick the last
// one dies, the same in one step or many, short strikes skipped within a
// step; a stop; a local beam in the world; distances along it; every
// check of the def.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
} Scene;

// A point: position, along, serial.
typedef struct Point
{
    float position[3];
    float along;
    uint32_t serial;
} Point;

static const mvfxLayoutField s_fields[3] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 12},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 16},
};

static const float s_parameters[8] = {0, 0, 0, 0, 4, 0, 0, 0};

static mvfxEffectDef EffectOf(const mvfxEmitterDef* emitter)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    def.parameters = s_parameters;
    def.parameterCount = 2;
    return def;
}

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Scene scene = {0};
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = EffectOf(emitter);
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 3;
    layoutDef.stride = (uint32_t)sizeof(Point);
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    return scene;
}

static mvfxEmitterDef Beam(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.lifetime = mvfxRangeSource(9.0f, 9.0f);
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.beam = (mvfxBeamDef){
        .enable = true, .segments = 4, .start = {.point = {0, 0, 0}}, .end = {.point = {4, 0, 0}}};
    return emitter;
}

typedef struct Written
{
    Point points[8];
    int64_t count;
} Written;

static Written Write(Scene* scene)
{
    static Written written;
    static uint32_t scratch[8 * 4];
    static mvfxRibbon ribbons[2];
    memset(&written, 0, sizeof written);
    static const mvfxView view = {0};
    mvfxRibbonBuffers buffers = {.points = written.points,
                                 .pointCapacity = 8,
                                 .ribbons = ribbons,
                                 .ribbonCapacity = 2,
                                 .scratch = scratch,
                                 .scratchBytes = sizeof scratch};
    int64_t ribbonCount = 0;
    CHECK(mvfxInstance_WriteRibbons(scene->context, scene->instance, 0, scene->layout, &view,
                                    &buffers, &written.count, &ribbonCount) == mvfx_success,
          "written");
    return written;
}

static void Step(Scene* scene, int64_t ticks)
{
    CHECK(mvfxContext_StepTicks(scene->context, ticks) == mvfx_success, "step");
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f;
}

static void TestLine(void)
{
    // From (0, 0, 0) to (4, 0, 0) under an instance at (10, 0, 0): five
    // points a unit apart.
    mvfxEmitterDef emitter = Beam();
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {10, 0, 0};
    static const float identity[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, identity, 1.0f, true) ==
              mvfx_success,
          "placed");
    Step(&scene, 1);
    Written w = Write(&scene);
    bool line = w.count == 5;
    for (int32_t i = 0; line && i < 5; ++i)
    {
        line = Near(w.points[i].position[0], 10.0f + (float)i) && w.points[i].position[1] == 0.0f &&
               w.points[i].serial == (uint32_t)i;
    }
    CHECK(line, "five points on the line");
    mvfxDestroyContext(scene.context);
}

static void TestAimed(void)
{
    // Start and end from parameters 0 and 1; the host moves the end.
    mvfxEmitterDef emitter = Beam();
    emitter.beam.start = (mvfxBeamPoint){.fromParameter = true, .parameter = 0};
    emitter.beam.end = (mvfxBeamPoint){.fromParameter = true, .parameter = 1};
    Scene scene = NewScene(&emitter);
    Step(&scene, 1);
    Written w = Write(&scene);
    CHECK(w.count == 5 && Near(w.points[4].position[0], 4.0f), "aimed by the defaults");
    static const float end[4] = {0, 8, 0, 0};
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 1, end) == mvfx_success, "aim");
    Step(&scene, 1);
    w = Write(&scene);
    CHECK(Near(w.points[4].position[1], 8.0f) && Near(w.points[2].position[1], 4.0f) &&
              w.points[4].serial == 4,
          "aimed again, the same points");
    mvfxDestroyContext(scene.context);
}

static void TestCurves(void)
{
    // Halfway: a quadratic through (2, 2, 0) at (2, 1, 0); a cubic through
    // (1, 2, 0) and (3, 2, 0) at (2, 1.5, 0).
    mvfxEmitterDef emitter = Beam();
    emitter.beam.controlCount = 1;
    emitter.beam.controls[0] = (mvfxBeamPoint){.point = {2, 2, 0}};
    Scene scene = NewScene(&emitter);
    Step(&scene, 1);
    Written w = Write(&scene);
    CHECK(Near(w.points[2].position[0], 2.0f) && Near(w.points[2].position[1], 1.0f) &&
              Near(w.points[1].position[1], 0.75f),
          "a quadratic curve");
    mvfxDestroyContext(scene.context);
    emitter.beam.controlCount = 2;
    emitter.beam.controls[0] = (mvfxBeamPoint){.point = {1, 2, 0}};
    emitter.beam.controls[1] = (mvfxBeamPoint){.point = {3, 2, 0}};
    scene = NewScene(&emitter);
    Step(&scene, 1);
    w = Write(&scene);
    CHECK(Near(w.points[2].position[0], 2.0f) && Near(w.points[2].position[1], 1.5f) &&
              Near(w.points[4].position[0], 4.0f),
          "a cubic curve");
    mvfxDestroyContext(scene.context);
}

static void TestNoise(void)
{
    // Across the beam only, none at the ends; still without speed, moving
    // with it.
    mvfxEmitterDef emitter = Beam();
    emitter.beam.noiseAmplitude = 1.0f;
    emitter.beam.noiseFrequency = 3.0f;
    Scene scene = NewScene(&emitter);
    Step(&scene, 1);
    Written first = Write(&scene);
    float off = fabsf(first.points[2].position[1]) + fabsf(first.points[2].position[2]);
    CHECK(first.points[2].position[0] == 2.0f && off > 0.0f && off <= 2.0f &&
              first.points[0].position[1] == 0.0f && fabsf(first.points[4].position[1]) <= 1e-6f,
          "across the beam, faded at its ends");
    Step(&scene, MVFX_TICKS_PER_SECOND / 10);
    Written still = Write(&scene);
    CHECK(memcmp(&first.points, &still.points, sizeof first.points) == 0, "still without speed");
    mvfxDestroyContext(scene.context);
    emitter.beam.noiseSpeed = 4.0f;
    scene = NewScene(&emitter);
    Step(&scene, 1);
    first = Write(&scene);
    Step(&scene, MVFX_TICKS_PER_SECOND / 10);
    Written moved = Write(&scene);
    CHECK(moved.points[2].position[1] != first.points[2].position[1], "moving with speed");
    mvfxDestroyContext(scene.context);
}

static void TestStrikes(void)
{
    // Strikes of a quarter second: after 0.3 seconds the second, serials 5
    // to 9 with new noise; the same stepped at once or in tenths.
    mvfxEmitterDef emitter = Beam();
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    emitter.beam.noiseAmplitude = 1.0f;
    emitter.beam.noiseFrequency = 3.0f;
    Scene once = NewScene(&emitter);
    Scene tenths = NewScene(&emitter);
    Step(&once, 1);
    Step(&tenths, 1);
    Written early = Write(&once);
    Step(&once, MVFX_TICKS_PER_SECOND * 3 / 10);
    for (int32_t s = 0; s < 3; ++s)
    {
        Step(&tenths, MVFX_TICKS_PER_SECOND / 10);
    }
    Written a = Write(&once);
    Written b = Write(&tenths);
    CHECK(a.count == 5 && a.points[0].serial == 5 && a.points[4].serial == 9 &&
              a.points[2].position[1] != early.points[2].position[1],
          "struck again, new noise");
    CHECK(memcmp(&a, &b, sizeof a) == 0, "at once as in tenths");
    mvfxDestroyContext(once.context);
    mvfxDestroyContext(tenths.context);
    // Strikes of a hundredth in a tenth: nine skipped within the step, the
    // tenth (serials 45 to 49) alive at its end.
    emitter.lifetime = mvfxRangeSource(0.01f, 0.01f);
    once = NewScene(&emitter);
    tenths = NewScene(&emitter);
    Step(&once, MVFX_TICKS_PER_SECOND / 10);
    for (int32_t s = 0; s < 10; ++s)
    {
        Step(&tenths, MVFX_TICKS_PER_SECOND / 100);
    }
    a = Write(&once);
    b = Write(&tenths);
    CHECK(a.count == 5 && a.points[0].serial == 45 && memcmp(&a, &b, sizeof a) == 0,
          "short strikes, at once as in steps");
    mvfxDestroyContext(once.context);
    mvfxDestroyContext(tenths.context);
}

static void TestBounded(void)
{
    // Strikes of about a tick in a second's step: 64 worked through, the
    // next struck at the step's end, serials 320 on.
    mvfxEmitterDef emitter = Beam();
    emitter.lifetime = mvfxRangeSource(1e-6f, 1e-6f);
    Scene scene = NewScene(&emitter);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    Written w = Write(&scene);
    CHECK(w.count == 5 && w.points[0].serial == 320, "a step works through 64 strikes");
    mvfxDestroyContext(scene.context);
    // A strike dying at the step's end: the next struck there, serials 5
    // on.
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    scene = NewScene(&emitter);
    Step(&scene, MVFX_TICKS_PER_SECOND / 4);
    w = Write(&scene);
    CHECK(w.count == 5 && w.points[0].serial == 5, "struck at the step's end");
    mvfxDestroyContext(scene.context);
}

static void TestAtRest(void)
{
    // Born with a speed, a beam's points are at rest.
    mvfxEmitterDef emitter = Beam();
    emitter.velocity.speed = (mvfxRange){5.0f, 5.0f};
    Scene scene = NewScene(&emitter);
    static const mvfxLayoutField velocity = {
        .field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &velocity;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &layout) == mvfx_success, "layout");
    Step(&scene, 1);
    float records[5][3];
    memset(records, 0xEE, sizeof records);
    static const mvfxView view = {0};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      records, 5, &count) == mvfx_success &&
              count == 5 && records[2][2] == 0.0f && records[4][0] == 0.0f,
          "at rest");
    mvfxDestroyContext(scene.context);
}

static void TestStop(void)
{
    // Stopped, a beam's strike lives out its life and is not struck again.
    mvfxEmitterDef emitter = Beam();
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    Scene scene = NewScene(&emitter);
    Step(&scene, 1);
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopEmitting) == mvfx_success,
          "stopped");
    Step(&scene, MVFX_TICKS_PER_SECOND / 10);
    CHECK(Write(&scene).count == 5, "living out its life");
    Step(&scene, MVFX_TICKS_PER_SECOND / 5);
    CHECK(Write(&scene).count == 0, "not struck again");
    mvfxDestroyContext(scene.context);
}

static void TestLocal(void)
{
    // A local beam under an instance at (10, 0, 0) turned a quarter about
    // Z: from (10, 0, 0) to (10, 4, 0); tiled by distance, a unit a point.
    mvfxEmitterDef emitter = Beam();
    emitter.space = mvfx_spaceLocal;
    emitter.ribbon.texture = mvfx_ribbonTileByDistance;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {10, 0, 0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 1.0f, true) ==
              mvfx_success,
          "turned");
    Step(&scene, 1);
    Written w = Write(&scene);
    CHECK(w.count == 5 && Near(w.points[4].position[0], 10.0f) &&
              Near(w.points[4].position[1], 4.0f) && Near(w.points[3].along, 3.0f) &&
              w.points[0].along == 0.0f,
          "in the world, distances along it");
    mvfxDestroyContext(scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const mvfxBurst burst = {
        .time = 0.0f, .minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1.0f};
    static const mvfxForce force = {
        .kind = mvfx_forceConstant, .vector = {0, 0, -1}, .strength = 1.0f};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 21; ++rule)
    {
        mvfxEmitterDef emitter = Beam();
        mvfxBeamDef* b = &emitter.beam;
        switch (rule)
        {
        case 0:
            b->segments = 0;
            break;
        case 20:
            // A count whose successor overflows, refused before it is
            // taken.
            b->segments = INT32_MAX;
            break;
        case 1:
            // With room for them: past the most segments alone.
            b->segments = MVFX_MAX_BEAM_SEGMENTS + 1;
            emitter.capacity = MVFX_MAX_BEAM_SEGMENTS + 2;
            break;
        case 2:
            b->segments = 8;
            break;
        case 3:
            b->controlCount = 3;
            break;
        case 4:
            b->start.point[1] = (float)NAN;
            break;
        case 5:
            b->end = (mvfxBeamPoint){.fromParameter = true, .parameter = 2};
            break;
        case 6:
            b->controlCount = 1;
            b->controls[0].point[2] = 2e6f;
            break;
        case 7:
            b->noiseAmplitude = -1.0f;
            break;
        case 8:
            b->noiseFrequency = 1001.0f;
            break;
        case 9:
            b->noiseSpeed = (float)NAN;
            break;
        case 10:
            emitter.rate = 1.0f;
            break;
        case 11:
            emitter.bursts = &burst;
            emitter.burstCount = 1;
            break;
        case 12:
            emitter.ratePerDistance = 1.0f;
            break;
        case 13:
            emitter.eventSpawn = (mvfxEventSpawn){.enable = true, .source = 0, .capacity = 1};
            break;
        case 14:
            emitter.hostSpawnCapacity = 1;
            break;
        case 15:
            emitter.collision.enable = true;
            break;
        case 16:
            emitter.forces = &force;
            emitter.forceCount = 1;
            break;
        case 17:
            emitter.ribbon.ids = mvfx_ribbonPerBatch;
            break;
        case 18:
            emitter.ribbon.pinHead = true;
            break;
        default:
            emitter.events = mvfx_eventMaskBirth;
            emitter.eventCapacity = 4;
            break;
        }
        mvfxEffectDef def = EffectOf(&emitter);
        mvfxEffectId effect = {0};
        // Past MVFX_MAX_BEAM_SEGMENTS or MVFX_MAX_EXTENT is past a named
        // bound.
        mvfxResult want =
            rule == 1 || rule == 6 || rule == 20 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect) == want;
    }
    CHECK(refused == 21, "every check");
    mvfxEmitterDef emitter = Beam();
    emitter.capacity = MVFX_MAX_BEAM_SEGMENTS + 1;
    emitter.beam.segments = MVFX_MAX_BEAM_SEGMENTS;
    emitter.beam.controlCount = 2;
    emitter.beam.end = (mvfxBeamPoint){.fromParameter = true, .parameter = 1};
    emitter.beam.noiseAmplitude = 1e6f;
    emitter.beam.noiseFrequency = 1000.0f;
    emitter.beam.noiseSpeed = -1000.0f;
    emitter.events = mvfx_eventMaskDeath;
    emitter.eventCapacity = 4;
    mvfxEffectDef def = EffectOf(&emitter);
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the largest");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestLine();
    TestAimed();
    TestCurves();
    TestNoise();
    TestStrikes();
    TestBounded();
    TestAtRest();
    TestStop();
    TestLocal();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
