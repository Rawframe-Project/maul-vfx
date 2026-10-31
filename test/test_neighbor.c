// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The neighbor grid against brute force over all pairs in double: random
// clouds, a plane, another emitter's grid stepping after it in index;
// capped queries counted only when more are left out; newborns with none;
// a source left out at a quality level; the record fields; the checks.

#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 512

// What a module saw before motion: each particle's place, velocity and
// neighbor values.
typedef struct Seen
{
    int32_t count;
    float place[MOST][3];
    float velocity[MOST][3];
    float neighbors[MOST];
    float center[MOST][3];
    float meanVelocity[MOST][3];
} Seen;

static Seen s_seen[2];

static void Capture(const mvfxModuleCall* call, void* user)
{
    (void)user;
    Seen* seen = &s_seen[call->emitter];
    const mvfxParticleArrays* a = &call->particles;
    seen->count = a->count;
    for (int32_t i = 0; i < a->count && i < MOST; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            seen->place[i][k] = a->position[k][i];
            seen->velocity[i][k] = a->velocity[k][i];
            seen->center[i][k] = a->neighborCenter[k] != NULL ? a->neighborCenter[k][i] : -1.0f;
            seen->meanVelocity[i][k] =
                a->neighborVelocity[k] != NULL ? a->neighborVelocity[k][i] : -1.0f;
        }
        seen->neighbors[i] = a->neighborCount != NULL ? a->neighborCount[i] : -1.0f;
    }
}

static const mvfxHostModule s_capture = {.stage = mvfx_stageBeforeMotion, .function = Capture};

static mvfxEmitterDef Emitter(float radius, int32_t most)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxMakeRangeSource(100.0f, 100.0f);
    emitter.hostModules = &s_capture;
    emitter.hostModuleCount = 1;
    emitter.neighbors =
        (mvfxNeighborDef){.enable = true, .source = -1, .radius = radius, .maxNeighbors = most};
    return emitter;
}

typedef struct Scene
{
    mvfxContext* context;
    mvfxEffectId effect;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitters, int32_t count, const mvfxQualityLevel* level)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = count;
    def.qualityLevels = level;
    def.qualityLevelCount = level != NULL ? 1 : 0;
    CHECK(mvfxCreateEffect(scene.context, &def, &scene.effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = scene.effect;
    instanceDef.origin[0] = 5000.0;
    instanceDef.quality = level != NULL ? 1 : 0;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    memset(s_seen, 0, sizeof s_seen);
    return scene;
}

static uint32_t s_random = 12345u;

static float Uniform(float low, float high)
{
    s_random = s_random * 1664525u + 1013904223u;
    return low + (high - low) * (float)(s_random >> 8u) / 16777216.0f;
}

static double s_places[MOST * 3];
static float s_velocities[MOST * 3];

// Spawns count particles: random within a box of side, or on a plane's
// lattice of spacing when side is negative.
static void Spawn(Scene* scene, int32_t emitter, int32_t count, float side)
{
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            s_places[3 * i + k] = 5000.0 * (k == 0) + (double)Uniform(0.0f, side);
            s_velocities[3 * i + k] = Uniform(-1.0f, 1.0f);
        }
        if (side < 0.0f)
        {
            s_places[3 * i + 0] = 5000.0 + 0.3 * (double)(i % 16);
            s_places[3 * i + 1] = 0.3 * (double)(i / 16);
            s_places[3 * i + 2] = 0.0;
        }
    }
    mvfxSpawnRequest request = {.count = count, .positions = s_places, .velocities = s_velocities};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, emitter, &request) == mvfx_success,
          "spawned");
}

// Steps two ticks: the births, then a step whose start the module sees.
static void Settle(Scene* scene)
{
    CHECK(mvfxContext_StepTicks(scene->context, 1) == mvfx_success &&
              mvfxContext_StepTicks(scene->context, 1) == mvfx_success,
          "stepped");
}

