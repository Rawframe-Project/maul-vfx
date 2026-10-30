// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Units' attachments: pins held in a moving, turning, scaling instance's
// space with their velocities; on the host's frames (followed, held when
// unset at birth or after, through a world rebase); on the particles that
// spawned them, turning with their orientation and let go when they die;
// many sources found by their serials across the 32-bit wrap; the checks
// and the host's calls' misuse.

#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 64

typedef struct Record
{
    float position[3];
    float velocity[3];
    float orientation[4];
    uint32_t serial;
} Record;

static const mvfxLayoutField s_fields[4] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    {.field = mvfx_recordOrientation, .format = mvfx_formatFloat32, .components = 4, .offset = 24},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 40},
};

static const mvfxView s_view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
static const float s_still[4] = {0, 0, 0, 1};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
    Record records[MOST];
    int64_t count;
} Scene;

static Scene s_scene;

// A world chain of three down from its root, its first point pinned,
// held as kind says; room for four, the host's spawns two.
static mvfxEmitterDef Chain(mvfxAttachKind kind)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 12;
    emitter.hostSpawnCapacity = 2;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                 .points = 3,
                                 .spacing = 0.5f,
                                 .direction = {0, -1, 0},
                                 .substeps = 4,
                                 .damping = 1.0f,
                                 .pinFirst = true,
                                 .attach = kind,
                                 .attachFrameCapacity = 2};
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
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 4;
    layoutDef.stride = (uint32_t)sizeof(Record);
    CHECK(mvfxCreateLayout(s_scene.context, &layoutDef, &s_scene.layout) == mvfx_success, "layout");
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

static bool Near(const float* a, double x, double y, double z, double within)
{
    return fabs((double)a[0] - x) <= within && fabs((double)a[1] - y) <= within &&
           fabs((double)a[2] - z) <= within;
}

static void Move(const double origin[3], const float rotation[4], float scale, bool teleport)
{
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, origin, rotation, scale,
                                    teleport) == mvfx_success,
          "moved");
}

static void TestInstance(void)
{
    // A root born at (1, 0, 0) of an instance at the origin: moved to
    // (5, 0, 0), turned a quarter about y and scaled 2 over a step, it is at
    // (5, 0, -2), its velocity its move over the last substep.
    mvfxEmitterDef emitter = Chain(mvfx_attachInstance);
    Open(&emitter, 1);
    static const double root[3] = {1, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = root};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Steps(1);
    static const double there[3] = {5, 0, 0};
    const float quarter[4] = {0, sinf(0.785398163f), 0, cosf(0.785398163f)};
    Move(there, quarter, 2.0f, false);
    Steps(1);
    Read(0);
    CHECK(Near(s_scene.records[0].position, 5, 0, -2, 1e-4), "held in the instance's space");
    CHECK(s_scene.records[0].velocity[0] > 30.0f && s_scene.records[0].velocity[2] < -30.0f,
          "a pin's velocity");
    // At rest again, the pin rests.
    Steps(2);
    Read(0);
    CHECK(Near(s_scene.records[0].position, 5, 0, -2, 1e-4) &&
              Near(s_scene.records[0].velocity, 0, 0, 0, 1e-3),
          "at rest");
    mvfxDestroyContext(s_scene.context);
}

