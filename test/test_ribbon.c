// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbons: one ribbon's points with their tangents, normalized lengths,
// widths and colours over length; a ribbon per batch, a lone particle
// drawn as none; a ribbon per event's particle; a local emitter's ribbon
// in the world; buffers too small, counts alone, an emitter not drawn as
// ribbons; the particle record's ribbon fields; the scratch; the def's
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

// A point: position, tangent, width, length, along, ribbon, serial,
// colour.
typedef struct Point
{
    float position[3];
    float tangent[3];
    float width;
    float length;
    float along;
    uint32_t ribbon;
    uint32_t serial;
    float color[4];
} Point;

static const mvfxLayoutField s_fields[8] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 24},
    {.field = mvfx_recordLength, .format = mvfx_formatFloat32, .components = 1, .offset = 28},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 32},
    {.field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 1, .offset = 36},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 40},
    {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 44},
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
    layoutDef.fieldCount = 8;
    layoutDef.stride = (uint32_t)sizeof(Point);
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    return scene;
}

static mvfxEmitterDef Ribbon(mvfxRibbonIds ids)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 16;
    emitter.hostSpawnCapacity = 16;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 1, 1, 0}, .max = {0.5f, 1, 1, 0}};
    emitter.ribbon.ids = ids;
    return emitter;
}

static void Spawn(Scene* scene, int32_t emitter, const double* points, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = points};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, emitter, &request) == mvfx_success,
          "spawned");
    CHECK(mvfxContext_StepTicks(scene->context, 1) == mvfx_success, "step");
}

typedef struct Written
{
    Point points[16];
    mvfxRibbon ribbons[4];
    int64_t pointCount;
    int64_t ribbonCount;
    mvfxResult result;
} Written;

static Written Write(Scene* scene, int32_t emitter, int64_t points, int64_t ribbons)
{
    static Written written;
    static uint32_t scratch[16 * 4];
    memset(&written, 0xEE, sizeof written);
    static const mvfxView view = {0};
    mvfxRibbonBuffers buffers = {.points = written.points,
                                 .pointCapacity = points,
                                 .ribbons = written.ribbons,
                                 .ribbonCapacity = ribbons,
                                 .scratch = scratch,
                                 .scratchBytes = sizeof scratch};
    written.result =
        mvfxInstance_WriteRibbons(scene->context, scene->instance, emitter, scene->layout, &view,
                                  &buffers, &written.pointCount, &written.ribbonCount);
    return written;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f;
}

static void TestOne(void)
{
    // x of 0, 3 and 7: lengths 0, 3/7, 1. Width 0.5 times 1 + 2t; colour
    // red to blue over length.
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonOne);
    static const mvfxCurveKey keys[2] = {
        {.time = 0.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 3.0f, .interpolation = mvfx_interpolationLinear}};
    emitter.ribbon.width = (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = keys, .keyCount = 2}};
    static const mvfxColorKey colors[2] = {{0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}};
    static const mvfxAlphaKey alphas[1] = {{0.0f, 1.0f}};
    emitter.ribbon.color = (mvfxGradient){
        .colorKeys = colors, .colorKeyCount = 2, .alphaKeys = alphas, .alphaKeyCount = 1};
    Scene scene = NewScene(&emitter, 1);
    static const double points[9] = {0, 0, 0, 3, 0, 0, 7, 0, 0};
    Spawn(&scene, 0, points, 3);
    Written w = Write(&scene, 0, 16, 4);
    CHECK(w.result == mvfx_success && w.pointCount == 3 && w.ribbonCount == 1 &&
              w.ribbons[0].first == 0 && w.ribbons[0].count == 3 && w.ribbons[0].id == 0,
          "one ribbon of three");
    const Point* p = w.points;
    CHECK(p[0].position[0] == 0.0f && p[1].position[0] == 3.0f && p[2].position[0] == 7.0f &&
              p[0].serial == 0 && p[2].serial == 2 && p[1].ribbon == 0,
          "its points oldest first");
    CHECK(Near(p[0].length, 0.0f) && Near(p[1].length, 3.0f / 7.0f) && Near(p[2].length, 1.0f) &&
              p[1].along == p[1].length,
          "normalized lengths; along them");
    CHECK(p[0].tangent[0] == 1.0f && p[1].tangent[0] == 1.0f && p[2].tangent[0] == 1.0f &&
              p[1].tangent[1] == 0.0f,
          "tangents toward the younger end");
    CHECK(Near(p[0].width, 0.5f) && Near(p[1].width, 0.5f * (1.0f + 6.0f / 7.0f)) &&
              Near(p[2].width, 1.5f),
          "width over length");
    CHECK(Near(p[0].color[0], 1.0f) && Near(p[2].color[2], 1.0f) && Near(p[2].color[0], 0.0f) &&
              Near(p[1].color[0], 4.0f / 7.0f),
          "colour over length");
    mvfxDestroyContext(scene.context);
}