// Whether what the queries of `query` found matches brute force over
// `source`'s particles (its own leaving each particle out), uncapped.
static bool MatchesBruteForce(const Seen* query, const Seen* source, float radius, bool own)
{
    bool ok = query->count > 0;
    for (int32_t i = 0; i < query->count; ++i)
    {
        int32_t count = 0;
        double place[3] = {0, 0, 0};
        double velocity[3] = {0, 0, 0};
        for (int32_t j = 0; j < source->count; ++j)
        {
            double square = 0.0;
            for (int32_t k = 0; k < 3; ++k)
            {
                double d = (double)source->place[j][k] - (double)query->place[i][k];
                square += d * d;
            }
            if ((own && i == j) || !(square < (double)radius * (double)radius))
            {
                continue;
            }
            count += 1;
            for (int32_t k = 0; k < 3; ++k)
            {
                place[k] += (double)source->place[j][k];
                velocity[k] += (double)source->velocity[j][k];
            }
        }
        ok = ok && query->neighbors[i] == (float)count;
        for (int32_t k = 0; ok && k < 3; ++k)
        {
            double center = count > 0 ? place[k] / count - (double)query->place[i][k] : 0.0;
            double mean = count > 0 ? velocity[k] / count - (double)query->velocity[i][k] : 0.0;
            ok = fabs(center - (double)query->center[i][k]) <= 1e-4 &&
                 fabs(mean - (double)query->meanVelocity[i][k]) <= 1e-4;
        }
    }
    return ok;
}

static uint64_t Capped(const Scene* scene, int32_t emitter)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, emitter, &stats) ==
              mvfx_success,
          "stats");
    return stats.cappedNeighborQueryCount;
}

static void TestClouds(void)
{
    // A sparse cloud, a dense one, a plane's lattice.
    static const float sides[3] = {6.0f, 1.5f, -1.0f};
    static const float radii[3] = {0.7f, 0.25f, 0.35f};
    for (int32_t c = 0; c < 3; ++c)
    {
        mvfxEmitterDef emitter = Emitter(radii[c], MVFX_MAX_NEIGHBORS);
        Scene scene = NewScene(&emitter, 1, NULL);
        Spawn(&scene, 0, 400, sides[c]);
        Settle(&scene);
        CHECK(s_seen[0].count == 400 && MatchesBruteForce(&s_seen[0], &s_seen[0], radii[c], true) &&
                  Capped(&scene, 0) == 0,
              "brute force over all pairs");
        mvfxDestroyContext(scene.context);
    }
    // On the plane's lattice, four neighbors inside, two at a corner.
    CHECK(s_seen[0].neighbors[17] == 4.0f && s_seen[0].neighbors[0] == 2.0f, "the lattice's");
}

static void TestEdges(void)
{
    // Exactly the radius apart: not neighbors. Far out, past 2^30 cells
    // each way: the outermost cells, still neighbors.
    mvfxEmitterDef emitter = Emitter(0.5f, MVFX_MAX_NEIGHBORS);
    Scene scene = NewScene(&emitter, 1, NULL);
    static const double apart[6] = {5000.0, 0, 0, 5000.5, 0, 0};
    static const float still[6] = {0, 0, 0, 0, 0, 0};
    mvfxSpawnRequest request = {.count = 2, .positions = apart, .velocities = still};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Settle(&scene);
    CHECK(s_seen[0].count == 2 && s_seen[0].neighbors[0] == 0.0f, "at the radius, none");
    mvfxDestroyContext(scene.context);
    emitter = Emitter(MVFX_MIN_NEIGHBOR_RADIUS, MVFX_MAX_NEIGHBORS);
    scene = NewScene(&emitter, 1, NULL);
    static const double far[12] = {-4e5, 3e5, 0, -4e5, 3e5, 5e-5, 9e5, -2e5, 0, 9e5, -2e5, 5e-5};
    static const float none[12] = {0};
    mvfxSpawnRequest farRequest = {.count = 4, .positions = far, .velocities = none};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &farRequest) == mvfx_success,
          "spawned far");
    Settle(&scene);
    CHECK(s_seen[0].count == 4 && s_seen[0].neighbors[0] == 1.0f && s_seen[0].neighbors[3] == 1.0f,
          "the outermost cells");
    mvfxDestroyContext(scene.context);
}

