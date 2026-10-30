// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Chains by XPBD: units born whole along their direction, counted in
// units against a capacity in points; a pendulum's period against
// 2 pi sqrt(L / g); a rigid hanging chain keeping its spacing; softer
// compliance stretching more; damping; two points moved past 2^63 apart
// still projected and damped; a kill of one point killing its
// unit; a pinned first point through a world rebase; quality levels in
// whole units; each unit a ribbon; the checks.

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
    mvfxEffectId effect;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
    Record records[MOST];
    int64_t count;
} Scene;

static Scene s_scene;

// A chain of points spacing 0.5 apart down from its root, rigid, eight
// substeps, under gravity, pinned.
static mvfxEmitterDef Chain(int32_t points)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 4 * points;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                 .points = points,
                                 .spacing = 0.5f,
                                 .direction = {0, -1, 0},
                                 .substeps = 8,
                                 .pinFirst = true};
    return emitter;
}

static void Open(const mvfxEmitterDef* emitter, const mvfxQualityLevel* level, int32_t quality)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    def.qualityLevels = level;
    def.qualityLevelCount = level != NULL ? 1 : 0;
    CHECK(mvfxCreateEffect(s_scene.context, &def, &s_scene.effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s_scene.effect;
    instanceDef.quality = quality;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = (uint32_t)sizeof(Record);
    CHECK(mvfxCreateLayout(s_scene.context, &layoutDef, &s_scene.layout) == mvfx_success, "layout");
}

static void Spawn(int32_t units, const double* places, const float* velocities)
{
    mvfxSpawnRequest request = {.count = units, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
}

static void Read(void)
{
    CHECK(mvfxInstance_WriteParticles(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                      NULL, 0, s_scene.records, MOST,
                                      &s_scene.count) == mvfx_success,
          "read");
}

static void Steps(int32_t n, int64_t ticks)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, ticks) == mvfx_success, "step");
    }
}

static mvfxEmitterStats Stats(void)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

static double Gap(int32_t a, int32_t b)
{
    double square = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = (double)s_scene.records[a].position[k] - (double)s_scene.records[b].position[k];
        square += d * d;
    }
    return sqrt(square);
}

static void TestBirths(void)
{
    // Two units of five spawned at a tick: their points down from each
    // root, serials by unit, owed in units; a third refused by a capacity
    // of four units once three are alive.
    mvfxEmitterDef emitter = Chain(5);
    emitter.motion.gravity[1] = 0.0f;
    Open(&emitter, NULL, 0);
    static const double roots[9] = {0, 0, 0, 3, 0, 0, 6, 0, 0};
    Spawn(2, roots, NULL);
    Steps(1, 1);
    Read();
    bool placed = s_scene.count == 10;
    for (int32_t i = 0; placed && i < 10; ++i)
    {
        placed = s_scene.records[i].serial == (uint32_t)i &&
                 fabsf(s_scene.records[i].position[1] + 0.5f * (float)(i % 5)) <= 1e-5f &&
                 fabsf(s_scene.records[i].position[0] - 3.0f * (float)(i / 5)) <= 1e-5f;
    }
    CHECK(placed && Stats().owed == 2 && Stats().live == 10, "units born whole, owed in units");
    Spawn(3, roots, NULL);
    Steps(1, 1);
    CHECK(Stats().live == 20 && Stats().refused == 1, "the capacity in units");
    mvfxDestroyContext(s_scene.context);
}

static void TestPendulum(void)
{
    // Two points a length of 1 apart, the first pinned, from 0.1 radians
    // at rest: the period of small swings, 2 pi sqrt(1 / 9.81) (1 + 0.1^2
    // / 16), within one percent, timed by the second's crossings of x = 0.
    mvfxEmitterDef emitter = Chain(2);
    emitter.unit.spacing = 1.0f;
    emitter.unit.direction[0] = sinf(0.1f);
    emitter.unit.direction[1] = -cosf(0.1f);
    Open(&emitter, NULL, 0);
    static const double root[3] = {0, 0, 0};
    static const float rest[3] = {0, 0, 0};
    Spawn(1, root, rest);
    int64_t tick = MVFX_TICKS_PER_SECOND / 240;
    double first = -1.0;
    double last = -1.0;
    int32_t crossings = 0;
    float before = 1.0f;
    double length = 0.0;
    for (int32_t i = 0; i < 240 * 9; ++i)
    {
        Steps(1, tick);
        Read();
        float x = s_scene.records[1].position[0];
        if (i > 0 && (x < 0.0f) != (before < 0.0f))
        {
            double at = (double)(i + 1) / 240.0;
            first = first < 0.0 ? at : first;
            last = at;
            crossings += 1;
        }
        before = x;
        length = Gap(0, 1);
    }
    double period = 2.0 * (last - first) / (double)(crossings - 1);
    double expected = 2.0 * 3.14159265358979 * sqrt(1.0 / 9.81) * (1.0 + 0.01 / 16.0);
    CHECK(crossings >= 8 && fabs(period - expected) <= 0.01 * expected &&
              fabs(length - 1.0) <= 1e-3 && s_scene.records[0].position[1] == 0.0f,
          "a pendulum's period, its length kept, its pin held");
    mvfxDestroyContext(s_scene.context);
}