static void TestPerBatch(void)
{
    // Three, then one, then two: ribbons of serials 0 to 2 and 4 to 5; the
    // lone particle is no ribbon.
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonPerBatch);
    Scene scene = NewScene(&emitter, 1);
    static const double points[9] = {0, 0, 0, 0, 1, 0, 0, 2, 0};
    Spawn(&scene, 0, points, 3);
    Spawn(&scene, 0, points, 1);
    Spawn(&scene, 0, points, 2);
    Written w = Write(&scene, 0, 16, 4);
    CHECK(w.result == mvfx_success && w.pointCount == 5 && w.ribbonCount == 2 &&
              w.ribbons[0].id == 0 && w.ribbons[0].count == 3 && w.ribbons[1].id == 4 &&
              w.ribbons[1].first == 3 && w.ribbons[1].count == 2 && w.points[3].serial == 4 &&
              w.points[4].ribbon == 1 && w.points[3].tangent[1] == 1.0f,
          "a ribbon per batch");
    // Too few points for the second: the first alone; too few ribbons, the
    // same; none, the counts.
    w = Write(&scene, 0, 4, 4);
    CHECK(w.pointCount == 5 && w.ribbonCount == 2 && w.ribbons[0].count == 3 &&
              w.ribbons[1].count == (int32_t)0xEEEEEEEE && w.points[3].serial == 0xEEEEEEEEu,
          "whole ribbons that fit");
    w = Write(&scene, 0, 16, 1);
    CHECK(w.pointCount == 5 && w.ribbonCount == 2 && w.points[3].serial == 0xEEEEEEEEu,
          "the ribbons' room");
    static uint32_t scratch[16 * 4];
    static const mvfxView view = {0};
    mvfxRibbonBuffers counts = {.scratch = scratch, .scratchBytes = sizeof scratch};
    int64_t pointCount = 0;
    int64_t ribbonCount = 0;
    CHECK(mvfxInstance_WriteRibbons(scene.context, scene.instance, 0, scene.layout, &view, &counts,
                                    &pointCount, &ribbonCount) == mvfx_success &&
              pointCount == 5 && ribbonCount == 2,
          "the counts alone");
    mvfxDestroyContext(scene.context);
}

static void TestStopsAtTheFirstMiss(void)
{
    // Ribbons of 3, 3 and 2 points in room for 5: the first alone, though
    // the third would fit after it.
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonPerBatch);
    Scene scene = NewScene(&emitter, 1);
    static const double points[9] = {0, 0, 0, 0, 1, 0, 0, 2, 0};
    Spawn(&scene, 0, points, 3);
    Spawn(&scene, 0, points, 3);
    Spawn(&scene, 0, points, 2);
    Written w = Write(&scene, 0, 5, 4);
    CHECK(w.pointCount == 8 && w.ribbonCount == 3 && w.ribbons[0].count == 3 &&
              w.ribbons[1].count == (int32_t)0xEEEEEEEE && w.points[3].serial == 0xEEEEEEEEu,
          "no ribbon after one that did not fit");
    mvfxDestroyContext(scene.context);
}

