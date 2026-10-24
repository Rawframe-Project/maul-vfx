// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Velocity modules: setting the motion (a scale of 0 and an added
// velocity), scaling it, an added speed over a curve of age, orbit exact
// on its circle however the steps fall, radial velocity, the speed limit
// alike over one step or many, the modules under a turned, scaled
// instance, and the refusals.

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
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

// An emitter of what the host spawns, living ten seconds (past every
// test's run, so no test reads a particle whose life ended).
static mvfxEmitterDef Held(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    return emitter;
}

static void Place(Scene* scene, double x, double y, double z, float vx, float vy, float vz)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, vy, vz};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
}

static void Run(Scene* scene, int32_t steps, int64_t total)
{
    for (int32_t i = 0; i < steps; ++i)
    {
        CHECK(mvfxContext_StepTicks(scene->context, total / steps) == mvfx_success, "step");
    }
}

// The first particle.
static TestParticle First(const Scene* scene)
{
    TestParticle particle = {0};
    CHECK(ReadParticles(scene->context, scene->instance, 0, &particle, 1) >= 1, "the particle");
    return particle;
}

static bool Near(float a, double b, double tolerance)
{
    return fabs((double)a - b) <= tolerance;
}

static void TestSetAndScale(void)
{
    // A scale of 0 and 2 a second along +Y: the particle's own velocity
    // and gravity move it not at all, so it is where the added velocity
    // puts it; its velocity still changes under gravity.
    mvfxEmitterDef emitter = Held();
    emitter.motion.gravity[0] = -9.75f;
    emitter.velocityModules.motionScale.value = 0.0f;
    emitter.velocityModules.addedDirection[1] = 5.0f;
    emitter.velocityModules.addedSpeed.value = 2.0f;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 10, 0, 0);
    Run(&scene, 8, MVFX_TICKS_PER_SECOND / 2);
    CHECK(Near(First(&scene).position[0], 0.0, 1e-6) && Near(First(&scene).position[1], 1.0, 1e-5),
          "set: moved by the added velocity alone");
    CHECK(Near(First(&scene).velocity[0], 10.0 - 9.75 * 0.5, 1e-4),
          "its velocity still integrated");
    mvfxDestroyContext(scene.context);

    // A scale of 0.5: half its motion.
    emitter = Held();
    emitter.velocityModules.motionScale.value = 0.5f;
    scene = NewScene(&emitter);
    Place(&scene, 1, 0, 0, 4, 0, 0);
    Run(&scene, 4, MVFX_TICKS_PER_SECOND / 2);
    CHECK(Near(First(&scene).position[0], 2.0, 1e-5), "half its motion");
    mvfxDestroyContext(scene.context);

    // An added speed rising linearly over the life, 0 to 3: over the whole
    // life it carries 1.5, to first order in the step.
    static const mvfxCurveKey rising[2] = {
        {.time = 0.0f, .value = 0.0f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear}};
    emitter = Held();
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.velocityModules.addedDirection[2] = 1.0f;
    emitter.velocityModules.addedSpeed =
        (mvfxLifeCurve){.value = 3.0f, .curve = {.keys = rising, .keyCount = 2}};
    scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    Run(&scene, 1024, MVFX_TICKS_PER_SECOND);
    // After half its life: the speed at age a is 3 a, a = t / 2, so the
    // distance is 3 t^2 / 4 = 0.75, less half a step's share.
    CHECK(Near(First(&scene).position[2], 0.75, 2e-3), "an added speed over age");
    mvfxDestroyContext(scene.context);
}

