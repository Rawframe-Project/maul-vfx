// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Plexus links: against brute force over all pairs in double, each pair
// once owned by the one born first; the caps of a particle and of the
// emitter, the rest counted; the fade, the width, the coordinate along,
// the tangent; strips; buffers too small taking whole links; an emitter
// without links; a restart clearing them; the checks.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 64

typedef struct Record
{
    uint32_t serial;
    float position[3];
    float color[4];
    float width;
    float along;
    float tangent[3];
    float corner[2];
} Record;

static const mvfxLayoutField s_fields[7] = {
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 0},
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 4},
    {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 16},
    {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 32},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 36},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat32, .components = 3, .offset = 40},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 52},
};

static const mvfxView s_view = {.eye = {0, 0, 10}, .forward = {0, 0, -1}, .up = {0, 1, 0}};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
} Scene;

static mvfxEmitterDef Emitter(float distance)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxMakeRangeSource(100.0f, 100.0f);
    emitter.plexus = (mvfxPlexusDef){.enable = true,
                                     .distance = distance,
                                     .maxLinks = 4096,
                                     .maxPerParticle = MVFX_MAX_NEIGHBORS,
                                     .width = 0.25f};
    return emitter;
}

static Scene Run(const mvfxEmitterDef* emitter, const double* places, int32_t count)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 7;
    layoutDef.stride = (uint32_t)sizeof(Record);
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    static const float rest[3 * MOST] = {0};
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = rest};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success &&
              mvfxContext_StepTicks(scene.context, 1) == mvfx_success,
          "stepped");
    return scene;
}

typedef struct Links
{
    Record points[4096];
    mvfxRibbon ribbons[2048];
    int64_t pointCount;
    int64_t linkCount;
    mvfxResult result;
} Links;

static Links s_links;

static void WriteLinks(const Scene* scene, int64_t points, int64_t ribbons)
{
    memset(&s_links, 0, sizeof s_links);
    mvfxRibbonBuffers buffers = {.points = s_links.points,
                                 .pointCapacity = points,
                                 .ribbons = s_links.ribbons,
                                 .ribbonCapacity = ribbons};
    s_links.result =
        mvfxInstance_WritePlexus(scene->context, scene->instance, 0, scene->layout, &s_view,
                                 &buffers, &s_links.pointCount, &s_links.linkCount);
}

static uint64_t Dropped(const Scene* scene)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, 0, &stats) == mvfx_success,
          "stats");
    return stats.linksDropped;
}

static void TestBruteForce(void)
{
    // Forty at random in a box of 3, linked within 1.2: every pair closer,
    // once, the one born first first, in their order.
    static double places[120];
    uint32_t random = 5u;
    for (int32_t i = 0; i < 120; ++i)
    {
        random = random * 1664525u + 1013904223u;
        places[i] = 3.0 * (double)(random >> 8u) / 16777216.0;
    }
    mvfxEmitterDef emitter = Emitter(1.2f);
    Scene scene = Run(&emitter, places, 40);
    WriteLinks(&scene, 4096, 2048);
    bool ok = s_links.result == mvfx_success && s_links.pointCount == 2 * s_links.linkCount;
    int64_t expected = 0;
    int64_t at = 0;
    for (uint32_t i = 0; i < 40; ++i)
    {
        for (uint32_t j = i + 1; j < 40; ++j)
        {
            double square = 0.0;
            for (int32_t k = 0; k < 3; ++k)
            {
                double d = places[3 * j + k] - places[3 * i + k];
                square += d * d;
            }
            expected += square < 1.44 ? 1 : 0;
        }
    }
    for (int64_t l = 0; l < s_links.linkCount; ++l)
    {
        const mvfxRibbon* ribbon = &s_links.ribbons[l];
        const Record* a = &s_links.points[ribbon->first];
        const Record* b = &s_links.points[ribbon->first + 1];
        uint32_t i = a->serial;
        uint32_t j = b->serial;
        double square = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            double d = places[3 * j + k] - places[3 * i + k];
            square += d * d;
        }
        // In the owners' order; each pair once, the owner first.
        ok = ok && ribbon->count == 2 && ribbon->id == (uint32_t)l && i < j && square < 1.44 &&
             i >= at;
        at = i;
        for (int64_t m = 0; m < l; ++m)
        {
            const Record* c = &s_links.points[s_links.ribbons[m].first];
            ok = ok && !(c[0].serial == i && c[1].serial == j);
        }
    }
    CHECK(ok && s_links.linkCount == expected && expected > 20 && Dropped(&scene) == 0,
          "brute force over all pairs");
    mvfxDestroyContext(scene.context);
}

