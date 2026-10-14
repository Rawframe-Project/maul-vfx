// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The integrator: its factors against the closed form on both sides of
// the series, particles under gravity and drag where the closed form puts
// them however the time is split, births within a step moved from their
// ticks, gravity in either space, a drag multiplier per particle and its
// hold, spin and angular velocity under angular drag, and the refusals.

#include "context.h"
#include "instance.h"
#include "integrate.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxInstance* live;
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
    instanceDef.seed = 3;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

static const float* Array(const Scene* scene, mvfxAttribute attribute)
{
    return mvfxAttributeArray(scene->live, 0, attribute);
}

static double AgeOf(const Scene* scene, int32_t i)
{
    const uint32_t* low = mvfxAttributeArray(scene->live, 0, mvfx_attributeBirthLow);
    return (double)(scene->live->clock - (int64_t)low[i]) / (double)MVFX_TICKS_PER_SECOND;
}

// The closed form of one axis: from rest at x0 0 with velocity v0, after t
// under acceleration a and drag k.
static void ClosedForm(double v0, double a, double k, double t, double* x, double* v)
{
    if (k == 0.0)
    {
        *x = v0 * t + 0.5 * a * t * t;
        *v = v0 + a * t;
        return;
    }
    double e = exp(-k * t);
    *v = v0 * e + a * (1.0 - e) / k;
    *x = v0 * (1.0 - e) / k + a * (t - (1.0 - e) / k) / k;
}

// Whether a float result is within a tolerance of a reference, relative
// above 1 and absolute below.
static bool Near(float a, double b, double tolerance)
{
    return fabs((double)a - b) <= tolerance * (1.0 + fabs(b));
}

static void TestFactors(void)
{
    // On both sides of the series and at the ends of the range.
    static const double drags[] = {0.0, 1e-9, 0.1, 0.2499, 0.25, 0.2501, 1.0, 7.5, 1000.0};
    static const double spans[] = {1.0 / 1048576.0, 0.01, 0.25, 1.0};
    bool close = true;
    for (int32_t d = 0; d < 9; ++d)
    {
        for (int32_t h = 0; h < 4; ++h)
        {
            double k = drags[d];
            double t = spans[h];
            mvfxDragFactors f = mvfxDragFactorsOf(k, t);
            double e = exp(-k * t);
            double carry = k == 0.0 ? t : (1.0 - e) / k;
            double push = k == 0.0 ? 0.5 * t * t : (t - carry) / k;
            // Near k t = 0 the closed form cancels; its series is the
            // reference there.
            if (k * t < 1e-3)
            {
                double x = k * t;
                carry = t * (1.0 - x / 2.0 + x * x / 6.0);
                push = t * t * (0.5 - x / 6.0 + x * x / 24.0);
            }
            close = close && Near(f.decay, e, 3e-7) && Near(f.carry, carry, 3e-7) &&
                    Near(f.push, push, 3e-7);
        }
    }
    CHECK(close, "the factors of the closed form, to a float's rounding");
    mvfxDragFactors none = mvfxDragFactorsOf(0.0, 0.5);
    CHECK(none.decay == 1.0f && none.carry == 0.5f && none.push == 0.125f, "no drag, exactly");
}