static void TestHost(void)
{
    // Two roots at (1, 0, 0) on frame 0 at the origin and (-1, 0, 0) on
    // frame 1, unset at birth. Frame 0 moved to (3, 0, 0) and turned a
    // quarter about z: its root is at (3, 1, 0); the other is held.
    mvfxEmitterDef emitter = Chain(mvfx_attachHost);
    Open(&emitter, 1);
    mvfxAttachFrame frames[2] = {{.position = {0, 0, 0}, .rotation = {0, 0, 0, 1}},
                                 {.position = {100, 0, 0}, .rotation = {0, 0, 0, 1}}};
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, frames, 1) ==
              mvfx_success,
          "one frame");
    static const double roots[6] = {1, 0, 0, -1, 0, 0};
    static const int32_t on[2] = {0, 1};
    mvfxSpawnRequest request = {.count = 2, .positions = roots, .attachFrames = on};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Steps(1);
    frames[0] = (mvfxAttachFrame){.position = {3, 0, 0},
                                  .rotation = {0, 0, sinf(0.785398163f), cosf(0.785398163f)}};
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, frames, 2) ==
              mvfx_success,
          "two frames");
    Steps(1);
    Read(0);
    CHECK(Near(s_scene.records[0].position, 3, 1, 0, 1e-4), "on the host's frame");
    CHECK(s_scene.records[0].velocity[0] > 30.0f && s_scene.records[0].velocity[1] > 10.0f,
          "its velocity");
    CHECK(Near(s_scene.records[3].position, -1, 0, 0, 0), "unset at birth: held");
    // Unset, the first is held where it was, through a world rebase.
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, NULL, 0) ==
              mvfx_success,
          "no frames");
    static const double far[3] = {5000, 0, 0};
    Move(far, s_still, 1.0f, true);
    Steps(3);
    Read(0);
    CHECK(Near(s_scene.records[0].position, 3, 1, 0, 1e-3) &&
              Near(s_scene.records[3].position, -1, 0, 0, 1e-3),
          "held through a rebase");
    // Set again after the rebase: followed in the world.
    frames[0].position[0] = 4.0;
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, frames, 1) ==
              mvfx_success,
          "again");
    Steps(1);
    Read(0);
    CHECK(Near(s_scene.records[0].position, 4, 1, 0, 1e-3), "followed after a rebase");
    mvfxDestroyContext(s_scene.context);
}

// Turns v by a unit quaternion q.
static void Turn(const float turn[4], const double v[3], double out[3])
{
    const double q[4] = {(double)turn[0], (double)turn[1], (double)turn[2], (double)turn[3]};
    double t[3] = {2.0 * (q[1] * v[2] - q[2] * v[1]), 2.0 * (q[2] * v[0] - q[0] * v[2]),
                   2.0 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

static void TestParticles(void)
{
    // A source particle flying along x and spinning about y spawns a chain
    // whose root follows it, its offset turning with it; when the source
    // dies, the chain is let go and falls.
    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), Chain(mvfx_attachParticles)};
    emitters[0].rate = 0.0f;
    emitters[0].capacity = 2;
    emitters[0].hostSpawnCapacity = 1;
    emitters[0].lifetime = mvfxRangeSource(1.0f, 1.0f);
    emitters[0].events = mvfx_eventMaskBirth;
    emitters[0].eventCapacity = 4;
    emitters[0].angularVelocity = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0, 2.0f, 0, 0}, .max = {0, 2.0f, 0, 0}};
    emitters[1].eventSpawn = (mvfxEventSpawn){.enable = true,
                                              .source = 0,
                                              .kind = mvfx_eventBirth,
                                              .minCount = 1,
                                              .maxCount = 1,
                                              .capacity = 4};
    Open(emitters, 2);
    static const double start[3] = {0, 0, 0};
    static const float flying[3] = {2, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = start, .velocities = flying};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Steps(1);
    Read(0);
    Record source = s_scene.records[0];
    Read(1);
    CHECK(s_scene.count == 3, "a chain spawned");
    double offset[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        offset[k] = (double)s_scene.records[0].position[k] - (double)source.position[k];
    }
    const float* q0 = source.orientation;
    const float back[4] = {-q0[0], -q0[1], -q0[2], q0[3]};
    double local[3];
    Turn(back, offset, local);
    Steps(30);
    Read(0);
    source = s_scene.records[0];
    Read(1);
    double turned[3];
    Turn(source.orientation, local, turned);
    CHECK(fabs(offset[0]) > 1e-3 &&
              Near(s_scene.records[0].position, (double)source.position[0] + turned[0],
                   (double)source.position[1] + turned[1], (double)source.position[2] + turned[2],
                   1e-4),
          "on its source, turning with it");
    float last = s_scene.records[0].position[1];
    Steps(60);
    Read(0);
    CHECK(s_scene.count == 0, "the source gone");
    Read(1);
    CHECK(s_scene.count == 3 && s_scene.records[0].position[1] < last - 0.5f, "let go, falling");
    mvfxDestroyContext(s_scene.context);
}