static void TestHanging(void)
{
    // Eight points hanging rigid for two seconds: every gap within a
    // hundredth of its spacing, the pin held, the chain still below it.
    // Softer compliance stretches the chain more.
    double reach[2] = {0, 0};
    for (int32_t soft = 0; soft < 2; ++soft)
    {
        mvfxEmitterDef emitter = Chain(8);
        emitter.unit.stretchCompliance = soft ? 1e-2f : 0.0f;
        emitter.unit.damping = 2.0f;
        Open(&emitter, NULL, 0);
        static const double root[3] = {0, 0, 0};
        Spawn(1, root, NULL);
        Steps(120, MVFX_TICKS_PER_SECOND / 60);
        Read();
        bool kept = s_scene.records[0].position[0] == 0.0f;
        for (int32_t j = 0; !soft && j + 1 < 8; ++j)
        {
            kept = kept && fabs(Gap(j, j + 1) - 0.5) <= 0.005;
        }
        reach[soft] = -(double)s_scene.records[7].position[1];
        CHECK(kept && reach[soft] >= 3.49 && fabsf(s_scene.records[7].position[0]) <= 1e-5f,
              "hanging, its spacing kept");
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(reach[1] > reach[0] + 0.01, "softer stretches more");
}

static void TestBending(void)
{
    // Twelve points pinned and held out sideways, stiff in stretch and in
    // bending: after half a second every pair two apart is within half a
    // percent of twice the spacing.
    mvfxEmitterDef emitter = Chain(12);
    emitter.unit.direction[0] = 1.0f;
    emitter.unit.direction[1] = 0.0f;
    emitter.unit.substeps = 16;
    emitter.capacity = 12;
    emitter.hostSpawnCapacity = 1;
    Open(&emitter, NULL, 0);
    static const double root[3] = {0, 0, 0};
    Spawn(1, root, NULL);
    Steps(30, MVFX_TICKS_PER_SECOND / 60);
    Read();
    double worst = 0.0;
    for (int32_t j = 0; j + 2 < 12; ++j)
    {
        double off = fabs(Gap(j, j + 2) - 1.0);
        worst = off > worst ? off : worst;
    }
    CHECK(worst <= 0.005, "every bending pair held");
    mvfxDestroyContext(s_scene.context);
}

static void TestDistance(void)
{
    // A jump of 20 at a unit a unit of travel, room for two units: the
    // last two along the jump are born, the rest refused at its start.
    mvfxEmitterDef emitter = Chain(3);
    emitter.motion.gravity[1] = 0.0f;
    emitter.ratePerDistance = 1.0f;
    emitter.capacity = 6;
    emitter.hostSpawnCapacity = 0;
    emitter.unit.pinFirst = false;
    Open(&emitter, NULL, 0);
    static const double far[3] = {20, 0, 0};
    static const float identity[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, far, identity, 1.0f,
                                    false) == mvfx_success,
          "moved");
    Steps(1, MVFX_TICKS_PER_SECOND / 60);
    Read();
    CHECK(s_scene.count == 6 && Stats().owed == 20 && Stats().refused == 18 &&
              s_scene.records[0].position[0] > 18.0f && s_scene.records[3].position[0] > 18.0f,
          "a jump's last units born");
    mvfxDestroyContext(s_scene.context);
}

// Moves the second point of a unit to 10^29 along x once, within
// MVFX_MAX_PLACE.
static void MoveFar(const mvfxModuleCall* call, void* user)
{
    bool* done = user;
    if (!*done && call->particles.count > 1)
    {
        call->particles.position[0][1] = 1e29f;
        *done = true;
    }
}