static void TestPerSource(void)
{
    // Each of two particles' births spawns three: ribbons of the sources'
    // serials, their points at one place, so lengths go by points.
    mvfxEmitterDef emitters[2] = {Ribbon(mvfx_ribbonNone), Ribbon(mvfx_ribbonPerSource)};
    emitters[0].events = mvfx_eventMaskBirth;
    emitters[0].eventCapacity = 4;
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventBirth,
                                              .minCount = 3,
                                              .maxCount = 3,
                                              .capacity = 16};
    Scene scene = NewScene(emitters, 2);
    static const double points[6] = {0, 0, 0, 5, 0, 0};
    Spawn(&scene, 0, points, 2);
    Written w = Write(&scene, 1, 16, 4);
    CHECK(w.result == mvfx_success && w.pointCount == 6 && w.ribbonCount == 2 &&
              w.ribbons[0].id == 0 && w.ribbons[1].id == 1 && w.ribbons[1].count == 3 &&
              w.points[3].position[0] == 5.0f && w.points[4].length == 0.5f &&
              w.points[4].tangent[0] == 0.0f,
          "a ribbon per source");
    w = Write(&scene, 0, 16, 4);
    CHECK(w.result == mvfx_success && w.pointCount == 0 && w.ribbonCount == 0,
          "an emitter not drawn as ribbons");
    static const mvfxView view = {0};
    mvfxRibbonBuffers none = {0};
    int64_t pointCount = -1;
    int64_t ribbonCount = -1;
    CHECK(mvfxInstance_WriteRibbons(scene.context, scene.instance, 0, scene.layout, &view, &none,
                                    &pointCount, &ribbonCount) == mvfx_success &&
              pointCount == 0 && ribbonCount == 0,
          "it needs no scratch");
    mvfxDestroyContext(scene.context);
}

static void TestLocal(void)
{
    // Turned a quarter about Z at scale 2: local x of 0 and 1 are the
    // world's y of 0 and 2, the tangent along y, the width doubled.
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonOne);
    emitter.space = mvfx_spaceLocal;
    Scene scene = NewScene(&emitter, 1);
    static const double origin[3] = {0, 0, 0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "turned");
    static const double points[6] = {0, 0, 0, 1, 0, 0};
    Spawn(&scene, 0, points, 2);
    Written w = Write(&scene, 0, 16, 4);
    CHECK(w.result == mvfx_success && w.pointCount == 2 && Near(w.points[1].position[1], 2.0f) &&
              Near(w.points[0].tangent[1], 1.0f) && Near(w.points[0].tangent[0], 0.0f) &&
              Near(w.points[0].width, 1.0f),
          "in the world");
    mvfxDestroyContext(scene.context);
}

