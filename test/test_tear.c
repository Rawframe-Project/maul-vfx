// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Tearing: a two-point chain's edge tearing under gravity, its event at
// the middle along the edge, the freed point falling; a chain pulled
// apart splitting into two ribbons; a sheet pulled apart across, its
// across edges torn and its mesh's quads gone, sparks spawned at each
// tear; no tear below the strain; the checks.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 64

typedef struct Record
{
    float position[3];
    uint32_t serial;
} Record;

static const mvfxLayoutField s_fields[2] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 12},
};

static const mvfxView s_view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
    Record records[MOST];
    int64_t count;
} Scene;

static Scene s_scene;

// A chain of points down from its root, 0.5 apart, its first pinned,
// tearing past a fifth more, soft in bending and shear (rigid, they would
// hold a stretched edge's ends together); no gravity, one at a time.
static mvfxEmitterDef Chain(int32_t points)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = points;
    emitter.hostSpawnCapacity = 1;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.events = mvfx_eventMaskTear;
    emitter.eventCapacity = 16;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                 .points = points,
                                 .spacing = 0.5f,
                                 .direction = {0, -1, 0},
                                 .substeps = 4,
                                 .shearCompliance = 1.0f,
                                 .bendCompliance = 1.0f,
                                 .pinFirst = true,
                                 .tearStrain = 0.2f};
    return emitter;
}

static void Open(const mvfxEmitterDef* emitters, int32_t count)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = (uint32_t)sizeof(Record);
    CHECK(mvfxCreateLayout(s_scene.context, &layoutDef, &s_scene.layout) == mvfx_success, "layout");
    static const double root[3] = {0, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = root};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
}

static void Read(int32_t emitter)
{
    CHECK(mvfxInstance_WriteParticles(s_scene.context, s_scene.instance, emitter, s_scene.layout,
                                      &s_view, NULL, 0, s_scene.records, MOST,
                                      &s_scene.count) == mvfx_success,
          "read");
}

static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
}

static mvfxEmitterStats Stats(int32_t emitter)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, emitter, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

// Kicks points once, before the first step's motion: those whose serial
// the user's table marks, by its velocity along x and y.
typedef struct Kick
{
    float velocity[MOST][2];
    bool done;
} Kick;

static void KickOnce(const mvfxModuleCall* call, void* user)
{
    Kick* kick = user;
    for (int32_t i = 0; !kick->done && i < call->particles.count; ++i)
    {
        uint32_t s = call->particles.serial[i];
        call->particles.velocity[0][i] += kick->velocity[s][0];
        call->particles.velocity[1][i] += kick->velocity[s][1];
    }
    kick->done = kick->done || call->particles.count > 0;
}

static void TestGravity(void)
{
    // Two points, the second pulled down by gravity, soft enough to
    // stretch past a fifth: one tear, its event at the edge's middle along
    // it (down), and the point falls free.
    mvfxEmitterDef emitter = Chain(2);
    emitter.motion.gravity[1] = -9.81f;
    emitter.unit.stretchCompliance = 0.05f;
    Open(&emitter, 1);
    Steps(1);
    CHECK(Stats(0).torn == 0, "not yet");
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    int32_t step = 1;
    for (; Stats(0).torn == 0 && step < 120; ++step)
    {
        Steps(1);
    }
    CHECK(mvfxInstance_GetEvents(s_scene.context, s_scene.instance, 0, &events, &count) ==
                  mvfx_success &&
              count == 1 && events[0].kind == mvfx_eventTear && events[0].serial == 0,
          "one tear event");
    CHECK(count == 1 && fabs(events[0].place[1] + 0.5 * 0.5 * 1.2) <= 0.05 &&
              fabsf(events[0].normal[1] + 1.0f) <= 1e-5f && fabs(events[0].place[0]) <= 1e-6,
          "at the middle, along the edge");
    Read(0);
    float at = s_scene.records[1].position[1];
    Steps(30);
    Read(0);
    CHECK(Stats(0).torn == 1 && s_scene.records[0].position[1] == 0.0f &&
              s_scene.records[1].position[1] < at - 1.0f,
          "falling free");
    mvfxDestroyContext(s_scene.context);
    // Stiff, it stretches less than a fifth and never tears.
    emitter.unit.stretchCompliance = 1e-4f;
    Open(&emitter, 1);
    Steps(120);
    CHECK(Stats(0).torn == 0, "below the strain");
    mvfxDestroyContext(s_scene.context);
}