// A two-point chain, unpinned, without gravity, its second point moved
// 10^29 along x before its second step's motion; the gap after it, or a
// negative one when either point is lost or not finite.
static double FarGap(float stretchCompliance, float damping)
{
    mvfxEmitterDef emitter = Chain(2);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.pinFirst = false;
    emitter.unit.stretchCompliance = stretchCompliance;
    emitter.unit.damping = damping;
    static bool moved;
    moved = false;
    static const mvfxHostModule mover = {
        .stage = mvfx_stageBeforeMotion, .function = MoveFar, .user = &moved};
    emitter.hostModules = &mover;
    emitter.hostModuleCount = 1;
    Open(&emitter, NULL, 0);
    static const double root[3] = {0, 0, 0};
    Spawn(1, root, NULL);
    Steps(2, MVFX_TICKS_PER_SECOND / 60);
    Read();
    bool finite = moved && s_scene.count == 2 && Stats().killed == 0;
    for (int32_t i = 0; finite && i < 2; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            finite = finite && isfinite(s_scene.records[i].position[k]);
        }
    }
    double gap = finite ? Gap(0, 1) : -1.0;
    mvfxDestroyContext(s_scene.context);
    return gap;
}

static void TestFarApart(void)
{
    // Moved 10^29 apart, where the square of the distance between them
    // passes binary32's largest: rigid, the projection still measures it
    // and pulls them together; soft, they stay far apart and the damping
    // still measures it and slows their closing.
    CHECK(FarGap(0.0f, 0.5f) >= 0.0, "rigid, finite");
    double free = FarGap(1.0f, 0.0f);
    double damped = FarGap(1.0f, 1.0f);
    CHECK(free > 1e28 && damped > free, "soft, damped");
}

static void TestFull(void)
{
    // Room for two units of three: A at 0 and B at 0.5, each living a
    // second; a burst of two at 1.1 finds A gone and B alive, so one is
    // born and one refused, in tick order.
    mvfxEmitterDef emitter = Chain(3);
    emitter.motion.gravity[1] = 0.0f;
    emitter.capacity = 6;
    emitter.hostSpawnCapacity = 2;
    emitter.lifetime = mvfxRangeSource(1.0f, 1.0f);
    static const mvfxBurst late = {
        .time = 1.1f, .minCount = 2, .maxCount = 2, .cycles = 1, .probability = 1.0f};
    emitter.bursts = &late;
    emitter.burstCount = 1;
    Open(&emitter, NULL, 0);
    static const double root[3] = {0, 0, 0};
    Spawn(1, root, NULL);
    Steps(2, MVFX_TICKS_PER_SECOND / 4);
    Spawn(1, root, NULL);
    Steps(1, MVFX_TICKS_PER_SECOND / 4);
    CHECK(Stats().live == 6, "two units alive");
    Steps(1, MVFX_TICKS_PER_SECOND / 2);
    CHECK(Stats().live == 6 && Stats().refused == 1 && Stats().owed == 4,
          "in tick order, the room in units");
    mvfxDestroyContext(s_scene.context);
}

// Kills the third point of the first unit after motion.
static void KillOne(const mvfxModuleCall* call, void* user)
{
    (void)user;
    if (call->particles.count > 2)
    {
        call->particles.kill[2] = 1;
    }
}

static void TestKill(void)
{
    mvfxEmitterDef emitter = Chain(4);
    static const mvfxHostModule killer = {.stage = mvfx_stageAfterMotion, .function = KillOne};
    emitter.hostModules = &killer;
    emitter.hostModuleCount = 1;
    Open(&emitter, NULL, 0);
    static const double roots[6] = {0, 0, 0, 5, 0, 0};
    Spawn(2, roots, NULL);
    Steps(1, 1);
    Read();
    CHECK(Stats().live == 4 && Stats().killed == 4 && s_scene.records[0].serial == 4,
          "a kill of one point kills its unit");
    mvfxDestroyContext(s_scene.context);
}

