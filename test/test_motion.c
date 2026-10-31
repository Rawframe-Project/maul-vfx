// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Moving instances: world and local space, turns and scales, teleports,
// births by distance along the way (alike however the move is split),
// the cap on a jump, inherited velocity, particles far from the world's
// origin keeping their places and precision, restart, and the refusals.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 11;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

static const float s_identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};

static void MoveTo(const Scene* scene, double x, double y, double z, bool teleport)
{
    const double origin[3] = {x, y, z};
    CHECK(mvfxInstance_SetTransform(scene->context, scene->instance, origin, s_identity, 1.0f,
                                    teleport) == mvfx_success,
          "moved");
}

static TestParticle s_particles[256];

// Reads the particles into s_particles, their places less origin (the
// world's when NULL); returns how many there are.
static int32_t ReadFrom(const Scene* scene, const double* origin)
{
    return ReadParticlesFrom(scene->context, scene->instance, 0, origin, s_particles, 256);
}

static int32_t Read(const Scene* scene)
{
    return ReadFrom(scene, NULL);
}

static mvfxEmitterStats Stats(const Scene* scene)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, 0, &stats) == mvfx_success,
          "stats");
    return stats;
}

static void TestWorldAndLocal(void)
{
    // 64 a second over a step moving 16 units along X: in world space each
    // birth is where the instance was at its tick (x = 16 times its share of
    // the step), and stays there; in local space each is at its origin,
    // with the instance at 16 at the step's end and carried with it.
    for (int32_t space = 0; space < 2; ++space)
    {
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.space = (mvfxSimulationSpace)space;
        emitter.lifetime = mvfxMakeRangeSource(5.0f, 5.0f);
        Scene scene = NewScene(&emitter);
        MoveTo(&scene, 16.0, 0.0, 0.0, false);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        int32_t count = Read(&scene);
        bool placed = count == 64;
        for (int32_t k = 0; k < count; ++k)
        {
            float expected = space == mvfx_spaceWorld ? 16.0f * (float)(k + 1) / 64.0f : 16.0f;
            placed = placed && fabsf(s_particles[k].position[0] - expected) <= 1e-4f;
        }
        CHECK(placed, space == mvfx_spaceWorld ? "born along the way" : "born at the origin");
        float first = s_particles[0].position[0];
        MoveTo(&scene, 40.0, 0.0, 0.0, false);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success,
              "step");
        CHECK(Read(&scene) == 65 &&
                  s_particles[0].position[0] == (space == mvfx_spaceWorld ? first : 40.0f),
              space == mvfx_spaceWorld ? "a particle stays put" : "a particle carried along");
        mvfxDestroyContext(scene.context);
    }
}

static void TestTurnAndScale(void)
{
    // A quarter turn about Z at scale 2: a shape offset of (1, 0, 0) is born
    // at (0, 2, 0) from the instance, a fixed velocity (1, 0, 0) goes (0, 2,
    // 0).
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.shape.offset[0] = 1.0f;
    emitter.events = mvfx_eventMaskBirth;
    emitter.eventCapacity = 64;
    emitter.velocity =
        (mvfxVelocity){.mode = mvfx_directionFixed, .direction = {1, 0, 0}, .speed = {1.0f, 1.0f}};
    Scene scene = NewScene(&emitter);
    const double origin[3] = {5.0, 0.0, 0.0};
    const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "teleported");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    const mvfxEvent* births = NULL;
    int32_t birthCount = 0;
    CHECK(mvfxInstance_GetEvents(scene.context, scene.instance, 0, &births, &birthCount) ==
              mvfx_success,
          "births");
    bool turned = Read(&scene) == 16 && birthCount == 16;
    for (int32_t k = 0; turned && k < 16; ++k)
    {
        const TestParticle* q = &s_particles[k];
        // Where each was born: its place less its flight since.
        CHECK(births[k].serial == q->serial, "its birth");
        float age =
            (float)(MVFX_TICKS_PER_SECOND / 4 - births[k].tick) / (float)MVFX_TICKS_PER_SECOND;
        float y = q->position[1] - q->velocity[1] * age;
        turned = fabsf(q->position[0] - 5.0f) <= 1e-5f && fabsf(y - 2.0f) <= 1e-5f &&
                 fabsf(q->velocity[0]) <= 1e-5f && fabsf(q->velocity[1] - 2.0f) <= 1e-5f;
    }
    CHECK(turned, "turned and scaled; a teleport moves at once");
    mvfxDestroyContext(scene.context);

    // Over a step: from unturned to a quarter turn given as its negative
    // (the same turn), the shorter way, and from scale 1 to 3. The birth
    // half way is at an eighth of a turn, at scale 2.
    emitter.rate = 2.0f;
    emitter.velocity.speed = (mvfxRange){0.0f, 0.0f};
    scene = NewScene(&emitter);
    const double home[3] = {0.0, 0.0, 0.0};
    const float negative[4] = {0.0f, 0.0f, -0.70710678f, -0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, home, negative, 3.0f, false) ==
              mvfx_success,
          "moved");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&scene) == 2, "two born");
    float x = s_particles[0].position[0];
    float y = s_particles[0].position[1];
    CHECK(fabsf(x - 2.0f * 0.70710678f) <= 1e-4f && fabsf(y - 2.0f * 0.70710678f) <= 1e-4f,
          "half way: an eighth of a turn at scale 2");
    mvfxDestroyContext(scene.context);
}