static void TestRibbons(void)
{
    // Five points, unpinned, the first three kicked up and the last two
    // down: the edge from point 2 tears, and the chain draws as two
    // ribbons, ids 0 and 3.
    static Kick kick;
    memset(&kick, 0, sizeof kick);
    kick.velocity[0][1] = 40.0f;
    kick.velocity[1][1] = 40.0f;
    kick.velocity[2][1] = 40.0f;
    kick.velocity[3][1] = -40.0f;
    kick.velocity[4][1] = -40.0f;
    mvfxEmitterDef emitter = Chain(5);
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.unit.stretchCompliance = 0.01f;
    emitter.unit.pinFirst = false;
    mvfxHostModule module = {.stage = mvfx_stageBeforeMotion, .function = KickOnce, .user = &kick};
    emitter.hostModules = &module;
    emitter.hostModuleCount = 1;
    Open(&emitter, 1);
    Steps(3);
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    CHECK(Stats(0).torn == 1 && mvfxInstance_GetEvents(s_scene.context, s_scene.instance, 0,
                                                       &events, &count) == mvfx_success,
          "one tear");
    static Record points[MOST];
    static mvfxRibbon ribbons[8];
    static uint32_t scratch[4 * MOST];
    mvfxRibbonBuffers buffers = {.points = points,
                                 .pointCapacity = MOST,
                                 .ribbons = ribbons,
                                 .ribbonCapacity = 8,
                                 .scratch = scratch,
                                 .scratchBytes = sizeof scratch};
    int64_t pointCount = 0;
    int64_t ribbonCount = 0;
    CHECK(mvfxInstance_WriteRibbons(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, &pointCount, &ribbonCount) == mvfx_success &&
              ribbonCount == 2 && ribbons[0].id == 0 && ribbons[0].count == 3 &&
              ribbons[1].id == 3 && ribbons[1].count == 2,
          "two ribbons");
    mvfxDestroyContext(s_scene.context);
}

static void TestSheet(void)
{
    // Three by three, stretchy (tearing reads the stretch the projections
    // leave), its left column kicked to -x and its right to +x: its six
    // edges across tear, none down; its mesh keeps no quad; a spark is
    // born at each tear. Without the kick, it keeps all four.
    for (int32_t kicked = 0; kicked < 2; ++kicked)
    {
        static Kick kick;
        memset(&kick, 0, sizeof kick);
        for (int32_t r = 0; kicked && r < 3; ++r)
        {
            kick.velocity[3 * r][0] = -30.0f;
            kick.velocity[3 * r + 2][0] = 30.0f;
        }
        mvfxEmitterDef emitters[2] = {Chain(3), mvfxDefaultEmitterDef()};
        emitters[0].unit.kind = mvfx_unitSheet;
        emitters[0].unit.rows = 3;
        emitters[0].unit.direction[1] = 0.0f;
        emitters[0].unit.direction[0] = 1.0f;
        emitters[0].unit.downDirection[2] = 1.0f;
        emitters[0].unit.pinFirst = false;
        emitters[0].unit.stretchCompliance = 0.01f;
        emitters[0].capacity = 9;
        mvfxHostModule module = {
            .stage = mvfx_stageBeforeMotion, .function = KickOnce, .user = &kick};
        emitters[0].hostModules = &module;
        emitters[0].hostModuleCount = 1;
        emitters[1].rate = 0.0f;
        emitters[1].capacity = 16;
        emitters[1].lifetime = mvfxRangeSource(1000.0f, 1000.0f);
        emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                                  .source = 0,
                                                  .kind = mvfx_eventTear,
                                                  .minCount = 1,
                                                  .maxCount = 1,
                                                  .capacity = 16};
        Open(emitters, 2);
        Steps(4);
        static Record vertices[MOST];
        static uint32_t indices[MOST];
        mvfxExpandBuffers buffers = {.vertices = vertices,
                                     .vertexCapacity = MOST,
                                     .indices = indices,
                                     .indexCapacity = MOST};
        int64_t vertexCount = 0;
        int64_t indexCount = 0;
        CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout,
                                        &s_view, &buffers, &vertexCount,
                                        &indexCount) == mvfx_success &&
                  vertexCount == 9 && indexCount == (kicked ? 0 : 24),
              "the mesh's quads");
        CHECK(Stats(0).torn == (kicked ? 6u : 0u) && Stats(1).live == (kicked ? 6 : 0),
              "the tears and their sparks");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitters[2] = {Chain(3), mvfxDefaultEmitterDef()};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 2;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 7; ++rule)
    {
        emitters[0] = Chain(3);
        emitters[1] = mvfxDefaultEmitterDef();
        switch (rule)
        {
        case 0:
            emitters[0].unit.tearStrain = -0.1f;
            break;
        case 1:
            emitters[0].unit.tearStrain = NAN;
            break;
        case 2:
            emitters[0].unit.tearStrain = 2.0f * MVFX_MAX_EXTENT;
            break;
        case 3:
            // Tear events from units that never tear.
            emitters[0].unit.tearStrain = 0.0f;
            break;
        case 4:
            emitters[1].events = mvfx_eventMaskTear;
            emitters[1].eventCapacity = 4;
            break;
        case 5:
            emitters[0].events = 16;
            break;
        default:
            emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                                      .source = 0,
                                                      .kind = mvfx_eventTear + 1,
                                                      .minCount = 1,
                                                      .maxCount = 1,
                                                      .capacity = 4};
            break;
        }
        refused += mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid;
    }
    CHECK(refused == 7, "every check");
    emitters[0] = Chain(3);
    emitters[0].unit.tearStrain = MVFX_MAX_EXTENT;
    emitters[1] = mvfxDefaultEmitterDef();
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the bound");
    emitters[0].unit.kind = mvfx_unitParticle;
    emitters[0].unit.tearStrain = -5.0f;
    emitters[0].events = 0;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "a particle's unread");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestGravity();
    TestRibbons();
    TestSheet();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