static void TestRebaseAndRibbons(void)
{
    // World space, pinned at the instance's place: the instance moves past
    // the rebase distance and the pin stays in the world.
    mvfxEmitterDef emitter = Chain(3);
    emitter.ribbon.ids = mvfx_ribbonOne;
    Open(&emitter, NULL, 0);
    static const double roots[6] = {10, 20, 30, 11, 20, 30};
    Spawn(2, roots, NULL);
    Steps(1, 1);
    static const double far[3] = {3000, 0, 0};
    static const float identity[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, far, identity, 1.0f, true) ==
              mvfx_success,
          "moved");
    Steps(3, MVFX_TICKS_PER_SECOND / 60);
    Read();
    CHECK(s_scene.records[0].position[0] == 10.0f && s_scene.records[0].position[1] == 20.0f &&
              s_scene.records[3].position[0] == 11.0f,
          "a pin through a rebase");
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
              ribbonCount == 2 && pointCount == 6 && ribbons[0].id == 0 && ribbons[1].id == 3,
          "each unit a ribbon");
    mvfxDestroyContext(s_scene.context);
}

static void TestQuality(void)
{
    // Twenty points in units of five at half: two units; at 0.3: one.
    mvfxEmitterDef emitter = Chain(5);
    static const mvfxQualityLevel half = {.capacityScale = 0.5f, .rateScale = 1.0f};
    static const mvfxQualityLevel less = {.capacityScale = 0.3f, .rateScale = 1.0f};
    Open(&emitter, &half, 1);
    CHECK(mvfxContext_GetParticles(s_scene.context) == 10, "half, whole units");
    mvfxDestroyContext(s_scene.context);
    Open(&emitter, &less, 1);
    CHECK(mvfxContext_GetParticles(s_scene.context) == 5, "less, one unit");
    mvfxDestroyContext(s_scene.context);
}

static void TestKillInSubsteps(void)
{
    // At 60 along x through a kill sphere (radius 0.2 at x = 0.5) in four
    // substeps: the second and third cross it, the last is past it, and
    // the chain dies.
    mvfxEmitterDef emitter = Chain(2);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.pinFirst = false;
    emitter.unit.substeps = 4;
    static const mvfxKillRule sphere = {
        .kind = mvfx_killInsideSphere, .center = {0.5f, 0, 0}, .radius = 0.2f};
    emitter.killRules = &sphere;
    emitter.killRuleCount = 1;
    Open(&emitter, NULL, 0);
    static const double root[3] = {-1, 0, 0};
    static const float fast[3] = {60, 0, 0};
    Spawn(1, root, fast);
    Steps(1, MVFX_TICKS_PER_SECOND / 60);
    CHECK(Stats().live == 2, "short of it");
    Steps(1, MVFX_TICKS_PER_SECOND / 60);
    CHECK(Stats().live == 0 && Stats().killed == 2, "killed in a substep");
    mvfxDestroyContext(s_scene.context);
}

static void TestLifeLossKeepsUnitsWhole(void)
{
    // A chain falling across, its tip alone bouncing off a small sphere:
    // the tip's life shrinks to 0.7 s, but the chain lives by its first
    // point's 1 s, all three points together.
    mvfxEmitterDef emitter = Chain(3);
    emitter.motion.gravity[1] = 0.0f;
    emitter.lifetime = mvfxRangeSource(1.0f, 1.0f);
    emitter.unit.pinFirst = false;
    emitter.unit.direction[0] = 1.0f;
    emitter.unit.direction[1] = 0.0f;
    emitter.unit.substeps = 4;
    emitter.unit.stretchCompliance = 0.01f;
    static const mvfxCollider sphere = {.kind = mvfx_colliderSphere,
                                        .center = {1.0f, -0.6f, 0},
                                        .radius = 0.1f,
                                        .rotation = {0, 0, 0, 1}};
    emitter.colliders = &sphere;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enable = true, .lifeLoss = 0.3f, .restitution = 1.0f};
    Open(&emitter, NULL, 0);
    static const double root[3] = {0, 0, 0};
    static const float down[3] = {0, -6, 0};
    Spawn(1, root, down);
    bool whole = true;
    // Sixty steps of a sixtieth (in whole ticks) fall just short of 1 s.
    for (int32_t s = 0; s < 60; ++s)
    {
        Steps(1, MVFX_TICKS_PER_SECOND / 60);
        whole = whole && Stats().live == 3;
    }
    Steps(1, MVFX_TICKS_PER_SECOND / 60);
    CHECK(whole && Stats().live == 0 && Stats().killed == 0, "the chain ends whole, at its root's");
    mvfxDestroyContext(s_scene.context);
}