static void TestCapped(void)
{
    // Nine in a clump at a cap of 8: eight each, none left out; ten: each
    // stops at eight, counted.
    for (int32_t n = 9; n <= 10; ++n)
    {
        mvfxEmitterDef emitter = Emitter(1.0f, 8);
        Scene scene = NewScene(&emitter, 1, NULL);
        Spawn(&scene, 0, n, 0.1f);
        Settle(&scene);
        bool eight = true;
        for (int32_t i = 0; i < n; ++i)
        {
            eight = eight && s_seen[0].neighbors[i] == 8.0f;
        }
        CHECK(eight && Capped(&scene, 0) == (uint64_t)(n == 10 ? 10 : 0),
              "at most the cap, counted when more are left out");
        mvfxDestroyContext(scene.context);
    }
}

static void TestOtherGrid(void)
{
    // Emitter 0 finds emitter 1's particles, so 1 steps first; 1 keeps no
    // neighbors of its own but has a grid.
    mvfxEmitterDef emitters[2] = {Emitter(0.5f, MVFX_MAX_NEIGHBORS),
                                  Emitter(0.5f, MVFX_MAX_NEIGHBORS)};
    emitters[0].neighbors.source = 1;
    emitters[1].neighbors.enable = false;
    Scene scene = NewScene(emitters, 2, NULL);
    Spawn(&scene, 0, 150, 3.0f);
    Spawn(&scene, 1, 250, 3.0f);
    Settle(&scene);
    CHECK(s_seen[0].count == 150 && s_seen[1].count == 250 &&
              MatchesBruteForce(&s_seen[0], &s_seen[1], 0.5f, false) &&
              s_seen[1].neighbors[0] == -1.0f,
          "another emitter's grid, at its step's start");
    mvfxDestroyContext(scene.context);
    // Queried within 0.5 by the first and 0.2 by itself: its cells are the
    // larger.
    emitters[1].neighbors.enable = true;
    emitters[1].neighbors.radius = 0.2f;
    scene = NewScene(emitters, 2, NULL);
    Spawn(&scene, 0, 150, 2.0f);
    Spawn(&scene, 1, 250, 2.0f);
    Settle(&scene);
    CHECK(MatchesBruteForce(&s_seen[0], &s_seen[1], 0.5f, false) &&
              MatchesBruteForce(&s_seen[1], &s_seen[1], 0.2f, true),
          "two radii, the cells the larger");
    mvfxDestroyContext(scene.context);
    // Its source left out at a quality level: none found.
    static const mvfxQualityLevel level = {.capacityScale = 1.0f, .rateScale = 1.0f, .omitted = 2u};
    scene = NewScene(emitters, 2, &level);
    Spawn(&scene, 0, 20, 0.5f);
    Settle(&scene);
    CHECK(s_seen[0].count == 20 && s_seen[0].neighbors[3] == 0.0f && s_seen[0].center[3][1] == 0.0f,
          "a source left out");
    mvfxDestroyContext(scene.context);
}

// A record: the neighbor count as a word, the center offset, the mean
// velocity's offset.
typedef struct Record
{
    uint32_t count;
    float center[3];
    float velocity[3];
} Record;

