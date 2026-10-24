// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sleeping rigid particles: a box at rest falls asleep, keeps its pose
// bit for bit and still dies at the end of its life; a sleeping stack
// stays asleep while a box falls past it and wakes as one when it lands,
// then sleeps again stacked; gravity turned by the instance wakes a box,
// which slides; moving starts its time to sleep again; a heavy box landing on a
// sleeper and a box rebounding off a sleeping stack as on awake ones; a
// box spinning in place stays awake; a far sleeping pile sleeping on; a
// host table set again unchanged leaves a box asleep, moved wakes it to
// fall; changed colliders wake only the bodies near them; a
// heightfield's values changed wake a box, and one taken away and
// destroyed is never read; a heightfield's points changed wake only the
// boxes near them, at any height; a depth image set again wakes a box;
// the host wakes bodies within a sphere; no sleep speed, no sleeping; the
// checks.
//
// A body is seen asleep as a host sees it: its pose the same bits over a
// step and its velocities 0, the bodies so seen no more than the emitter's
// statistics count asleep.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene s_scene;

enum
{
    MOST = 16
};

// The bodies as the host modules see them after motion: in the last step
// and in the one before it.
typedef struct Body
{
    float pose[7];
    float velocity[3];
    float angular[3];
    uint32_t serial;
} Body;

typedef struct Seen
{
    int32_t count;
    Body bodies[MOST];
} Seen;

static Seen s_seen[2];

static void Peek(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    s_seen[1] = s_seen[0];
    s_seen[0].count = p->count < MOST ? p->count : MOST;
    for (int32_t i = 0; i < s_seen[0].count; ++i)
    {
        Body* body = &s_seen[0].bodies[i];
        for (int32_t k = 0; k < 3; ++k)
        {
            body->pose[k] = p->position[k][i];
            body->velocity[k] = p->velocity[k][i];
            body->angular[k] = p->angularVelocity[k][i];
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            body->pose[3 + k] = p->orientation[k][i];
        }
        body->serial = p->serial[i];
    }
}

static const mvfxHostModule s_peek = {.stage = mvfx_stageAfterMotion, .function = Peek};

static const mvfxCollider s_ground = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};

// Unit boxes with gravity on the ground, sleeping at 0.05 after 0.5 s.
static mvfxEmitterDef Boxes(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 4;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1}, .max = {1, 1, 1}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, 0}, .max = {0, 0, 0}};
    emitter.rigid.enabled = true;
    emitter.rigid.shape = mvfx_rigidBox;
    emitter.rigid.density = 1.0f;
    emitter.rigid.substeps = 8;
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    emitter.collision.enabled = true;
    emitter.colliders = &s_ground;
    emitter.colliderCount = 1;
    return emitter;
}

static void OpenContext(void)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
}

static mvfxResult EffectOf(const mvfxEmitterDef* emitter, mvfxEffectId* effect)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    return mvfxCreateEffect(s_scene.context, &def, effect);
}

