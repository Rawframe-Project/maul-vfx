// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Trails: a trail spawned by distance with its head pinned to the moving
// source, the head in the bounds and in a tile by distance, gone once the
// emitter stops; breaks by the host (one emitter or all), by a teleport
// and by a gap past the break distance; a restart's counter; the def's
// checks and the call's refusals.

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

static Scene NewScene(const mvfxEmitterDef* emitters, int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Scene scene = {0};
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = count;
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

static mvfxEmitterDef Trail(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 32;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(9.0f, 9.0f);
    emitter.ribbon.ids = mvfx_ribbonOne;
    return emitter;
}

typedef struct Written
{
    Point points[40];
    mvfxRibbon ribbons[4];
    int64_t pointCount;
    int64_t ribbonCount;
} Written;

static Written Write(Scene* scene, int32_t emitter)
{
    static Written written;
    static uint32_t scratch[32 * 4];
    memset(&written, 0, sizeof written);
    static const mvfxView view = {.eye = {0, 0, 10}};
    mvfxRibbonBuffers buffers = {.points = written.points,
                                 .pointCapacity = 40,
                                 .ribbons = written.ribbons,
                                 .ribbonCapacity = 4,
                                 .scratch = scratch,
                                 .scratchBytes = sizeof scratch};
    CHECK(mvfxInstance_WriteRibbons(scene->context, scene->instance, emitter, scene->layout, &view,
                                    &buffers, &written.pointCount,
                                    &written.ribbonCount) == mvfx_success,
          "written");
    return written;
}

static void MoveTo(Scene* scene, double x, bool teleport)
{
    const double origin[3] = {x, 0, 0};
    static const float identity[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(scene->context, scene->instance, origin, identity, 1.0f,
                                    teleport) == mvfx_success,
          "moved");
}

static void Step(Scene* scene)
{
    CHECK(mvfxContext_StepTicks(scene->context, MVFX_TICKS_PER_SECOND / 10) == mvfx_success,
          "step");
}

static void Spawn(Scene* scene, double x)
{
    const double place[3] = {x, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "spawned");
    Step(scene);
}

static void TestPinned(void)
{
    // Two a unit of travel, moved 1.3 a step: points every half unit, the
    // head at the source's place; tiled by distance, the head's coordinate
    // its gap past the youngest.
    mvfxEmitterDef emitter = Trail();
    emitter.ratePerDistance = 2.0f;
    emitter.ribbon.pinHead = true;
    emitter.ribbon.texture = mvfx_ribbonTileByDistance;
    // Points so small only the head reaches the source in the bounds.
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                      .min = {0.01f, 0.01f, 0.01f, 0},
                                      .max = {0.01f, 0.01f, 0.01f, 0},
                                      .uniform = true};
    Scene scene = NewScene(&emitter, 1);
    for (int32_t s = 1; s <= 3; ++s)
    {
        MoveTo(&scene, 1.3 * s, false);
        Step(&scene);
    }
    Written w = Write(&scene, 0);
    int64_t last = w.pointCount - 1;
    CHECK(w.ribbonCount == 1 && w.pointCount >= 8 &&
              fabsf(w.points[last].position[0] - 3.9f) <= 1e-5f &&
              w.points[last].serial == w.points[last - 1].serial,
          "the head at the source, the youngest's values");
    float gap = w.points[last].position[0] - w.points[last - 1].position[0];
    CHECK(gap > 0.0f && gap <= 0.5f + 1e-5f &&
              fabsf(w.points[last].along - (w.points[last - 1].along + gap)) <= 1e-4f,
          "within a spacing of the youngest; its distance past it");
    mvfxBounds bounds = {0};
    CHECK(mvfxInstance_GetBounds(scene.context, scene.instance, 0, &bounds) == mvfx_success &&
              bounds.max[0] >= 3.9 && bounds.max[0] < 3.90001,
          "the head in the bounds");
    // Stopped, it may not spawn: the trail lets go of its source.
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopEmitting) == mvfx_success,
          "stopped");
    Step(&scene);
    Written after = Write(&scene, 0);
    CHECK(after.pointCount == w.pointCount - 1 &&
              after.points[after.pointCount - 1].position[0] < 3.9f,
          "no head once stopped");
    mvfxDestroyContext(scene.context);
}

static void TestBreaks(void)
{
    // The host breaks one emitter's ribbon and then all; the other emitter
    // is not drawn as ribbons and is left alone.
    mvfxEmitterDef emitters[2] = {Trail(), Trail()};
    emitters[1].ribbon.ids = mvfx_ribbonNone;
    Scene scene = NewScene(emitters, 2);
    Spawn(&scene, 0);
    Spawn(&scene, 1);
    CHECK(mvfxInstance_BreakRibbons(scene.context, scene.instance, 0) == mvfx_success, "broken");
    Spawn(&scene, 5);
    Spawn(&scene, 6);
    CHECK(mvfxInstance_BreakRibbons(scene.context, scene.instance, -1) == mvfx_success,
          "all broken");
    Spawn(&scene, 9);
    Spawn(&scene, 10);
    Written w = Write(&scene, 0);
    CHECK(w.ribbonCount == 3 && w.ribbons[0].id == 0 && w.ribbons[1].id == 1 &&
              w.ribbons[2].id == 2 && w.points[2].position[0] == 5.0f && w.pointCount == 6,
          "a ribbon after each break");
    // A teleport breaks too; a move does not.
    MoveTo(&scene, 20, false);
    Spawn(&scene, 11);
    MoveTo(&scene, 40, true);
    Spawn(&scene, 12);
    Spawn(&scene, 13);
    w = Write(&scene, 0);
    CHECK(w.ribbonCount == 4 && w.ribbons[2].count == 3 && w.ribbons[3].id == 3 &&
              w.ribbons[3].count == 2,
          "a teleport breaks, a move does not");
    // A restart starts the counter again.
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restarted");
    Spawn(&scene, 0);
    Spawn(&scene, 1);
    w = Write(&scene, 0);
    CHECK(w.ribbonCount == 1 && w.ribbons[0].id == 0, "the counter from 0");
    mvfxDestroyContext(scene.context);
}