static void TestCollisionEvents(void)
{
    // A chain lying on a floor over four substeps, touching it in each:
    // one collision event a point a step (its first contact), in point
    // order, as a particle's.
    mvfxEmitterDef emitter = Chain(3);
    emitter.unit.pinFirst = false;
    emitter.unit.direction[0] = 1.0f;
    emitter.unit.direction[1] = 0.0f;
    emitter.unit.substeps = 4;
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -0.01f, 0}, .normal = {0, 1, 0}};
    emitter.colliders = &floor;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enable = true};
    emitter.events = mvfx_eventMaskCollision;
    emitter.eventCapacity = 64;
    Open(&emitter, NULL, 0);
    static const double root[3] = {0, 0, 0};
    Spawn(1, root, NULL);
    bool once = true;
    int32_t most = 0;
    for (int32_t s = 0; s < 20; ++s)
    {
        Steps(1, MVFX_TICKS_PER_SECOND / 60);
        const mvfxEvent* events = NULL;
        int32_t count = 0;
        CHECK(mvfxInstance_GetEvents(s_scene.context, s_scene.instance, 0, &events, &count) ==
                  mvfx_success,
              "events");
        for (int32_t e = 1; e < count; ++e)
        {
            once = once && events[e].serial > events[e - 1].serial;
        }
        most = count > most ? count : most;
    }
    CHECK(once && most == 3, "a collision event a point a step, in point order");
    mvfxDestroyContext(s_scene.context);
}

static void TestParticlesUnread(void)
{
    // A particle emitter's unit is not read: a stray pin leaves its
    // particles' velocities as given.
    mvfxEmitterDef emitter = Chain(4);
    emitter.unit.kind = mvfx_unitParticle;
    emitter.motion.gravity[1] = 0.0f;
    Open(&emitter, NULL, 0);
    static const double root[3] = {0, 0, 0};
    static const float launch[3] = {2, 0, 0};
    Spawn(1, root, launch);
    Steps(60, MVFX_TICKS_PER_SECOND / 60);
    Read();
    CHECK(s_scene.count == 1 && fabsf(s_scene.records[0].position[0] - 2.0f) <= 1e-3f,
          "a particle's pin unread");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Chain(4);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 15; ++rule)
    {
        emitter = Chain(4);
        mvfxUnitDef* u = &emitter.unit;
        switch (rule)
        {
        case 0:
            u->points = 1;
            break;
        case 1:
            u->points = MVFX_MAX_CHAIN_POINTS + 1;
            break;
        case 2:
            u->spacing = 0.0f;
            break;
        case 3:
            u->direction[1] = 0.0f;
            break;
        case 4:
            u->direction[0] = NAN;
            break;
        case 5:
            u->substeps = 0;
            break;
        case 6:
            u->substeps = MVFX_MAX_SUBSTEPS + 1;
            break;
        case 7:
            u->stretchCompliance = -1.0f;
            break;
        case 8:
            u->bendCompliance = NAN;
            break;
        case 9:
            u->damping = 2.0f * MVFX_MAX_DRAG;
            break;
        case 10:
            emitter.capacity = 18;
            break;
        case 11:
            emitter.hostSpawnCapacity = 5;
            break;
        case 12:
            u->kind = 3;
            break;
        case 13:
            u->spacing = 2.0f * MVFX_MAX_EXTENT;
            break;
        default:
            emitter.eventSpawn = (mvfxEventSpawn){.enable = true,
                                                  .source = 0,
                                                  .kind = mvfx_eventBirth,
                                                  .minCount = 1,
                                                  .maxCount = 1,
                                                  .capacity = 5};
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want =
            rule == 1 || rule == 6 || rule == 9 || rule == 13 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect, NULL) == want;
    }
    CHECK(refused == 15, "every check");
    emitter = Chain(MVFX_MAX_CHAIN_POINTS);
    emitter.unit.substeps = MVFX_MAX_SUBSTEPS;
    emitter.unit.damping = MVFX_MAX_DRAG;
    emitter.unit.stretchCompliance = MVFX_MAX_EXTENT;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "the bounds");
    emitter = Chain(4);
    emitter.unit.kind = mvfx_unitParticle;
    emitter.unit.points = -1;
    emitter.capacity = 15;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "particles, unread");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestBirths();
    TestPendulum();
    TestHanging();
    TestBending();
    TestDistance();
    TestFarApart();
    TestFull();
    TestKill();
    TestRebaseAndRibbons();
    TestQuality();
    TestKillInSubsteps();
    TestLifeLossKeepsUnitsWhole();
    TestCollisionEvents();
    TestParticlesUnread();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