static void Play(const mvfxEmitterDef* emitter)
{
    mvfxEmitterDef peeked = *emitter;
    peeked.hostModules = &s_peek;
    peeked.hostModuleCount = 1;
    memset(s_seen, 0, sizeof s_seen);
    mvfxEffectId effect = {0};
    CHECK(EffectOf(&peeked, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
}

static void Spawn(const double* places, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
}

static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
}

static mvfxEmitterStats Stats(void)
{
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

// Body i as the last step left it.
static const Body* BodyAt(int32_t i)
{
    CHECK(i < s_seen[0].count, "the body seen");
    return &s_seen[0].bodies[i];
}

// Whether body i is still: its pose the same bits over the last step and
// its velocities 0.
static bool Still(int32_t i)
{
    const Body* now = BodyAt(i);
    const Body* before = &s_seen[1].bodies[i];
    bool still = i < s_seen[1].count && now->serial == before->serial &&
                 memcmp(now->pose, before->pose, sizeof now->pose) == 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        still = still && now->velocity[k] == 0.0f && now->angular[k] == 0.0f;
    }
    return still;
}

// Whether body i sleeps: still, the still bodies no more than the asleep
// (one falling asleep within the last step moved in it).
static bool Asleep(int32_t i)
{
    int32_t still = 0;
    for (int32_t j = 0; j < s_seen[0].count; ++j)
    {
        still += Still(j) ? 1 : 0;
    }
    CHECK(still <= Stats().asleep, "the still bodies asleep");
    return Still(i);
}

static void TestFallsAsleep(void)
{
    // A box dropped from 0.1 above the ground, living 3 s: asleep by 1.5
    // s, its pose the same bits half a second on with no velocity, and
    // gone after its life.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.lifetime = mvfxRangeSource(3.0f, 3.0f);
    Play(&boxes);
    static const double place[3] = {0, 0.6, 0};
    Spawn(place, 1);
    Steps(90);
    bool asleep = Stats().asleep == 1 && Asleep(0);
    float before[7];
    memcpy(before, BodyAt(0)->pose, sizeof before);
    Steps(30);
    const Body* after = BodyAt(0);
    bool held = memcmp(before, after->pose, sizeof before) == 0 && after->velocity[1] == 0.0f;
    CHECK(asleep && held && fabsf(after->pose[1] - 0.5f) <= 0.01f, "a box at rest falls asleep");
    Steps(70);
    CHECK(Stats().live == 0, "a sleeping box still dies");
    mvfxDestroyContext(s_scene.context);
}

static void TestPileWakes(void)
{
    // Two boxes stacked sleep; a third dropped from 3 m leaves them asleep
    // as it falls, wakes both as it lands, and the three sleep stacked.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.rigid.bodyContacts = 8;
    Play(&boxes);
    static const double stack[6] = {0, 0.5, 0, 0, 1.501, 0};
    Spawn(stack, 2);
    Steps(120);
    bool slept = Stats().asleep == 2;
    static const double above[3] = {0, 4.0, 0};
    Spawn(above, 1);
    Steps(1);
    bool passed = Stats().asleep == 2;
    bool woke = false;
    for (int32_t i = 0; i < 90 && !woke; ++i)
    {
        Steps(1);
        woke = Stats().asleep == 0;
    }
    CHECK(slept && passed && woke, "a landing box wakes the pile");
    Steps(240);
    bool stacked = Stats().asleep == 3;
    for (int32_t i = 0; stacked && i < 3; ++i)
    {
        stacked = fabsf(BodyAt(i)->pose[1] - (0.5f + (float)BodyAt(i)->serial)) <= 0.02f;
    }
    CHECK(stacked, "the three sleep stacked");
    mvfxDestroyContext(s_scene.context);
}

static void TestForcesWake(void)
{
    // A box in the instance's space on its ground, gravity the world's:
    // asleep, then the instance tilted 40 degrees (past friction's 31) so
    // gravity turns in the box's space: it wakes and slides.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .space = mvfx_spaceLocal, .normal = {0, 1, 0}};
    boxes.space = mvfx_spaceLocal;
    boxes.motion.gravitySpace = mvfx_spaceWorld;
    boxes.colliders = &ground;
    Play(&boxes);
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Stats().asleep == 1;
    static const double origin[3] = {0, 0, 0};
    const float tilt[4] = {0, 0, sinf(0.3490659f), cosf(0.3490659f)};
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, origin, tilt, 1.0f, true) ==
              mvfx_success,
          "tilted");
    Steps(1);
    bool woke = Stats().asleep == 0;
    Steps(30);
    CHECK(slept && woke && fabsf(BodyAt(0)->pose[0]) > 0.1f, "turned gravity wakes a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestStillResets(void)
{
    // A box still 0.4 s of a 2 s sleep time, tilted to slide for three
    // steps and laid flat again: its time to sleep starts again once it
    // stops, so it is awake 1.9 s after, where 1.6 s would have been left.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .space = mvfx_spaceLocal, .normal = {0, 1, 0}};
    boxes.space = mvfx_spaceLocal;
    boxes.motion.gravitySpace = mvfx_spaceWorld;
    boxes.colliders = &ground;
    boxes.rigid.sleepTime = 2.0f;
    Play(&boxes);
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(24);
    bool awake = Stats().asleep == 0;
    static const double origin[3] = {0, 0, 0};
    const float tilt[4] = {0, 0, sinf(0.3490659f), cosf(0.3490659f)};
    const float flat[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, origin, tilt, 1.0f, true) ==
              mvfx_success,
          "tilted");
    Steps(3);
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, origin, flat, 1.0f, true) ==
              mvfx_success,
          "flat");
    Steps(114);
    bool later = Stats().asleep == 0;
    Steps(126);
    CHECK(awake && later && Stats().asleep == 1, "moving starts its time to sleep again");
    mvfxDestroyContext(s_scene.context);
}