static void TestShorterWay(void)
{
    // A frame turned a quarter about z, then given as the same turn's
    // negative: nothing moves, the pin's velocity 0.
    mvfxEmitterDef emitter = Chain(mvfx_attachHost);
    emitter.motion.gravity[1] = 0.0f;
    float half = sinf(0.785398163f);
    mvfxAttachFrame frame = {.position = {0, 0, 0}, .rotation = {0, 0, half, half}};
    Open(&emitter, 1);
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, &frame, 1) ==
              mvfx_success,
          "a frame");
    static const double root[3] = {1, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = root};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Steps(2);
    Read(0);
    Record before[3] = {s_scene.records[0], s_scene.records[1], s_scene.records[2]};
    frame.rotation[2] = -half;
    frame.rotation[3] = -half;
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, &frame, 1) ==
              mvfx_success,
          "its negative");
    Steps(1);
    Read(0);
    bool still = Near(s_scene.records[0].velocity, 0, 0, 0, 1e-4);
    for (int32_t i = 0; i < 3; ++i)
    {
        still = still && Near(s_scene.records[i].position, (double)before[i].position[0],
                              (double)before[i].position[1], (double)before[i].position[2], 1e-5);
    }
    CHECK(still, "the shorter way");
    mvfxDestroyContext(s_scene.context);
}

static void TestHostLocal(void)
{
    // A local emitter's root at (1, 0, 0) on a frame at the origin: the
    // instance turned a quarter about z, the frame a quarter about y, the
    // root is at (0, 0, -1) in the world. The frame unset and the
    // instance moved to (0, 5, 0), it is held in the instance's space, at
    // (0, 5, -1).
    mvfxEmitterDef emitter = Chain(mvfx_attachHost);
    emitter.space = mvfx_spaceLocal;
    emitter.motion.gravity[1] = 0.0f;
    Open(&emitter, 1);
    mvfxAttachFrame frame = {.position = {0, 0, 0}, .rotation = {0, 0, 0, 1}};
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, &frame, 1) ==
              mvfx_success,
          "a frame");
    static const double root[3] = {1, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = root};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Steps(1);
    float half = sinf(0.785398163f);
    const float aboutZ[4] = {0, 0, half, half};
    static const double origin[3] = {0, 0, 0};
    Move(origin, aboutZ, 1.0f, true);
    frame.rotation[1] = half;
    frame.rotation[3] = half;
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, &frame, 1) ==
              mvfx_success,
          "turned");
    Steps(1);
    Read(0);
    CHECK(Near(s_scene.records[0].position, 0, 0, -1, 1e-4), "a local emitter's frame");
    CHECK(mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, 0, NULL, 0) ==
              mvfx_success,
          "unset");
    static const double up[3] = {0, 5, 0};
    Move(up, aboutZ, 1.0f, true);
    Steps(2);
    Read(0);
    CHECK(Near(s_scene.records[0].position, 0, 5, -1, 1e-4), "held in the instance's space");
    mvfxDestroyContext(s_scene.context);
}

// Kills every particle after motion.
static void KillAll(const mvfxModuleCall* call, void* user)
{
    (void)user;
    for (int64_t i = 0; i < call->particles.count; ++i)
    {
        call->particles.kill[i] = 1;
    }
}