static void TestCaps(void)
{
    // Four in a clump, six pairs: a particle owning one keeps three; an
    // emitter keeping two keeps two; the rest counted.
    static const double clump[12] = {0, 0, 0, 0.1, 0, 0, 0, 0.1, 0, 0, 0, 0.1};
    mvfxEmitterDef emitter = Emitter(1.0f);
    emitter.plexus.maxPerParticle = 1;
    Scene scene = Run(&emitter, clump, 4);
    WriteLinks(&scene, 4096, 2048);
    CHECK(s_links.linkCount == 3 && Dropped(&scene) == 3, "a particle's most");
    mvfxDestroyContext(scene.context);
    emitter = Emitter(1.0f);
    emitter.plexus.maxLinks = 2;
    scene = Run(&emitter, clump, 4);
    WriteLinks(&scene, 4096, 2048);
    CHECK(s_links.linkCount == 2 && Dropped(&scene) == 4, "the emitter's most");
    mvfxDestroyContext(scene.context);
}

static void TestPoints(void)
{
    // A link of 0.5 along x within 1, fading: alpha 0.5; width a quarter
    // of size 1; along 0 and 1; the tangent from the owner.
    static const double pair[6] = {0, 0, 0, 0.5, 0, 0};
    mvfxEmitterDef emitter = Emitter(1.0f);
    emitter.plexus.fade = true;
    Scene scene = Run(&emitter, pair, 2);
    WriteLinks(&scene, 4096, 2048);
    const Record* a = &s_links.points[0];
    const Record* b = &s_links.points[1];
    CHECK(s_links.linkCount == 1 && fabsf(a->color[3] - 0.5f) <= 1e-6f && a->color[0] == 1.0f &&
              a->width == 0.25f && a->along == 0.0f && b->along == 1.0f && a->tangent[0] == 1.0f &&
              b->position[0] == 0.5f,
          "fade, width, along, tangent");
    // Strips: four vertices, six indices; and too small for one, none.
    static Record vertices[8];
    static uint32_t indices[12];
    mvfxExpandBuffers buffers = {
        .vertices = vertices, .vertexCapacity = 8, .indices = indices, .indexCapacity = 12};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandPlexus(scene.context, scene.instance, 0, scene.layout, &s_view,
                                    &buffers, &vertexCount, &indexCount) == mvfx_success &&
              vertexCount == 4 && indexCount == 6 && vertices[0].corner[1] == 0.0f &&
              vertices[1].corner[1] == 1.0f && fabsf(vertices[0].position[1] - 0.125f) <= 1e-6f &&
              indices[5] == 3,
          "strips");
    WriteLinks(&scene, 1, 2048);
    CHECK(s_links.pointCount == 2 && s_links.linkCount == 1 && s_links.ribbons[0].count == 0,
          "too small: counted, none written");
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restarted");
    WriteLinks(&scene, 4096, 2048);
    CHECK(s_links.linkCount == 0, "a restart clears them");
    mvfxDestroyContext(scene.context);
    // Not fading: alpha 1. Without links: none.
    emitter.plexus.fade = false;
    scene = Run(&emitter, pair, 2);
    WriteLinks(&scene, 4096, 2048);
    CHECK(s_links.points[0].color[3] == 1.0f, "no fade");
    mvfxDestroyContext(scene.context);
    emitter.plexus.enable = false;
    scene = Run(&emitter, pair, 2);
    WriteLinks(&scene, 4096, 2048);
    CHECK(s_links.result == mvfx_success && s_links.linkCount == 0 && s_links.pointCount == 0,
          "an emitter without links");
    mvfxDestroyContext(scene.context);
}

static void TestOwnSettings(void)
{
    // Exactly the distance apart: no link.
    static const double apart[6] = {0, 0, 0, 1, 0, 0};
    mvfxEmitterDef emitter = Emitter(1.0f);
    Scene scene = Run(&emitter, apart, 2);
    WriteLinks(&scene, 4096, 2048);
    CHECK(s_links.linkCount == 0, "the distance exclusive");
    mvfxDestroyContext(scene.context);
    // Ribbons cut in four, tiled per segment, scrolling, three times as
    // wide and facing up: the links are their own, straight, along 0 to 1,
    // a quarter wide, facing the eye.
    static const double pair[6] = {0, 0, 0, 0.5, 0, 0};
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.ribbon.pieces = 4;
    emitter.ribbon.texture = mvfx_ribbonTilePerSegment;
    emitter.ribbon.scroll = 2.0f;
    emitter.ribbon.facing = mvfx_ribbonFaceNormal;
    emitter.ribbon.normal[1] = 1.0f;
    emitter.ribbon.width.value = 3.0f;
    scene = Run(&emitter, pair, 2);
    WriteLinks(&scene, 4096, 2048);
    CHECK(s_links.pointCount == 2 && s_links.points[1].along == 1.0f &&
              s_links.points[0].width == 0.25f,
          "straight, along, width");
    static Record vertices[8];
    static uint32_t indices[12];
    mvfxExpandBuffers buffers = {
        .vertices = vertices, .vertexCapacity = 8, .indices = indices, .indexCapacity = 12};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandPlexus(scene.context, scene.instance, 0, scene.layout, &s_view,
                                    &buffers, &vertexCount, &indexCount) == mvfx_success &&
              vertexCount == 4 && fabsf(vertices[0].position[1] - 0.125f) <= 1e-6f &&
              vertices[0].position[2] == 0.0f,
          "facing the eye");
    mvfxDestroyContext(scene.context);
}