static void TestOrbitAndRadial(void)
{
    // Half a turn a second about Z through (1, 0, 0), from (3, 0, 0) at
    // rest: at (-1, 0, 0) after a second, on its circle the whole way,
    // whatever the steps.
    for (int32_t steps = 1; steps <= 64; steps *= 64)
    {
        mvfxEmitterDef emitter = Held();
        emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
        emitter.velocityModules.orbitAxis[2] = 2.0f;
        emitter.velocityModules.orbitCenter[0] = 1.0f;
        emitter.velocityModules.orbitRate.value = 3.14159265f;
        Scene scene = NewScene(&emitter);
        Place(&scene, 3, 0, 0, 0, 0, 0);
        Run(&scene, steps, MVFX_TICKS_PER_SECOND);
        float x = First(&scene).position[0];
        float y = First(&scene).position[1];
        CHECK(Near(x, -1.0, 2e-5) && Near(y, 0.0, 2e-5), "half a turn");
        mvfxDestroyContext(scene.context);
    }

    // Radial: 3 a second away from the origin, and none at it.
    mvfxEmitterDef emitter = Held();
    emitter.velocityModules.radialSpeed.value = 3.0f;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, 0, 0);
    Run(&scene, 10, MVFX_TICKS_PER_SECOND / 2);
    CHECK(Near(First(&scene).position[1], 2.5, 1e-4), "away from the center");
    mvfxDestroyContext(scene.context);
    scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    Run(&scene, 2, MVFX_TICKS_PER_SECOND / 4);
    CHECK(First(&scene).position[0] == 0.0f && First(&scene).position[1] == 0.0f,
          "none at the center");
    mvfxDestroyContext(scene.context);
}

static void TestSpeedLimit(void)
{
    // From 10, a limit of 4 with dampen 2: the speed 4 + 6 e^(-2 t) over one
    // step or sixteen; under the limit, untouched.
    for (int32_t steps = 1; steps <= 16; steps *= 16)
    {
        mvfxEmitterDef emitter = Held();
        emitter.velocityModules.limitSpeed = true;
        emitter.velocityModules.speedLimit.value = 4.0f;
        emitter.velocityModules.limitDampen = 2.0f;
        Scene scene = NewScene(&emitter);
        Place(&scene, 0, 0, 0, 6, 8, 0);
        Run(&scene, steps, MVFX_TICKS_PER_SECOND / 2);
        double speed = 4.0 + 6.0 * exp(-1.0);
        CHECK(Near(First(&scene).velocity[0], 0.6 * speed, 1e-5) &&
                  Near(First(&scene).velocity[1], 0.8 * speed, 1e-5),
              "the excess decays, alike over the steps");
        mvfxDestroyContext(scene.context);
    }
    mvfxEmitterDef emitter = Held();
    emitter.velocityModules.limitSpeed = true;
    emitter.velocityModules.speedLimit.value = 4.0f;
    emitter.velocityModules.limitDampen = 2.0f;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 3, 0, 0);
    Run(&scene, 4, MVFX_TICKS_PER_SECOND / 2);
    CHECK(First(&scene).velocity[0] == 3.0f, "under the limit, untouched");
    mvfxDestroyContext(scene.context);
}

static void TestUnderAnInstance(void)
{
    // A world emitter under an instance at (100, 0, 0), a quarter turn
    // about Z, scale 2: the added velocity along the emitter's X goes along
    // the world's Y at twice its speed; the orbit's center at the
    // emitter's (1, 0, 0) is the world's (100, 2, 0).
    mvfxEmitterDef emitter = Held();
    emitter.velocityModules.addedDirection[0] = 1.0f;
    emitter.velocityModules.addedSpeed.value = 1.5f;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 100, 0, 0, 0, 0, 0);
    Run(&scene, 2, MVFX_TICKS_PER_SECOND / 2);
    CHECK(Near(First(&scene).position[0], 100.0, 1e-5) &&
              Near(First(&scene).position[1], 1.5, 1e-5),
          "added along the turned, scaled axis");
    mvfxDestroyContext(scene.context);

    emitter = Held();
    emitter.velocityModules.orbitAxis[2] = 1.0f;
    emitter.velocityModules.orbitCenter[0] = 1.0f;
    emitter.velocityModules.orbitRate.value = 3.14159265f;
    scene = NewScene(&emitter);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 100, 3, 0, 0, 0, 0);
    Run(&scene, 8, MVFX_TICKS_PER_SECOND);
    // From (100, 3, 0) about the center (100, 2, 0), half a turn: (100, 1,
    // 0).
    CHECK(Near(First(&scene).position[0], 100.0, 2e-4) &&
              Near(First(&scene).position[1], 1.0, 2e-4),
          "about the mapped center");
    mvfxDestroyContext(scene.context);
}