// A body of size `big` dropped from 4 m with restitution `bounce` onto
// the boxes at `places`, which sleep first or never: the lowest the
// first box goes, and the dropped body's fastest rise once it lands.
static void Landing(int32_t count, const double* places, float big, float bounce, bool sleeps,
                    float* lowest, float* rise)
{
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.rigid.bodyContacts = 8;
    boxes.rigid.restitution = bounce;
    boxes.rigid.sleepSpeed = sleeps ? boxes.rigid.sleepSpeed : 0.0f;
    Play(&boxes);
    Spawn(places, count);
    Steps(120);
    CHECK(Stats().asleep == (sleeps ? count : 0), "asleep as asked");
    static const double high[3] = {0, 4.0, 0};
    const float sizes[3] = {big, big, big};
    mvfxSpawnRequest dropped = {.count = 1, .positions = high, .sizes = sizes};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &dropped) == mvfx_success,
          "dropped");
    *lowest = INFINITY;
    *rise = 0.0f;
    for (int32_t i = 0; i < 90; ++i)
    {
        Steps(1);
        *lowest = fminf(*lowest, BodyAt(0)->pose[1]);
        *rise = fmaxf(*rise, BodyAt(count)->velocity[1]);
    }
    mvfxDestroyContext(s_scene.context);
}

static void TestSleeperMeets(void)
{
    // A box twice the size (eight times the mass) dropped onto a box, and
    // a box at restitution 0.5 dropped onto a stack of two: on sleeping
    // boxes as on awake ones. A sleeper is woken, with its whole pile,
    // before the step its toucher meets it, so it is never met as
    // immovable ground: woken after, the dropped box rebounded off the
    // stack at 2.16 m/s where off an awake one at 1.49.
    static const double low[3] = {0, 0.5, 0};
    static const double stack[6] = {0, 0.5, 0, 0, 1.501, 0};
    float lowest[2];
    float rise[2];
    for (int32_t sleeps = 0; sleeps < 2; ++sleeps)
    {
        Landing(1, low, 2.0f, 0.0f, sleeps != 0, &lowest[sleeps], &rise[sleeps]);
    }
    CHECK(fabsf(lowest[1] - lowest[0]) <= 1e-4f && lowest[1] >= 0.498f,
          "a heavy landing on a sleeper as on an awake box");
    for (int32_t sleeps = 0; sleeps < 2; ++sleeps)
    {
        Landing(2, stack, 1.0f, 0.5f, sleeps != 0, &lowest[sleeps], &rise[sleeps]);
    }
    CHECK(fabsf(rise[1] - rise[0]) <= 0.01f && rise[0] > 1.0f,
          "a rebound off a sleeping stack as off an awake one");
}

// Far depth everywhere, from a camera at (0, 0, 10) looking down -Z.
static const float s_far[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static void TestFarPileSleeps(void)
{
    // Two sleeping stacks 4 m apart and a box dropped on one: only that
    // one wakes, the other's sleepers, touching only each other, sleep on.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.capacity = 6;
    boxes.hostSpawnCapacity = 6;
    boxes.rigid.bodyContacts = 8;
    Play(&boxes);
    static const double stacks[12] = {0, 0.5, 0, 0, 1.501, 0, 4, 0.5, 0, 4, 1.501, 0};
    Spawn(stacks, 4);
    Steps(120);
    bool slept = Stats().asleep == 4;
    static const double above[3] = {0, 4.0, 0};
    Spawn(above, 1);
    bool far = true;
    for (int32_t i = 0; i < 90; ++i)
    {
        Steps(1);
        for (int32_t j = 0; j < 5; ++j)
        {
            far = far && (BodyAt(j)->pose[0] < 2.0f || Asleep(j));
        }
    }
    CHECK(slept && far && Stats().asleep >= 2, "a far pile sleeps on");
    mvfxDestroyContext(s_scene.context);
}

static void TestWorldNear(void)
{
    // Boxes asleep on two host tables 6 apart: a table lowered wakes the
    // box on it alone; a ball added far off wakes none; the other table
    // and the ball taken away wake the box that stood on it alone.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.collision.hostColliders = true;
    Play(&boxes);
    mvfxCollider set[3] = {{.kind = mvfx_colliderBox,
                            .center = {-3, -0.5f, 0},
                            .halfExtents = {1, 0.5f, 1},
                            .rotation = {0, 0, 0, 1}},
                           {.kind = mvfx_colliderBox,
                            .center = {3, -0.5f, 0},
                            .halfExtents = {1, 0.5f, 1},
                            .rotation = {0, 0, 0, 1}},
                           {.kind = mvfx_colliderSphere, .center = {0, 3, 0}, .radius = 0.5f}};
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 2) == mvfx_success,
          "tables");
    static const double places[6] = {-3, 0.5, 0, 3, 0.5, 0};
    Spawn(places, 2);
    Steps(60);
    bool slept = Asleep(0) && Asleep(1);
    set[0].center[1] = -0.8f;
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 2) == mvfx_success,
          "one lowered");
    Steps(1);
    bool lowered = !Asleep(0) && Asleep(1);
    Steps(60);
    bool settled = Asleep(0) && Asleep(1);
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 3) == mvfx_success,
          "a ball");
    Steps(1);
    bool far = Asleep(0) && Asleep(1);
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 1) == mvfx_success,
          "one table");
    Steps(1);
    bool removed = Asleep(0) && !Asleep(1);
    CHECK(slept && lowered && settled && far && removed, "the world wakes the bodies near it");
    mvfxDestroyContext(s_scene.context);
}