static void TestDistance(void)
{
    // Two a unit over ten units: twenty births, birth k where the travel
    // reaches (k + 1) / 2, and the same however the move is split.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.ratePerDistance = 2.0f;
    emitter.lifetime = mvfxMakeRangeSource(10.0f, 10.0f);
    Scene whole = NewScene(&emitter);
    Scene split = NewScene(&emitter);
    MoveTo(&whole, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    MoveTo(&split, 3.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(split.context, 314573) == mvfx_success, "step");
    MoveTo(&split, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(split.context, MVFX_TICKS_PER_SECOND - 314573) == mvfx_success,
          "step");
    TestParticle splitParticles[20];
    bool along = ReadParticles(split.context, split.instance, 0, splitParticles, 20) == 20 &&
                 Read(&whole) == 20;
    for (int32_t k = 0; k < 20 && along; ++k)
    {
        float a = s_particles[k].position[0];
        float b = splitParticles[k].position[0];
        along = fabsf(a - 0.5f * (float)(k + 1)) <= 1e-4f && fabsf(a - b) <= 1e-4f;
    }
    CHECK(along, "born along the way, alike when the move is split");
    MoveTo(&whole, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&whole) == 20, "standing still spawns nothing");
    MoveTo(&whole, 100.0, 0.0, 0.0, true);
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&whole) == 20, "a teleport spawns nothing");
    mvfxDestroyContext(whole.context);
    mvfxDestroyContext(split.context);

    // Fractions carry from step to step: five moves of 0.4 at one a unit
    // owe two births, at 1 and 2 units.
    emitter.ratePerDistance = 1.0f;
    Scene carried = NewScene(&emitter);
    for (int32_t i = 1; i <= 5; ++i)
    {
        MoveTo(&carried, 0.4 * i, 0.0, 0.0, false);
        CHECK(mvfxContext_StepTicks(carried.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success,
              "step");
    }
    CHECK(Read(&carried) == 2 && fabsf(s_particles[0].position[0] - 1.0f) <= 1e-4f &&
              fabsf(s_particles[1].position[0] - 2.0f) <= 1e-4f,
          "fractions carried across steps");
    mvfxDestroyContext(carried.context);
    emitter.ratePerDistance = 2.0f;

    // Moving during a delay spawns nothing; after it, the rest of the way.
    emitter.startDelay = 0.5f;
    Scene delayed = NewScene(&emitter);
    MoveTo(&delayed, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(delayed.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(delayed.context, delayed.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    CHECK(stats.owed == 10 && Read(&delayed) == 10 && s_particles[0].position[0] > 5.0f,
          "only the second half of the way");
    mvfxDestroyContext(delayed.context);

    // A jump past the capacity: the earliest births refused, the last eight
    // born near the end.
    emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.ratePerDistance = 1.0f;
    emitter.lifetime = mvfxMakeRangeSource(10.0f, 10.0f);
    Scene jump = NewScene(&emitter);
    MoveTo(&jump, 100.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(jump.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    CHECK(mvfxInstance_GetEmitterStats(jump.context, jump.instance, 0, &stats) == mvfx_success,
          "stats");
    CHECK(stats.owed == 100 && stats.birthsDropped == 92 && stats.live == 8, "the excess refused");
    CHECK(Read(&jump) == 8 && fabsf(s_particles[0].position[0] - 93.0f) <= 1e-3f,
          "the last eight born");
    mvfxDestroyContext(jump.context);
}

static void TestHugeJumps(void)
{
    // Across the whole range a host may give, at the highest distance rate:
    // the step takes the capacity and refuses the rest at once, its counts
    // held, and the steps after go on as usual. With a delay over the
    // jump, nothing is born and nothing hangs.
    for (int32_t delayed = 0; delayed < 2; ++delayed)
    {
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.rate = 0.0f;
        emitter.capacity = 8;
        emitter.ratePerDistance = MVFX_MAX_DISTANCE_RATE;
        emitter.lifetime = mvfxMakeRangeSource(10.0f, 10.0f);
        emitter.startDelay = delayed != 0 ? 5.0f : 0.0f;
        Scene scene = NewScene(&emitter);
        MoveTo(&scene, -0x1p52, -0x1p52, -0x1p52, true);
        MoveTo(&scene, 0x1p52, 0x1p52, 0x1p52, false);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success,
              "step");
        mvfxEmitterStats stats;
        CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) ==
                  mvfx_success,
              "stats");
        CHECK(stats.live == (delayed != 0 ? 0 : 8) && stats.birthsDropped >= ((uint64_t)1 << 62u) &&
                  stats.birthsDropped <= stats.owed,
              "the capacity taken, the rest refused");
        uint64_t owed = stats.owed;
        // Back across: a second jump's refusals would wrap the counts.
        MoveTo(&scene, -0x1p52, -0x1p52, -0x1p52, false);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success,
              "step");
        CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) ==
                  mvfx_success,
              "stats");
        CHECK(stats.owed >= owed, "counts never wrap");
        mvfxDestroyContext(scene.context);
    }
}