static void TestMoreUnderAnInstance(void)
{
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    // A speed limit of 2 in the emitter's space is 4 under scale 2.
    mvfxEmitterDef emitter = Held();
    emitter.velocityModules.limitSpeed = true;
    emitter.velocityModules.speedLimit.value = 2.0f;
    emitter.velocityModules.limitDampen = MVFX_MAX_DRAG;
    Scene scene = NewScene(&emitter);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 100, 0, 0, 10, 0, 0);
    Run(&scene, 1, MVFX_TICKS_PER_SECOND / 4);
    CHECK(Near(First(&scene).velocity[0], 4.0, 1e-4), "the limit scaled");
    mvfxDestroyContext(scene.context);

    // Radial 1 a second from the emitter's (0, 1, 0), the world's
    // (98, 0, 0) under the turn and scale, so 2 a second there: from
    // (99, 0, 0) along +X.
    emitter = Held();
    emitter.velocityModules.radialCenter[1] = 1.0f;
    emitter.velocityModules.radialSpeed.value = 1.0f;
    scene = NewScene(&emitter);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 99, 0, 0, 0, 0, 0);
    Run(&scene, 2, MVFX_TICKS_PER_SECOND);
    CHECK(Near(First(&scene).position[0], 101.0, 1e-4) &&
              Near(First(&scene).position[1], 0.0, 1e-4),
          "radial from the mapped center, scaled");
    mvfxDestroyContext(scene.context);

    // An orbit about the emitter's X, the world's Y: a quarter turn from
    // (101, 0, 1) about the line through (100, 0, 0) along Y.
    emitter = Held();
    emitter.velocityModules.orbitAxis[0] = 1.0f;
    emitter.velocityModules.orbitRate.value = 1.5707963f;
    scene = NewScene(&emitter);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 100, 0, 1, 0, 0, 0);
    Run(&scene, 4, MVFX_TICKS_PER_SECOND);
    CHECK(Near(First(&scene).position[0], 101.0, 1e-4) &&
              Near(First(&scene).position[2], 0.0, 1e-4),
          "about the mapped axis");
    mvfxDestroyContext(scene.context);

    // A motion scale of value 1 over a curve falling from 1 to 0: by its
    // curve, half the motion over the life.
    static const mvfxCurveKey falling[2] = {
        {.time = 0.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 0.0f, .interpolation = mvfx_interpolationLinear}};
    emitter = Held();
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.velocityModules.motionScale =
        (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = falling, .keyCount = 2}};
    scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 4, 0, 0);
    Run(&scene, 1024, MVFX_TICKS_PER_SECOND);
    // Over the first second the scale falls 1 to 0.5: 4 times its mean.
    CHECK(Near(First(&scene).position[0], 3.0, 5e-3), "a motion scale by its curve");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const mvfxCurveKey late[2] = {{.time = 0.0f, .value = 1.0f},
                                         {.time = 2.0f, .value = 1.0f}};
    for (int32_t field = 0; field < 8; ++field)
    {
        mvfxEmitterDef bad = Held();
        mvfxVelocityModules* m = &bad.velocityModules;
        switch (field)
        {
        case 0:
            m->motionScale.value = (float)NAN;
            break;
        case 1:
            m->addedSpeed = (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = late, .keyCount = 2}};
            m->addedDirection[0] = 1.0f;
            break;
        case 2:
            m->addedSpeed.value = 1.0f;
            break;
        case 3:
            m->orbitRate.value = 1.0f;
            break;
        case 4:
            m->limitSpeed = true;
            m->speedLimit.value = 1.0f;
            break;
        case 5:
            m->limitSpeed = true;
            m->speedLimit.value = 1.0f;
            m->limitDampen = 2.0f * MVFX_MAX_DRAG;
            break;
        case 6:
            m->limitSpeed = true;
            m->speedLimit.value = -1.0f;
            m->limitDampen = 1.0f;
            break;
        default:
            m->radialCenter[1] = (float)INFINITY;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &bad;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "refused");
    }
    mvfxDestroyContext(context);
}

int main(void)
{
    TestSetAndScale();
    TestOrbitAndRadial();
    TestSpeedLimit();
    TestUnderAnInstance();
    TestMoreUnderAnInstance();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