static void TestParticleRecord(void)
{
    // A particle's record: tangent, length, along and ribbon 0, its
    // width its size's X.
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonOne);
    Scene scene = NewScene(&emitter, 1);
    static const double points[3] = {1, 2, 3};
    Spawn(&scene, 0, points, 1);
    Point record;
    memset(&record, 0xEE, sizeof record);
    static const mvfxView view = {0};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, scene.layout, &view, NULL,
                                      0, &record, 1, &count) == mvfx_success &&
              count == 1 && record.tangent[0] == 0.0f && record.tangent[2] == 0.0f &&
              record.width == 0.5f && record.length == 0.0f && record.along == 0.0f &&
              record.ribbon == 0 && record.color[0] == 1.0f,
          "ribbon fields in a particle's record");
    int64_t bytes = 0;
    CHECK(mvfxInstance_GetSortScratchBytes(scene.context, scene.instance, 0, &bytes) ==
                  mvfx_success &&
              bytes == 16 * 16,
          "scratch for its capacity");
    mvfxDestroyContext(scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    // The ribbon fields' components: 3 for a tangent, 1 for the rest.
    const mvfxLayoutField wide[5] = {
        {.field = mvfx_recordTangent, .format = mvfx_formatFloat32, .components = 4},
        {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 2},
        {.field = mvfx_recordLength, .format = mvfx_formatFloat32, .components = 2},
        {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 2},
        {.field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 2}};
    int32_t refusedFields = 0;
    for (int32_t f = 0; f < 5; ++f)
    {
        mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
        layoutDef.fields = &wide[f];
        layoutDef.fieldCount = 1;
        layoutDef.stride = 16;
        mvfxLayoutId refused = {0};
        refusedFields += mvfxCreateLayout(context, &layoutDef, &refused) == mvfx_errorInvalid;
    }
    CHECK(refusedFields == 5, "ribbon fields' components checked");
    mvfxEmitterDef emitter = Ribbon(4);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "ids past the last");
    emitter = Ribbon(mvfx_ribbonPerSource);
    emitter.ribbon.width.value = (float)NAN;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "a width not finite");
    emitter = Ribbon(mvfx_ribbonPerSource);
    emitter.ribbon.color.colorKeyCount = 1;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "a gradient without keys");
    emitter = Ribbon(mvfx_ribbonPerSource);
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the last ids");
    mvfxDestroyContext(context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonOne);
    Scene scene = NewScene(&emitter, 1);
    static const double points[6] = {0, 0, 0, 1, 0, 0};
    Spawn(&scene, 0, points, 2);
    mvfxContext* c = scene.context;
    mvfxInstanceId i = scene.instance;
    mvfxLayoutId l = scene.layout;
    static uint32_t scratch[16 * 4];
    static Point out[4];
    static mvfxRibbon list[2];
    static const mvfxView view = {0};
    mvfxView far = {.origin = {0x1p53, 0, 0}};
    int64_t points1 = 0;
    int64_t ribbons1 = 0;
    mvfxRibbonBuffers small = {.points = out,
                               .pointCapacity = 4,
                               .ribbons = list,
                               .ribbonCapacity = 2,
                               .scratch = scratch,
                               .scratchBytes = 31};
    CHECK(mvfxInstance_WriteRibbons(c, i, 0, l, &view, &small, &points1, &ribbons1) ==
                  mvfx_errorCapacity &&
              points1 == 0,
          "scratch too small for two");
    mvfxRibbonBuffers good = small;
    good.scratchBytes = 32;
    CHECK(mvfxInstance_WriteRibbons(c, i, 0, l, &view, &good, &points1, &ribbons1) ==
                  mvfx_success &&
              points1 == 2,
          "enough for two");
    mvfxRibbonBuffers bad[6] = {good, good, good, good, good, good};
    bad[0].points = NULL;
    bad[1].pointCapacity = -1;
    bad[2].ribbons = NULL;
    bad[3].ribbonCapacity = -1;
    bad[4].scratch = (unsigned char*)scratch + 2;
    bad[5].ribbons = (mvfxRibbon*)((unsigned char*)list + 4);
    uint64_t misuse = mvfxGetContextMisuse(c);
    int32_t refused = 0;
    for (int32_t b = 0; b < 6; ++b)
    {
        refused += mvfxInstance_WriteRibbons(c, i, 0, l, &view, &bad[b], &points1, &ribbons1) ==
                   mvfx_errorInvalid;
    }
    refused += mvfxInstance_WriteRibbons(c, i, 0, l, &far, &good, &points1, &ribbons1) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_WriteRibbons(c, i, 0, l, NULL, &good, &points1, &ribbons1) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_WriteRibbons(c, i, 0, l, &view, NULL, &points1, &ribbons1) ==
               mvfx_errorInvalid;
    refused +=
        mvfxInstance_WriteRibbons(c, i, 0, l, &view, &good, NULL, &ribbons1) == mvfx_errorInvalid;
    refused +=
        mvfxInstance_WriteRibbons(c, i, 0, l, &view, &good, &points1, NULL) == mvfx_errorInvalid;
    refused += mvfxInstance_WriteRibbons(c, i, 1, l, &view, &good, &points1, &ribbons1) ==
               mvfx_errorInvalid;
    CHECK(refused == 12 && mvfxGetContextMisuse(c) == misuse + 12, "refused, each counted");
    mvfxInstanceId stale = i;
    stale.generation += 1;
    mvfxLayoutId staleLayout = l;
    staleLayout.generation += 1;
    CHECK(mvfxInstance_WriteRibbons(c, stale, 0, l, &view, &good, &points1, &ribbons1) ==
                  mvfx_errorStale &&
              mvfxInstance_WriteRibbons(c, i, 0, staleLayout, &view, &good, &points1, &ribbons1) ==
                  mvfx_errorStale &&
              mvfxInstance_WriteRibbons(NULL, i, 0, l, &view, &good, &points1, &ribbons1) ==
                  mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(c);
}

int main(void)
{
    TestOne();
    TestPerBatch();
    TestStopsAtTheFirstMiss();
    TestPerSource();
    TestLocal();
    TestParticleRecord();
    TestChecks();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