static void TestSources(void)
{
    // Sources born at a rate and in bursts, their serials crossing 2^32,
    // flying apart, some dying: each live one's chain is by it, the others'
    // let go. A unit the host spawns is born of no event and stays where it
    // is. A source killed in its birth's step lets its chain go at once.
    mvfxEmitterDef emitters[3] = {mvfxDefaultEmitterDef(), Chain(mvfx_attachParticles),
                                  mvfxDefaultEmitterDef()};
    emitters[0].rate = 20.0f;
    static const mvfxBurst burst = {
        .time = 0.1f, .minCount = 3, .maxCount = 3, .cycles = 1, .probability = 1.0f};
    emitters[0].bursts = &burst;
    emitters[0].burstCount = 1;
    emitters[0].capacity = 16;
    emitters[0].lifetime = mvfxRangeSource(0.3f, 1.2f);
    emitters[0].shape.kind = mvfx_shapeSphere;
    emitters[0].shape.radius = 0.1f;
    emitters[0].velocity.speed = (mvfxRange){3.0f, 3.0f};
    emitters[0].events = mvfx_eventMaskBirth;
    emitters[0].eventCapacity = 16;
    static const mvfxEventSpawn fromZero = {.enable = true,
                                            .source = 0,
                                            .kind = mvfx_eventBirth,
                                            .minCount = 1,
                                            .maxCount = 1,
                                            .capacity = 16};
    emitters[1].eventSpawn = fromZero;
    emitters[1].capacity = 3 * 16;
    emitters[1].motion.gravity[1] = -20.0f;
    static const mvfxHostModule killer = {.stage = mvfx_stageAfterMotion, .function = KillAll};
    emitters[2].rate = 0.0f;
    emitters[2].hostSpawnCapacity = 1;
    emitters[2].lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitters[2].events = mvfx_eventMaskBirth;
    emitters[2].eventCapacity = 1;
    emitters[2].hostModules = &killer;
    emitters[2].hostModuleCount = 1;
    mvfxEmitterDef chains[4] = {emitters[0], emitters[1], emitters[2], Chain(mvfx_attachParticles)};
    chains[3].eventSpawn = fromZero;
    chains[3].eventSpawn.source = 2;
    chains[3].eventSpawn.capacity = 1;
    Open(chains, 4);
    CHECK(mvfxTestSetOwed(s_scene.context, s_scene.instance, 0, 0xFFFFFFFAu), "near the wrap");
    static const double root[3] = {0, 50, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = root};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 1, &request) == mvfx_success &&
              mvfxInstance_Spawn(s_scene.context, s_scene.instance, 2, &request) == mvfx_success,
          "spawned");
    Steps(36);
    Read(0);
    static Record sources[MOST];
    int64_t sourceCount = s_scene.count;
    memcpy(sources, s_scene.records, sizeof sources);
    bool wrapped =
        sourceCount >= 4 && sources[0].serial > 0xF0000000u && sources[sourceCount - 1].serial < 16;
    Read(1);
    int64_t units = s_scene.count / 3;
    int64_t near = 0;
    for (int64_t u = 1; u < units; ++u)
    {
        double best = 1e9;
        for (int64_t i = 0; i < sourceCount; ++i)
        {
            double d = 0.0;
            for (int32_t k = 0; k < 3; ++k)
            {
                double e =
                    (double)s_scene.records[3 * u].position[k] - (double)sources[i].position[k];
                d += e * e;
            }
            best = d < best ? d : best;
        }
        near += sqrt(best) <= 0.06 ? 1 : 0;
    }
    CHECK(wrapped, "serials across the wrap");
    CHECK(units > sourceCount + 1 && near == sourceCount, "each live source's chain by it");
    CHECK(Near(s_scene.records[0].position, 0, 50, 0, 0), "a host's unit stays");
    Read(3);
    CHECK(s_scene.count == 3 && s_scene.records[0].position[1] < 49.0f, "let go at once");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    emitters[0].events = mvfx_eventMaskBirth;
    emitters[0].eventCapacity = 4;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 2;
    mvfxEffectId effect = {0};
    static const mvfxEventSpawn spawn = {.enable = true,
                                         .source = 0,
                                         .kind = mvfx_eventBirth,
                                         .minCount = 1,
                                         .maxCount = 1,
                                         .capacity = 4};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 8; ++rule)
    {
        emitters[1] = Chain(mvfx_attachHost);
        mvfxUnitDef* u = &emitters[1].unit;
        switch (rule)
        {
        case 0:
            u->attach = 4;
            break;
        case 1:
            u->attach = mvfx_attachInstance;
            emitters[1].space = mvfx_spaceLocal;
            break;
        case 2:
            u->pinFirst = false;
            break;
        case 3:
            u->attachFrameCapacity = 0;
            break;
        case 4:
            u->attachFrameCapacity = MVFX_MAX_ATTACH_FRAMES + 1;
            break;
        case 5:
            u->attach = mvfx_attachParticles;
            break;
        case 6:
            u->attach = mvfx_attachParticles;
            emitters[1].eventSpawn = spawn;
            emitters[0].space = mvfx_spaceLocal;
            break;
        default:
            // A sheet's pinned row counts as a pin, a chain's does not.
            u->pinFirst = false;
            u->pinFirstRow = true;
            break;
        }
        // Past MVFX_MAX_ATTACH_FRAMES is past a named bound.
        mvfxResult want = rule == 4 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect, NULL) == want;
        emitters[0].space = mvfx_spaceWorld;
    }
    CHECK(refused == 8, "every check");
    emitters[1] = Chain(mvfx_attachParticles);
    emitters[1].eventSpawn = spawn;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "particles");
    emitters[1] = Chain(mvfx_attachHost);
    emitters[1].unit.attachFrameCapacity = MVFX_MAX_ATTACH_FRAMES;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "the most frames");
    emitters[1] = Chain(mvfx_attachInstance);
    emitters[1].unit.attachFrameCapacity = -3;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "capacity unread");
    emitters[1] = Chain(mvfx_attachHost);
    emitters[1].unit.kind = mvfx_unitSheet;
    emitters[1].unit.rows = 2;
    emitters[1].unit.downDirection[2] = 1.0f;
    emitters[1].unit.pinFirst = false;
    emitters[1].unit.pinFirstRow = true;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "a sheet's row");
    mvfxDestroyContext(context);
}