static void TestGridChanges(void)
{
    // A box asleep on the host's heightfield: the heights lowered 0.3 wake
    // it and it settles lower; then the heightfield taken away and its
    // grid destroyed, the next steps never read it.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.collision.hostColliders = true;
    Play(&boxes);
    static const float flat[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const float lower[9] = {-0.3f, -0.3f, -0.3f, -0.3f, -0.3f, -0.3f, -0.3f, -0.3f, -0.3f};
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 3;
    def.size[2] = 3;
    def.values = flat;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_scene.context, &def, &grid) == mvfx_success, "heights");
    const mvfxCollider field = {.kind = mvfx_colliderHeightfield,
                                .halfExtents = {2, 0, 2},
                                .rotation = {0, 0, 0, 1},
                                .grid = grid};
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &field, 1) == mvfx_success,
          "field");
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Asleep(0);
    CHECK(mvfxGrid_SetValues(s_scene.context, grid, lower, 9) == mvfx_success, "lowered");
    Steps(1);
    bool woke = !Asleep(0);
    Steps(60);
    bool settled = Asleep(0) && BodyAt(0)->pose[1] < 0.35f;
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, NULL, 0) == mvfx_success,
          "taken away");
    bool destroyed = mvfxDestroyGrid(s_scene.context, grid) == mvfx_success;
    Steps(2);
    CHECK(slept && woke && settled && destroyed && !Asleep(0), "a heightfield's changes wake");
    mvfxDestroyContext(s_scene.context);
}

// Heights for a 9 by 9 heightfield, 1 m apart from -4 to 4.
static float s_heights[81];

// Lowers the heights from point (x, z), n by n of them, by drop.
static void Dig(mvfxGridId grid, int32_t x, int32_t z, int32_t n, float drop)
{
    float box[81];
    for (int32_t j = 0; j < n; ++j)
    {
        for (int32_t i = 0; i < n; ++i)
        {
            s_heights[(z + j) * 9 + x + i] -= drop;
            box[j * n + i] = s_heights[(z + j) * 9 + x + i];
        }
    }
    const int32_t first[3] = {x, 0, z};
    const int32_t size[3] = {n, 1, n};
    CHECK(mvfxGrid_SetValuesIn(s_scene.context, grid, first, size, box, (int64_t)n * n) ==
              mvfx_success,
          "dug");
}