static void TestBreakDistance(void)
{
    // Gaps of 1, 4 and 1 with a break distance of 2: ribbons {0, 1} and
    // {5, 6}.
    mvfxEmitterDef emitter = Trail();
    emitter.ribbon.breakDistance = 2.0f;
    Scene scene = NewScene(&emitter, 1);
    Spawn(&scene, 0);
    Spawn(&scene, 1);
    Spawn(&scene, 5);
    Spawn(&scene, 6);
    Written w = Write(&scene, 0);
    CHECK(w.ribbonCount == 2 && w.ribbons[0].count == 2 && w.ribbons[1].count == 2 &&
              w.ribbons[1].id == w.ribbons[0].id + 1 && w.points[2].position[0] == 5.0f,
          "a gap past the break distance");
    mvfxDestroyContext(scene.context);
    // The first particle is a ribbon's youngest point too.
    scene = NewScene(&emitter, 1);
    Spawn(&scene, 0);
    Spawn(&scene, 5);
    Spawn(&scene, 6);
    w = Write(&scene, 0);
    CHECK(w.ribbonCount == 1 && w.ribbons[0].count == 2 && w.points[0].position[0] == 5.0f,
          "a gap from the first particle");
    mvfxDestroyContext(scene.context);
}

static void TestPerSourceCounter(void)
{
    // A ribbon per source takes the counter for births not of an event,
    // its ids with the top bit set (apart from the sources' serials).
    mvfxEmitterDef emitter = Trail();
    emitter.ribbon.ids = mvfx_ribbonPerSource;
    Scene scene = NewScene(&emitter, 1);
    Spawn(&scene, 0);
    Spawn(&scene, 1);
    CHECK(mvfxInstance_BreakRibbons(scene.context, scene.instance, 0) == mvfx_success, "broken");
    Spawn(&scene, 2);
    Spawn(&scene, 3);
    Written w = Write(&scene, 0);
    CHECK(w.ribbonCount == 2 && w.ribbons[0].id == 0x80000000u && w.ribbons[1].id == 0x80000001u,
          "by the counter");
    mvfxDestroyContext(scene.context);
}

static void TestHeadPlace(void)
{
    // A shape at (1, 0, 0) under an instance at (10, 0, 0) turned a
    // quarter about Z at scale 2: the head at (10, 2, 0). With no
    // particles, no head in the bounds.
    mvfxEmitterDef emitter = Trail();
    emitter.rate = 1.0f;
    emitter.shape.offset[0] = 1.0f;
    emitter.ribbon.pinHead = true;
    Scene scene = NewScene(&emitter, 1);
    const double origin[3] = {10, 0, 0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Step(&scene);
    mvfxBounds bounds = {0};
    CHECK(mvfxInstance_GetBounds(scene.context, scene.instance, 0, &bounds) == mvfx_success &&
              bounds.empty && bounds.max[1] == 0.0 && bounds.min[0] == 0.0,
          "no particles, no head in the bounds");
    const double place[3] = {10, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success, "one");
    Step(&scene);
    Written w = Write(&scene, 0);
    CHECK(w.ribbonCount == 1 && w.pointCount == 2 &&
              fabsf(w.points[1].position[0] - 10.0f) <= 1e-5f &&
              fabsf(w.points[1].position[1] - 2.0f) <= 1e-5f,
          "the head at the shape's origin, turned and scaled");
    mvfxDestroyContext(scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Trail();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 5; ++rule)
    {
        emitter = Trail();
        switch (rule)
        {
        case 0:
            emitter.ribbon.breakDistance = -1.0f;
            break;
        case 1:
            emitter.ribbon.breakDistance = (float)NAN;
            break;
        case 2:
            emitter.ribbon.breakDistance = 2e6f;
            break;
        case 3:
            emitter.ribbon.ids = mvfx_ribbonPerBatch;
            emitter.ribbon.pinHead = true;
            break;
        default:
            emitter.ribbon.ids = mvfx_ribbonPerSource;
            emitter.ribbon.pinHead = true;
            break;
        }
        refused += mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid;
    }
    CHECK(refused == 5, "every check");
    emitter = Trail();
    emitter.ribbon.breakDistance = 1e6f;
    emitter.ribbon.pinHead = true;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the largest");
    mvfxDestroyContext(context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Trail();
    Scene scene = NewScene(&emitter, 1);
    uint64_t misuse = mvfxGetContextMisuse(scene.context);
    CHECK(mvfxInstance_BreakRibbons(scene.context, scene.instance, 1) == mvfx_errorInvalid &&
              mvfxInstance_BreakRibbons(scene.context, scene.instance, -2) == mvfx_errorInvalid &&
              mvfxGetContextMisuse(scene.context) == misuse + 2,
          "emitters out of range, counted");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_BreakRibbons(scene.context, stale, 0) == mvfx_errorStale &&
              mvfxInstance_BreakRibbons(NULL, scene.instance, 0) == mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(scene.context);
}

int main(void)
{
    TestPinned();
    TestBreaks();
    TestBreakDistance();
    TestPerSourceCounter();
    TestHeadPlace();
    TestChecks();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