static void TestMisuse(void)
{
    mvfxEmitterDef emitters[2] = {Chain(mvfx_attachHost), Chain(mvfx_attachNone)};
    Open(emitters, 2);
    mvfxAttachFrame frame = {.position = {0, 0, 0}, .rotation = {0, 0, 0, 1}};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 8; ++rule)
    {
        int32_t emitter = rule == 0 ? 1 : 0;
        int32_t count = rule == 1 ? 3 : rule == 2 ? -1 : 1;
        const mvfxAttachFrame* frames = rule == 3 ? NULL : &frame;
        frame = (mvfxAttachFrame){.position = {0, 0, 0}, .rotation = {0, 0, 0, 1}};
        frame.rotation[3] = rule == 4 ? 0.9f : 1.0f;
        frame.position[1] = rule == 5 ? (double)NAN : rule == 6 ? 0x1p53 : 0.0;
        emitter = rule == 7 ? 2 : emitter;
        refused += mvfxInstance_SetAttachFrames(s_scene.context, s_scene.instance, emitter, frames,
                                                count) == mvfx_errorInvalid;
    }
    CHECK(refused == 8, "frames' misuse");
    CHECK(mvfxInstance_SetAttachFrames(NULL, s_scene.instance, 0, &frame, 1) == mvfx_errorInvalid,
          "no context");
    static const double root[3] = {0, 0, 0};
    static const int32_t frames[3] = {2, -1, 0};
    refused = 0;
    for (int32_t rule = 0; rule < 3; ++rule)
    {
        mvfxSpawnRequest request = {.count = 1, .positions = root, .attachFrames = &frames[rule]};
        refused += mvfxInstance_Spawn(s_scene.context, s_scene.instance, rule == 2 ? 1 : 0,
                                      &request) == mvfx_errorInvalid;
    }
    CHECK(refused == 3, "spawns' misuse");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestInstance();
    TestHost();
    TestParticles();
    TestHostLocal();
    TestShorterWay();
    TestSources();
    TestChecks();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