static void TestInheritedVelocity(void)
{
    // Half the instance's velocity: 4 units in half a second is 8 a second,
    // so each particle sets off at 4.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.inheritVelocity = 0.5f;
    Scene scene = NewScene(&emitter);
    MoveTo(&scene, 0.0, 4.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    int32_t count = Read(&scene);
    bool inherited = count == 32;
    for (int32_t k = 0; k < count; ++k)
    {
        inherited =
            inherited && s_particles[k].velocity[1] == 4.0f && s_particles[k].velocity[0] == 0.0f;
    }
    CHECK(inherited, "half the instance's velocity");
    MoveTo(&scene, 0.0, 100.0, 0.0, true);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    CHECK(Read(&scene) == 33 && s_particles[32].velocity[1] == 0.0f, "a teleport has no velocity");
    mvfxDestroyContext(scene.context);
}

// Keeps the anchor a step's host modules see.
static void SeeAnchor(const mvfxModuleCall* call, void* user)
{
    double* anchor = user;
    for (int32_t k = 0; k < 3; ++k)
    {
        anchor[k] = call->anchor[k];
    }
}

static void TestRebase(void)
{
    // Far along X in steps: the anchor follows past the rebase distance,
    // every particle keeps its place in the world, to binary32's precision
    // near it, and new births are where the instance is.
    static double anchor[3];
    const mvfxHostModule seeing = {
        .stage = mvfx_stageAfterMotion, .function = SeeAnchor, .user = anchor};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.lifetime = mvfxMakeRangeSource(100.0f, 100.0f);
    emitter.capacity = 4096;
    emitter.hostModules = &seeing;
    emitter.hostModuleCount = 1;
    Scene scene = NewScene(&emitter);
    static const double near[3] = {600.0, 0.0, 0.0};
    static const double far[3] = {1100.0, 0.0, 0.0};
    float places[64];
    MoveTo(&scene, 600.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(anchor[0] == 0.0, "not yet rebased");
    CHECK(ReadFrom(&scene, near) == 64, "a second's births");
    for (int32_t k = 0; k < 64; ++k)
    {
        places[k] = s_particles[k].position[0];
    }
    MoveTo(&scene, 1100.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(anchor[0] == 1100.0, "rebased to the instance");
    bool kept = ReadFrom(&scene, near) == 128;
    for (int32_t k = 0; k < 64; ++k)
    {
        kept = kept && fabsf(s_particles[k].position[0] - places[k]) <= 1e-4f;
    }
    CHECK(kept, "every particle keeps its place");
    CHECK(ReadFrom(&scene, far) == 128 && fabsf(s_particles[127].position[0]) <= 1e-3f,
          "new births at the instance");

    // A restart plays from where the instance is: a birth there.
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    CHECK(anchor[0] == 1100.0, "a restart anchors where the instance is");
    CHECK(ReadFrom(&scene, far) == 1 && fabsf(s_particles[0].position[0]) <= 1e-4f,
          "a restart where the instance is");
    mvfxDestroyContext(scene.context);

    // A restart carries no fraction of the way before it: half a unit,
    // a restart, then three quarters owe nothing.
    emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.ratePerDistance = 1.0f;
    emitter.lifetime = mvfxMakeRangeSource(10.0f, 10.0f);
    scene = NewScene(&emitter);
    MoveTo(&scene, 0.5, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    CHECK(Stats(&scene).owed == 0, "no travel standing still");
    MoveTo(&scene, 1.25, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    CHECK(Stats(&scene).owed == 0, "no carry from before it");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    Scene scene = NewScene(&emitter);
    uint64_t misuse = mvfxContext_GetMisuse(scene.context);
    const double origin[3] = {0.0, 0.0, 0.0};
    const double far[3] = {0.0, 0x1p53, 0.0};
    const float half[4] = {0.0f, 0.0f, 0.0f, 0.5f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, far, s_identity, 1.0f, false) ==
              mvfx_errorInvalid,
          "too far");
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, half, 1.0f, false) ==
              mvfx_errorInvalid,
          "not a unit quaternion");
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, s_identity, 0.0f,
                                    false) == mvfx_errorLimit,
          "scale 0, below MVFX_MIN_SCALE");
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, NULL, s_identity, 1.0f, false) ==
              mvfx_errorInvalid,
          "no origin");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 4, "each counted");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_SetTransform(scene.context, stale, origin, s_identity, 1.0f, false) ==
              mvfx_errorStale,
          "stale");
    CHECK(mvfxInstance_SetTransform(NULL, scene.instance, origin, s_identity, 1.0f, false) ==
              mvfx_errorInvalid,
          "no context");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = (mvfxEffectId){1, 1};
    def.scale = 2e6f;
    mvfxInstanceId none = {0};
    uint64_t counted = mvfxContext_GetMisuse(scene.context);
    CHECK(mvfxCreateInstance(scene.context, &def, &none) == mvfx_errorLimit &&
              mvfxContext_GetMisuse(scene.context) == counted,
          "a def's scale past MVFX_MAX_SCALE, no misuse");
    def.scale = 1.0f;
    def.rotation[3] = 0.9f;
    CHECK(mvfxCreateInstance(scene.context, &def, &none) == mvfx_errorInvalid, "a def's rotation");
    mvfxDestroyContext(scene.context);

    // Emitter fields: a local emitter cannot inherit; ranges.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 5; ++field)
    {
        // Past a named bound: mvfx_errorLimit, data a correct program may
        // pass, so no misuse.
        mvfxResult want = field == 3 ? mvfx_errorLimit : mvfx_errorInvalid;
        mvfxEmitterDef bad = mvfxDefaultEmitterDef();
        switch (field)
        {
        case 0:
            bad.space = mvfx_spaceLocal;
            bad.inheritVelocity = 1.0f;
            break;
        case 1:
            bad.space = 2;
            break;
        case 2:
            bad.ratePerDistance = -1.0f;
            break;
        case 3:
            bad.ratePerDistance = MVFX_MAX_DISTANCE_RATE * 2.0f;
            break;
        default:
            bad.inheritVelocity = 11.0f;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &bad;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == want, "refused");
    }
    mvfxDestroyContext(context);
}

int main(void)
{
    TestWorldAndLocal();
    TestTurnAndScale();
    TestDistance();
    TestHugeJumps();
    TestInheritedVelocity();
    TestRebase();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