static void TestGridRegions(void)
{
    // Boxes asleep at x = -3 and 3 on a heightfield: the heights lowered
    // under the second (points 6 to 8 along X, 3 to 5 along Z, so the
    // cells from x = 1 to the edge and z = -2 to 2) wake it alone, which
    // settles lower; a corner far from both lowered wakes neither; heights
    // lowered under the second before a step of no time and at that
    // corner after it wake it, the step of no time meeting the first; a
    // point at x = -2 or at x = -4 lowered wakes the first, the cells on
    // either side of a point changing with it; new heights everywhere
    // wake both. A sleeper's height above the heightfield never keeps it
    // asleep: its heights may rise to any.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.collision.hostColliders = true;
    Play(&boxes);
    for (int32_t i = 0; i < 81; ++i)
    {
        s_heights[i] = 0.0f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 9;
    def.size[2] = 9;
    def.values = s_heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_scene.context, &def, &grid) == mvfx_success, "heights");
    const mvfxCollider field = {.kind = mvfx_colliderHeightfield,
                                .halfExtents = {4, 0, 4},
                                .rotation = {0, 0, 0, 1},
                                .grid = grid};
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &field, 1) == mvfx_success,
          "field");
    static const double places[6] = {-3, 0.5, 0, 3, 0.5, 0};
    Spawn(places, 2);
    Steps(60);
    bool slept = Asleep(0) && Asleep(1);
    Dig(grid, 6, 3, 3, 0.2f);
    Steps(1);
    bool one = Asleep(0) && !Asleep(1);
    Steps(60);
    bool settled = Asleep(1) && BodyAt(1)->pose[1] < 0.35f;
    Dig(grid, 0, 7, 2, 0.2f);
    Steps(1);
    bool neither = Asleep(0) && Asleep(1);
    Dig(grid, 6, 3, 3, 0.1f);
    CHECK(mvfxContext_StepTicks(s_scene.context, 0) == mvfx_success, "no time");
    Dig(grid, 0, 7, 2, 0.1f);
    Steps(1);
    bool kept = Asleep(0) && !Asleep(1);
    Steps(60);
    bool sides = true;
    for (int32_t x = 2; x >= 0; x -= 2)
    {
        sides = sides && Asleep(0);
        Dig(grid, x, 4, 1, 0.05f);
        Steps(1);
        sides = sides && !Asleep(0);
        Steps(60);
    }
    CHECK(mvfxGrid_SetValues(s_scene.context, grid, s_heights, 81) == mvfx_success, "all");
    Steps(1);
    bool both = !Asleep(0) && !Asleep(1);
    CHECK(slept && one && settled && neither && kept && sides && both,
          "a heightfield's changed points wake the bodies near them");
    mvfxDestroyContext(s_scene.context);
}

static void TestGridRises(void)
{
    // A box asleep on a host table 2 m above a heightfield, 0.5 m aside
    // from the cells a point raised 2.5 m changes: it wakes, as the
    // heights may rise to it; measured with the height too, its gap was
    // 2.6 m and it slept on.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.collision.hostColliders = true;
    Play(&boxes);
    for (int32_t i = 0; i < 81; ++i)
    {
        s_heights[i] = 0.0f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 9;
    def.size[2] = 9;
    def.values = s_heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_scene.context, &def, &grid) == mvfx_success, "heights");
    const mvfxCollider colliders[2] = {{.kind = mvfx_colliderHeightfield,
                                        .halfExtents = {4, 0, 4},
                                        .rotation = {0, 0, 0, 1},
                                        .grid = grid},
                                       {.kind = mvfx_colliderBox,
                                        .center = {-3.5f, 1.5f, 0},
                                        .halfExtents = {1, 0.5f, 1},
                                        .rotation = {0, 0, 0, 1}}};
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, colliders, 2) ==
              mvfx_success,
          "field and table");
    static const double place[3] = {-3.5, 2.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Asleep(0);
    Dig(grid, 2, 4, 1, -2.5f);
    Steps(1);
    CHECK(slept && !Asleep(0), "heights rising to a sleeper wake it");
    mvfxDestroyContext(s_scene.context);
}

static void TestDepthWakes(void)
{
    // A box asleep on the ground by an empty depth image: the image set
    // again wakes it (its pixels may have changed).
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.collision.depthImage = true;
    boxes.collision.depthThickness = 0.5f;
    boxes.collision.depthSamples = 4;
    Play(&boxes);
    const float a = 100.0f / (0.1f - 100.0f);
    const mvfxDepthImage image = {
        .pixels = s_far,
        .byteCount = (int64_t)sizeof s_far,
        .width = 4,
        .height = 4,
        .stride = 16,
        .format = mvfx_depthFloat32,
        .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, a, -1, 0, 0, a * 0.1f, 0},
        .origin = {0, 0, 10},
        .rotation = {0, 0, 0, 1},
    };
    CHECK(mvfxInstance_SetDepthImage(s_scene.context, s_scene.instance, &image) == mvfx_success,
          "depth");
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Stats().asleep == 1;
    CHECK(mvfxInstance_SetDepthImage(s_scene.context, s_scene.instance, &image) == mvfx_success,
          "depth again");
    Steps(1);
    CHECK(slept && Stats().asleep == 0, "a depth image set again wakes a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestSpinning(void)
{
    // A box spinning in place at 1 radian a second with no gravity or
    // ground: its center still, its corners not, it never sleeps.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.motion.gravity[1] = 0.0f;
    boxes.collision.enabled = false;
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 1, 0}, .max = {0, 1, 0}};
    Play(&boxes);
    static const double place[3] = {0, 2, 0};
    Spawn(place, 1);
    Steps(120);
    CHECK(Stats().asleep == 0 && fabsf(BodyAt(0)->angular[1] - 1.0f) <= 1e-3f,
          "a spinning box stays awake");
    mvfxDestroyContext(s_scene.context);
}