// Whether every byte of a buffer is value.
static bool Filled(const void* buffer, size_t size, unsigned char value)
{
    const unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != value)
        {
            return false;
        }
    }
    return true;
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Emitter(1.0f);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 8; ++rule)
    {
        emitter = Emitter(1.0f);
        mvfxPlexusDef* p = &emitter.plexus;
        switch (rule)
        {
        case 0:
            p->distance = 0.5f * MVFX_MIN_NEIGHBOR_RADIUS;
            break;
        case 1:
            p->distance = 2.0f * MVFX_MAX_NEIGHBOR_RADIUS;
            break;
        case 2:
            p->maxLinks = 0;
            break;
        case 3:
            p->maxLinks = MVFX_MAX_PLEXUS_LINKS + 1;
            break;
        case 4:
            p->maxPerParticle = 0;
            break;
        case 5:
            p->maxPerParticle = MVFX_MAX_NEIGHBORS + 1;
            break;
        case 6:
            p->width = -1.0f;
            break;
        default:
            p->width = NAN;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want =
            rule == 0 || rule == 1 || rule == 3 || rule == 5 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect, NULL) == want;
    }
    CHECK(refused == 8, "every check");
    emitter = Emitter(MVFX_MAX_NEIGHBOR_RADIUS);
    emitter.plexus.maxLinks = 4;
    emitter.plexus.maxPerParticle = MVFX_MAX_NEIGHBORS;
    emitter.plexus.width = MVFX_MAX_EXTENT;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "the bounds");
    emitter.plexus.enable = false;
    emitter.plexus.maxLinks = -1;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "not enabled, unread");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 7;
    layoutDef.stride = (uint32_t)sizeof(Record);
    mvfxLayoutId layout = {0};
    CHECK(mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success, "layout");
    mvfxRibbonBuffers buffers = {0};
    mvfxExpandBuffers strips = {0};
    int64_t a = 0;
    int64_t b = 0;
    uint64_t misuse = mvfxContext_GetMisuse(context);
    refused = mvfxInstance_WritePlexus(context, instance, 0, layout, NULL, &buffers, &a, &b) ==
              mvfx_errorInvalid;
    refused += mvfxInstance_WritePlexus(context, instance, 1, layout, &s_view, &buffers, &a, &b) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_ExpandPlexus(context, instance, 0, layout, &s_view, NULL, &a, &b) ==
               mvfx_errorInvalid;
    strips.vertexCapacity = -1;
    refused += mvfxInstance_ExpandPlexus(context, instance, 0, layout, &s_view, &strips, &a, &b) ==
               mvfx_errorInvalid;
    CHECK(refused == 4 && mvfxContext_GetMisuse(context) == misuse + 4, "refused, counted");
    // What a refusal leaves: the counts 0, and the ribbons and indices
    // zeroed; the points and vertices too while the layout is live.
    Record held[2];
    mvfxRibbon list[2];
    uint32_t indices[3];
    memset(held, 0xEE, sizeof held);
    memset(list, 0xEE, sizeof list);
    buffers = (mvfxRibbonBuffers){
        .points = held, .pointCapacity = 2, .ribbons = list, .ribbonCapacity = 2};
    a = 7;
    b = 7;
    CHECK(mvfxInstance_WritePlexus(NULL, instance, 0, layout, &s_view, &buffers, &a, &b) ==
                  mvfx_errorInvalid &&
              a == 0 && b == 0 && Filled(held, sizeof held, 0xEE) && Filled(list, sizeof list, 0),
          "no context: the counts 0, the points left, the ribbons zeroed");
    memset(held, 0xEE, sizeof held);
    memset(list, 0xEE, sizeof list);
    CHECK(mvfxInstance_WritePlexus(context, instance, 1, layout, &s_view, &buffers, &a, &b) ==
                  mvfx_errorInvalid &&
              Filled(held, sizeof held, 0) && Filled(list, sizeof list, 0),
          "an emitter out of range: both zeroed");
    memset(held, 0xEE, sizeof held);
    memset(indices, 0xEE, sizeof indices);
    strips = (mvfxExpandBuffers){
        .vertices = held, .vertexCapacity = 2, .indices = indices, .indexCapacity = 3};
    a = 7;
    b = 7;
    CHECK(mvfxInstance_ExpandPlexus(context, instance, 1, layout, &s_view, &strips, &a, &b) ==
                  mvfx_errorInvalid &&
              a == 0 && b == 0 && Filled(held, sizeof held, 0) &&
              Filled(indices, sizeof indices, 0),
          "an emitter out of range: the counts 0, both zeroed");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestBruteForce();
    TestCaps();
    TestPoints();
    TestOwnSettings();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