// An emitter of one burst of four at the start, thrown along X at 10 and
// up at 5, under gravity -9.75 on Y and drag 0.7.
static mvfxEmitterDef Thrown(void)
{
    static const mvfxBurst burst = {
        .time = 0.0f, .minCount = 4, .maxCount = 4, .cycles = 1, .probability = 1.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.velocity = (mvfxVelocity){
        .mode = mvfx_directionFixed, .direction = {2, 1, 0}, .speed = {11.18034f, 11.18034f}};
    emitter.motion.gravity[1] = -9.75f;
    emitter.motion.drag = 0.7f;
    return emitter;
}

static bool MatchesClosedForm(const Scene* scene, double k, double tolerance)
{
    bool match = scene->live->emitters[0].count > 0;
    for (int32_t i = 0; i < scene->live->emitters[0].count; ++i)
    {
        double t = AgeOf(scene, i);
        double x = 0.0;
        double vx = 0.0;
        double y = 0.0;
        double vy = 0.0;
        ClosedForm(10.0, 0.0, k, t, &x, &vx);
        ClosedForm(5.0, -9.75, k, t, &y, &vy);
        match = match && Near(Array(scene, mvfx_attributePositionX)[i], x, tolerance) &&
                Near(Array(scene, mvfx_attributePositionY)[i], y, tolerance) &&
                Near(Array(scene, mvfx_attributeVelocityX)[i], vx, tolerance) &&
                Near(Array(scene, mvfx_attributeVelocityY)[i], vy, tolerance);
    }
    return match;
}

static void TestExactWhateverTheSplit(void)
{
    // Two seconds as one step, as eight, and as uneven ones: each where
    // the closed form puts it.
    static const int64_t splits[3][5] = {
        {1048576, 1048576, 0, 0, 0},
        {262144, 262144, 262144, 262144, 1048576},
        {1, 524287, 99999, 948577, 524288},
    };
    mvfxEmitterDef emitter = Thrown();
    for (int32_t s = 0; s < 3; ++s)
    {
        Scene scene = NewScene(&emitter);
        int64_t total = 0;
        for (int32_t i = 0; i < 5 && total < 2 * MVFX_TICKS_PER_SECOND; ++i)
        {
            int64_t ticks = splits[s][i];
            if (ticks == 0)
            {
                continue;
            }
            if (s == 1 && i == 4)
            {
                // Four quarters, then four more.
                for (int32_t q = 0; q < 4; ++q)
                {
                    CHECK(mvfxContext_StepTicks(scene.context, 262144) == mvfx_success, "step");
                }
                total += 1048576;
                continue;
            }
            CHECK(mvfxContext_StepTicks(scene.context, ticks) == mvfx_success, "step");
            total += ticks;
        }
        CHECK(scene.live->clock == 2 * MVFX_TICKS_PER_SECOND, "two seconds");
        CHECK(MatchesClosedForm(&scene, 0.7, 2e-5), "where the closed form puts them");
        mvfxDestroyContext(scene.context);
    }
}

static void TestNewborns(void)
{
    // A rate of 40 over one step of a second: each birth moved from its own
    // tick, so each is where the closed form puts a particle of its age.
    mvfxEmitterDef emitter = Thrown();
    emitter.burstCount = 0;
    emitter.rate = 40.0f;
    Scene scene = NewScene(&emitter);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(scene.live->emitters[0].count == 40, "forty born");
    CHECK(MatchesClosedForm(&scene, 0.7, 2e-5), "each moved from its birth");
    CHECK(AgeOf(&scene, 39) == 0.0 && Array(&scene, mvfx_attributePositionX)[39] == 0.0f,
          "the last born at the step's end, not moved");
    mvfxDestroyContext(scene.context);
}

static void TestGravitySpaces(void)
{
    // An instance turned a quarter about Z at scale 2. World gravity in a
    // world emitter: as given. Emitter gravity in a world emitter: turned
    // and scaled. World gravity in a local emitter: turned back and
    // unscaled.
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    static const double origin[3] = {0.0, 0.0, 0.0};
    static const float expected[3][2] = {{0.0f, -4.0f}, {8.0f, 0.0f}, {-2.0f, 0.0f}};
    for (int32_t c = 0; c < 3; ++c)
    {
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.rate = 4.0f;
        emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
        emitter.motion.gravity[1] = -4.0f;
        emitter.motion.gravitySpace = c == 1 ? mvfx_spaceLocal : mvfx_spaceWorld;
        emitter.space = c == 2 ? mvfx_spaceLocal : mvfx_spaceWorld;
        Scene scene = NewScene(&emitter);
        CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f,
                                        true) == mvfx_success,
              "turned");
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        bool accelerated = scene.live->emitters[0].count == 4;
        for (int32_t i = 0; i < scene.live->emitters[0].count; ++i)
        {
            float t = (float)AgeOf(&scene, i);
            accelerated =
                accelerated &&
                fabsf(Array(&scene, mvfx_attributeVelocityX)[i] - expected[c][0] * t) <= 1e-5f &&
                fabsf(Array(&scene, mvfx_attributeVelocityY)[i] - expected[c][1] * t) <= 1e-5f;
        }
        CHECK(accelerated, "gravity in its space");
        mvfxDestroyContext(scene.context);
    }
}