static void TestWorldWakes(void)
{
    // A box asleep on the host's table; the same table set again leaves it
    // asleep, the table set 0.3 lower wakes it, and it sleeps on it again.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.collision.hostColliders = true;
    Play(&boxes);
    mvfxCollider table = {.kind = mvfx_colliderBox,
                          .center = {0, -0.5f, 0},
                          .halfExtents = {2, 0.5f, 2},
                          .rotation = {0, 0, 0, 1}};
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &table, 1) == mvfx_success,
          "table");
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Stats().asleep == 1;
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &table, 1) == mvfx_success,
          "table again");
    Steps(1);
    bool kept = Stats().asleep == 1;
    table.center[1] = -0.8f;
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &table, 1) == mvfx_success,
          "table lowered");
    Steps(1);
    bool woke = Stats().asleep == 0;
    Steps(90);
    CHECK(slept && kept && woke && Stats().asleep == 1 && fabsf(BodyAt(0)->pose[1] - 0.2f) <= 0.01f,
          "a moved table wakes a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestWakeWithin(void)
{
    // Two boxes asleep 6 apart: a sphere about one wakes it alone, an
    // infinite one the other; arguments out of range refused.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    Play(&boxes);
    static const double places[6] = {-3, 0.5, 0, 3, 0.5, 0};
    Spawn(places, 2);
    Steps(60);
    bool slept = Stats().asleep == 2;
    static const float near[3] = {-3.5f, 0.5f, 0};
    CHECK(mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, 0.1f) == mvfx_success,
          "near one");
    bool one = Stats().asleep == 1;
    Steps(1);
    one = one && !Asleep(0) && Asleep(1);
    CHECK(mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, INFINITY) ==
              mvfx_success,
          "all");
    bool all = Stats().asleep == 0;
    const float nowhere[3] = {NAN, 0, 0};
    bool refused =
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, nowhere, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, -1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, NAN) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 1, near, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, NULL, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(NULL, s_scene.instance, 0, near, 1.0f) == mvfx_errorInvalid;
    CHECK(slept && one && all && refused, "woken within a sphere");
    mvfxDestroyContext(s_scene.context);
}

static void TestNeverSleeps(void)
{
    // No sleep speed: a box at rest never sleeps.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.rigid.sleepSpeed = 0.0f;
    boxes.rigid.sleepTime = 0.0f;
    Play(&boxes);
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(120);
    CHECK(Stats().asleep == 0, "no sleep speed, no sleeping");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    // The default's 0.05 and 0.5; a sleep speed below 0 or NaN, or one
    // above 0 with no sleep time or past the most, refused.
    OpenContext();
    mvfxEmitterDef def = mvfxDefaultEmitterDef();
    CHECK(def.rigid.sleepSpeed == 0.05f && def.rigid.sleepTime == 0.5f, "defaults");
    static const float speeds[5] = {-0.1f, NAN, 0.05f, 0.05f, 0.05f};
    static const float times[5] = {0.5f, 0.5f, 0.0f, 4000.0f, NAN};
    int32_t refused = 0;
    for (int32_t i = 0; i < 5; ++i)
    {
        mvfxEmitterDef boxes = Boxes();
        boxes.rigid.sleepSpeed = speeds[i];
        boxes.rigid.sleepTime = times[i];
        mvfxEffectId effect = {0};
        refused += EffectOf(&boxes, &effect) == mvfx_errorInvalid ? 1 : 0;
    }
    mvfxEmitterDef off = Boxes();
    off.rigid.sleepSpeed = 0.0f;
    off.rigid.sleepTime = 0.0f;
    mvfxEffectId effect = {0};
    CHECK(refused == 5 && EffectOf(&off, &effect) == mvfx_success, "the checks");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestFallsAsleep();
    TestPileWakes();
    TestForcesWake();
    TestStillResets();
    TestSleeperMeets();
    TestSpinning();
    TestWorldWakes();
    TestFarPileSleeps();
    TestWorldNear();
    TestGridChanges();
    TestGridRegions();
    TestGridRises();
    TestDepthWakes();
    TestWakeWithin();
    TestNeverSleeps();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