static void TestRecords(void)
{
    mvfxEmitterDef emitter = Emitter(2.0f, MVFX_MAX_NEIGHBORS);
    emitter.space = mvfx_spaceLocal;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.scale = 2.0f;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    static const mvfxLayoutField fields[3] = {
        {.field = mvfx_recordNeighborCount, .format = mvfx_formatUint32, .components = 1},
        {.field = mvfx_recordNeighborCenter,
         .format = mvfx_formatFloat32,
         .components = 3,
         .offset = 4},
        {.field = mvfx_recordNeighborVelocity,
         .format = mvfx_formatFloat32,
         .components = 3,
         .offset = 16},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 3;
    layoutDef.stride = (uint32_t)sizeof(Record);
    mvfxLayoutId layout = {0};
    CHECK(mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success, "layout");
    static const double places[6] = {0, 0, 0, 1, 0, 0};
    static const float velocities[6] = {0, 0, 0, 0, 3, 0};
    mvfxSpawnRequest request = {.count = 2, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(context, instance, 0, &request) == mvfx_success, "spawned");
    static const mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    Record records[2];
    int64_t count = 0;
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success &&
              mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, records, 2,
                                          &count) == mvfx_success &&
              count == 2 && records[0].count == 0 && records[1].center[0] == 0.0f,
          "newborns have none");
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success &&
              mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, records, 2,
                                          &count) == mvfx_success,
          "written");
    // The local offsets are 1 and 3; in the world twice that.
    CHECK(records[0].count == 1 && records[1].count == 1 && records[0].center[0] == 2.0f &&
              records[1].center[0] == -2.0f && records[0].velocity[1] == 6.0f &&
              records[1].velocity[1] == -6.0f,
          "in the world");
    mvfxDestroyContext(context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitters[2] = {Emitter(1.0f, 4), Emitter(1.0f, 4)};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 2;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 12; ++rule)
    {
        emitters[0] = Emitter(1.0f, 4);
        emitters[1] = Emitter(1.0f, 4);
        mvfxNeighborDef* n = &emitters[0].neighbors;
        switch (rule)
        {
        case 0:
            n->radius = 0.5f * MVFX_MIN_NEIGHBOR_RADIUS;
            break;
        case 1:
            n->radius = 2.0f * MVFX_MAX_NEIGHBOR_RADIUS;
            break;
        case 2:
            n->radius = NAN;
            break;
        case 3:
            n->maxNeighbors = 0;
            break;
        case 4:
            n->maxNeighbors = MVFX_MAX_NEIGHBORS + 1;
            break;
        case 5:
            n->source = 2;
            break;
        case 6:
            n->source = -2;
            break;
        case 10:
            // Far out of range either way: refused before any lookup.
            n->source = 1000;
            break;
        case 11:
            n->source = INT32_MIN;
            break;
        case 7:
            n->source = 1;
            emitters[1].space = mvfx_spaceLocal;
            break;
        case 8:
            // A cycle: 0 finds 1's neighbors, 1 spawns from 0's births.
            n->source = 1;
            emitters[0].events = mvfx_eventMaskBirth;
            emitters[1].eventSpawn = (mvfxEventSpawn){.enable = true,
                                                      .source = 0,
                                                      .kind = mvfx_eventBirth,
                                                      .minCount = 1,
                                                      .maxCount = 1,
                                                      .capacity = 4};
            break;
        default:
            // Two finding each other's.
            n->source = 1;
            emitters[1].neighbors.source = 0;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = rule == 0 || rule == 1 || rule == 4 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect, NULL) == want;
    }
    CHECK(refused == 12, "every check");
    emitters[0] = Emitter(MVFX_MAX_NEIGHBOR_RADIUS, MVFX_MAX_NEIGHBORS);
    emitters[1] = Emitter(MVFX_MIN_NEIGHBOR_RADIUS, 1);
    emitters[1].neighbors.source = 1;
    emitters[0].neighbors.enable = false;
    emitters[0].neighbors.radius = NAN;
    emitters[0].neighbors.source = 1000;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success,
          "the bounds; its own index; a def not enabled unread");
    static const mvfxLayoutField wrong[3] = {
        {.field = mvfx_recordNeighborCount, .format = mvfx_formatFloat16, .components = 1},
        {.field = mvfx_recordNeighborCenter, .format = mvfx_formatFloat32, .components = 4},
        {.field = 22, .format = mvfx_formatFloat32, .components = 1},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fieldCount = 1;
    layoutDef.stride = 64;
    mvfxLayoutId layout = {0};
    refused = 0;
    for (int32_t f = 0; f < 3; ++f)
    {
        layoutDef.fields = &wrong[f];
        refused += mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_errorInvalid;
    }
    CHECK(refused == 3, "the fields' formats and components");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestClouds();
    TestEdges();
    TestCapped();
    TestOtherGrid();
    TestRecords();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