static void TestDragPerParticle(void)
{
    // Drag 1 times a multiplier from 0 to 2: each particle slows by its own.
    mvfxEmitterDef emitter = Thrown();
    emitter.motion.gravity[1] = 0.0f;
    emitter.motion.drag = 1.0f;
    emitter.motion.dragScale = mvfxRangeSource(0.0f, 2.0f);
    emitter.bursts = &(mvfxBurst){
        .time = 0.0f, .minCount = 64, .maxCount = 64, .cycles = 1, .probability = 1.0f};
    Scene scene = NewScene(&emitter);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    const float* scales = Array(&scene, mvfx_attributeDrag);
    bool own = scene.live->emitters[0].count == 64;
    bool spread = false;
    for (int32_t i = 0; i < scene.live->emitters[0].count; ++i)
    {
        double x = 0.0;
        double v = 0.0;
        ClosedForm(10.0, 0.0, (double)scales[i], AgeOf(&scene, i), &x, &v);
        own = own && Near(Array(&scene, mvfx_attributeVelocityX)[i], v, 2e-5) &&
              Near(Array(&scene, mvfx_attributePositionX)[i], x, 2e-5) && scales[i] >= 0.0f &&
              scales[i] <= 2.0f;
        spread = spread || fabsf(scales[i] - scales[0]) > 0.5f;
    }
    CHECK(own && spread, "each by its own drag");
    mvfxDestroyContext(scene.context);

    // Held: a negative multiplier is no drag, and a large one MVFX_MAX_DRAG.
    for (int32_t c = 0; c < 2; ++c)
    {
        emitter.motion.drag = c == 0 ? 1.0f : MVFX_MAX_DRAG;
        emitter.motion.dragScale =
            c == 0 ? mvfxRangeSource(-3.0f, -3.0f) : mvfxRangeSource(5.0f, 5.0f);
        scene = NewScene(&emitter);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 1024) == mvfx_success,
              "step");
        double x = 0.0;
        double v = 0.0;
        ClosedForm(10.0, 0.0, c == 0 ? 0.0 : (double)MVFX_MAX_DRAG, AgeOf(&scene, 0), &x, &v);
        CHECK(Near(Array(&scene, mvfx_attributeVelocityX)[0], v, 2e-5),
              c == 0 ? "a negative multiplier, no drag" : "held to the most drag");
        mvfxDestroyContext(scene.context);
    }
}

static void TestRotation(void)
{
    // Spin 3 under angular drag 0.5, and an angular velocity of 2 about Z
    // without the spin's drag: the angle and the quarter turn follow the
    // closed form over many steps.
    for (int32_t c = 0; c < 2; ++c)
    {
        mvfxEmitterDef emitter = Thrown();
        emitter.motion = (mvfxMotion){.angularDrag = c == 0 ? 0.5f : 0.0f};
        if (c == 0)
        {
            emitter.spin = mvfxRangeSource(3.0f, 3.0f);
        }
        else
        {
            emitter.angularVelocity = (mvfxVectorSource){
                .kind = mvfx_sourceRange, .min = {0, 0, 2, 0}, .max = {0, 0, 2, 0}};
        }
        Scene scene = NewScene(&emitter);
        for (int32_t i = 0; i < 37; ++i)
        {
            CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 37) == mvfx_success,
                  "step");
        }
        double t = AgeOf(&scene, 0);
        if (c == 0)
        {
            double e = exp(-0.5 * t);
            CHECK(Near(Array(&scene, mvfx_attributeSpin)[0], 3.0 * e, 1e-5) &&
                      Near(Array(&scene, mvfx_attributeAngle)[0], 3.0 * (1.0 - e) / 0.5, 1e-5),
                  "spin and angle under angular drag");
        }
        else
        {
            const float* qz = Array(&scene, mvfx_attributeOrientationZ);
            const float* qw = Array(&scene, mvfx_attributeOrientationW);
            const float* qx = Array(&scene, mvfx_attributeOrientationX);
            CHECK(fabs((double)qz[0] - sin(t)) <= 1e-5 && fabs((double)qw[0] - cos(t)) <= 1e-5 &&
                      qx[0] == 0.0f,
                  "turned 2 t radians about Z");
            CHECK(Array(&scene, mvfx_attributeAngularZ)[0] == 2.0f, "no drag, no slowing");
        }
        mvfxDestroyContext(scene.context);
    }
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 7; ++field)
    {
        mvfxEmitterDef bad = mvfxDefaultEmitterDef();
        switch (field)
        {
        case 0:
            bad.motion.gravity[2] = (float)NAN;
            break;
        case 1:
            bad.motion.gravity[0] = 2.0f * MVFX_MAX_EXTENT;
            break;
        case 2:
            bad.motion.gravitySpace = 2;
            break;
        case 3:
            bad.motion.drag = -0.5f;
            break;
        case 4:
            bad.motion.drag = 2.0f * MVFX_MAX_DRAG;
            break;
        case 5:
            bad.motion.angularDrag = 2.0f * MVFX_MAX_DRAG;
            break;
        default:
            bad.motion.dragScale = mvfxRangeSource(2.0f, 1.0f);
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
    TestFactors();
    TestExactWhateverTheSplit();
    TestNewborns();
    TestGravitySpaces();
    TestDragPerParticle();
    TestRotation();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
