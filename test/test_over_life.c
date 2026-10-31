// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Values over life, taken where they are read: colour times a gradient
// over age or speed and alpha over age, white without a stored colour,
// size uniform, per axis and by speed, the flipbook over life and at a
// rate, looping or held, from a start frame, custom attributes by a
// curve, the stored values never written, rotation speed over life (which
// a step applies), and the refusals. Read from the particles' records;
// the frames' exact products at ages a test cannot reach by stepping are
// in test_over_life_internal.c.

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
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

// The scene's first particle's record.
static TestParticle Record(const Scene* scene)
{
    TestParticle particles[4];
    CHECK(ReadParticles(scene->context, scene->instance, 0, particles, 4) >= 1, "a particle");
    return particles[0];
}

// An emitter of what the host spawns, living two seconds.
static mvfxEmitterDef Held(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxMakeRangeSource(2.0f, 2.0f);
    return emitter;
}

// One particle at rest at the origin (or moving at speed along X), then
// the given seconds.
static Scene Run(const mvfxEmitterDef* emitter, float speed, int64_t ticks)
{
    Scene scene = NewScene(emitter);
    static const double origin[3] = {0, 0, 0};
    const float velocity[3] = {speed, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = origin, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success, "placed");
    // At most a second a step.
    for (int64_t left = ticks; left > 0; left -= MVFX_TICKS_PER_SECOND)
    {
        int64_t step = left < MVFX_TICKS_PER_SECOND ? left : MVFX_TICKS_PER_SECOND;
        CHECK(mvfxContext_StepTicks(scene.context, step) == mvfx_success, "step");
    }
    return scene;
}

static const mvfxColorKey s_redToBlue[2] = {{0.0f, 1, 0, 0}, {1.0f, 0, 0, 1}};
static const mvfxAlphaKey s_opaque[2] = {{0.0f, 1}, {1.0f, 0.5f}};
static const mvfxGradient s_gradient = {s_redToBlue, 2, s_opaque, 2};
static const mvfxCurveKey s_rising[2] = {
    {.time = 0.0f, .value = 0.0f, .interpolation = mvfx_interpolationLinear},
    {.time = 1.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear}};

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f;
}

static void TestColor(void)
{
    // A stored colour (0.5, 1, 1, 0.8) times the gradient at age 0.5
    // (half red, half blue, alpha 0.75), and alpha times 0.5 at that age
    // (a rising curve of value 1).
    mvfxEmitterDef emitter = Held();
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 1, 1, 0.8f}, .max = {0.5f, 1, 1, 0.8f}};
    emitter.overLife.color =
        (mvfxColorOverLife){.enable = true,
                            .gradient = s_gradient,
                            .scaleAlpha = true,
                            .alpha = {.value = 1.0f, .curve = {.keys = s_rising, .keyCount = 2}}};
    Scene scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND);
    TestParticle record = Record(&scene);
    const float* color = record.color;
    CHECK(Near(color[0], 0.25f) && Near(color[1], 0.0f) && Near(color[2], 0.5f) &&
              Near(color[3], 0.8f * 0.75f * 0.5f),
          "the colour by the gradient and alpha over age");
    // Half a second on, at age 0.75, the stored colour times the gradient
    // there (a quarter red, three quarters blue, alpha 0.625), not the
    // colour read before times it again.
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "on");
    record = Record(&scene);
    CHECK(Near(color[0], 0.125f) && Near(color[2], 0.75f) && Near(color[3], 0.8f * 0.625f * 0.75f),
          "the stored colour never written");
    mvfxDestroyContext(scene.context);

    // By speed between 0 and 10, under an instance at scale 2: a world
    // speed of 10 is 5 in the emitter's units, half way; no stored colour
    // is white.
    emitter = Held();
    emitter.overLife.color = (mvfxColorOverLife){.enable = true,
                                                 .input = mvfx_overSpeed,
                                                 .speedRange = {0.0f, 10.0f},
                                                 .gradient = s_gradient};
    scene = NewScene(&emitter);
    static const double origin[3] = {0, 0, 0};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin,
                                    (const float[4]){0, 0, 0, 1}, 2.0f, true) == mvfx_success,
          "scaled");
    static const float fast[3] = {10, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = origin, .velocities = fast};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success, "placed");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    record = Record(&scene);
    CHECK(Near(color[0], 0.5f) && Near(color[2], 0.5f) && Near(color[3], 0.75f),
          "by speed in the emitter's units, from white");
    mvfxDestroyContext(scene.context);

    // Not enabled: the stored value, or white.
    emitter = Held();
    scene = Run(&emitter, 0.0f, 1);
    record = Record(&scene);
    CHECK(color[0] == 1.0f && color[3] == 1.0f, "white without one");
    mvfxDestroyContext(scene.context);
}

static void TestSize(void)
{
    // A stored size (1, 2, 3); uniform: twice at any age; per axis: the
    // rising curve on X at age 0.5, 3 on Y, 1 on Z; by speed: at the top
    // of its range.
    mvfxEmitterDef emitter = Held();
    emitter.size =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 2, 3, 0}, .max = {1, 2, 3, 0}};
    emitter.overLife.size = (mvfxSizeOverLife){.enable = true, .size = {{.value = 2.0f}}};
    Scene scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND);
    TestParticle record = Record(&scene);
    const float* size = record.size;
    CHECK(size[0] == 2.0f && size[1] == 4.0f && size[2] == 6.0f, "uniform");
    mvfxDestroyContext(scene.context);
    emitter.overLife.size =
        (mvfxSizeOverLife){.enable = true,
                           .perAxis = true,
                           .size = {{.value = 1.0f, .curve = {.keys = s_rising, .keyCount = 2}},
                                    {.value = 3.0f},
                                    {.value = 1.0f}}};
    scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND);
    record = Record(&scene);
    CHECK(Near(size[0], 0.5f) && size[1] == 6.0f && size[2] == 3.0f, "per axis");
    mvfxDestroyContext(scene.context);
    emitter.overLife.size =
        (mvfxSizeOverLife){.enable = true,
                           .input = mvfx_overSpeed,
                           .speedRange = {1.0f, 3.0f},
                           .size = {{.value = 2.0f, .curve = {.keys = s_rising, .keyCount = 2}}}};
    scene = Run(&emitter, 7.0f, 1);
    record = Record(&scene);
    CHECK(Near(size[0], 2.0f) && Near(size[2], 6.0f), "by speed, held at its range's top");
    mvfxDestroyContext(scene.context);
    // Within the range: speed 2 between 1 and 3 is half way.
    scene = Run(&emitter, 2.0f, 1);
    record = Record(&scene);
    CHECK(Near(size[0], 1.0f) && Near(size[1], 2.0f), "by speed, its share of the range");
    mvfxDestroyContext(scene.context);
}

static void TestFrames(void)
{
    // Four frames, twice over a life of two seconds: at one second, four
    // frames on, so frame 0 looping and 3 held; from a start frame of 2,
    // frame 2 looping and 3 held.
    for (int32_t start = 0; start <= 2; start += 2)
    {
        for (int32_t loop = 0; loop < 2; ++loop)
        {
            mvfxEmitterDef emitter = Held();
            if (start != 0)
            {
                emitter.frame = (mvfxFrameStart){mvfx_frameFixed, 2, 2};
            }
            emitter.overLife.frame = (mvfxFrameOverLife){.animation = mvfx_frameOverLife,
                                                         .frameCount = 4,
                                                         .cycles = 2.0f,
                                                         .loop = loop != 0};
            Scene scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND + 1);
            uint32_t expected = loop != 0 ? (uint32_t)start : 3u;
            CHECK(Record(&scene).frame == expected, "over life");
            mvfxDestroyContext(scene.context);
        }
    }
    // Ten frames a second of eight: at 1.25 seconds, frame 12, so 4
    // looping and 7 held; still, the start frame.
    mvfxEmitterDef emitter = Held();
    emitter.overLife.frame = (mvfxFrameOverLife){
        .animation = mvfx_frameAtRate, .frameCount = 8, .frameRate = 10.0f, .loop = true};
    Scene scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND * 5 / 4);
    CHECK(Record(&scene).frame == 4u, "at a rate, looping");
    mvfxDestroyContext(scene.context);
    emitter.overLife.frame.loop = false;
    scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND * 5 / 4);
    CHECK(Record(&scene).frame == 7u, "at a rate, held");
    mvfxDestroyContext(scene.context);
    emitter = Held();
    emitter.frame = (mvfxFrameStart){mvfx_frameFixed, 5, 5};
    scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND);
    CHECK(Record(&scene).frame == 5u, "still");
    mvfxDestroyContext(scene.context);
}

static void TestCustomAndRotation(void)
{
    // A custom attribute (3, 4) scaled by twice the rising curve: (1.5, 2)
    // at age 0.25, (4.5, 6) at 0.75, the stored values kept.
    mvfxCustomAttribute custom = {
        .components = 2,
        .initial = {.kind = mvfx_sourceRange, .min = {3, 4, 0, 0}, .max = {3, 4, 0, 0}},
        .scaledOverLife = true,
        .overLife = {.value = 2.0f, .curve = {.keys = s_rising, .keyCount = 2}}};
    mvfxEmitterDef emitter = Held();
    emitter.custom = &custom;
    emitter.customCount = 1;
    // Moving, so nothing reads as 0 by chance.
    Scene scene = Run(&emitter, 1.0f, MVFX_TICKS_PER_SECOND / 2);
    TestParticle record = Record(&scene);
    const float* values = record.custom;
    CHECK(Near(values[0], 1.5f) && Near(values[1], 2.0f) && values[2] == 0.0f,
          "scaled by its curve");
    // A second on, at age 0.75: (3, 4) times 1.5, not the values read
    // before times it.
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "on");
    record = Record(&scene);
    CHECK(Near(values[0], 4.5f) && Near(values[1], 6.0f), "stored values kept");
    mvfxDestroyContext(scene.context);

    // Spin 2 at half its rate over life: half a radian over half a second;
    // an angular velocity of 2 about Z likewise, half a turn of a radian.
    emitter = Held();
    emitter.spin = mvfxMakeRangeSource(2.0f, 2.0f);
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, 2, 0}, .max = {0, 0, 2, 0}};
    emitter.overLife.rotation = (mvfxRotationOverLife){.enable = true, .scale = {.value = 0.5f}};
    scene = Run(&emitter, 0.0f, MVFX_TICKS_PER_SECOND / 2);
    record = Record(&scene);
    CHECK(Near(record.angle, 0.5f), "a sprite's turn scaled");
    CHECK(Near(record.orientation[2], sinf(0.25f)), "a mesh particle's turn scaled");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const mvfxCurveKey late[1] = {{.time = 2.0f, .value = 1.0f}};
    for (int32_t field = 0; field < 13; ++field)
    {
        mvfxEmitterDef bad = Held();
        mvfxOverLife* o = &bad.overLife;
        mvfxCustomAttribute custom = {.components = 1};
        switch (field)
        {
        case 0:
            o->color = (mvfxColorOverLife){
                .enable = true, .input = mvfx_overSpeed, .speedRange = {2.0f, 1.0f}};
            break;
        case 1:
            o->color = (mvfxColorOverLife){.enable = true, .input = 2};
            break;
        case 2:
            o->color = (mvfxColorOverLife){.enable = true, .gradient = {s_redToBlue, 2, NULL, 1}};
            break;
        case 3:
            o->color = (mvfxColorOverLife){
                .enable = true, .scaleAlpha = true, .alpha = {.value = (float)NAN}};
            break;
        case 4:
            o->size =
                (mvfxSizeOverLife){.enable = true,
                                   .perAxis = true,
                                   .size = {{.value = 1},
                                            {.value = 1},
                                            {.value = 1, .curve = {.keys = late, .keyCount = 1}}}};
            break;
        case 5:
            o->frame = (mvfxFrameOverLife){.animation = mvfx_frameOverLife, .cycles = 1.0f};
            break;
        case 6:
            o->frame = (mvfxFrameOverLife){
                .animation = mvfx_frameOverLife, .frameCount = 4, .cycles = 1001.0f};
            break;
        case 12:
            o->frame = (mvfxFrameOverLife){.animation = mvfx_frameOverLife, .frameCount = 4};
            break;
        case 7:
            o->frame = (mvfxFrameOverLife){
                .animation = mvfx_frameAtRate, .frameCount = 4, .frameRate = 2e4f};
            break;
        case 8:
            o->frame = (mvfxFrameOverLife){.animation = 3};
            break;
        case 9:
            o->rotation =
                (mvfxRotationOverLife){.enable = true, .scale = {.value = (float)INFINITY}};
            break;
        case 10:
            o->size = (mvfxSizeOverLife){.enable = true,
                                         .input = mvfx_overSpeed,
                                         .speedRange = {0.0f, (float)NAN},
                                         .size = {{.value = 1}}};
            break;
        case 11:
            custom.scaledOverLife = true;
            custom.overLife =
                (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = late, .keyCount = 1}};
            bad.custom = &custom;
            bad.customCount = 1;
            break;
        default:
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &bad;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_errorInvalid, "refused");
    }
    mvfxDestroyContext(context);
}

int main(void)
{
    TestColor();
    TestSize();
    TestFrames();
    TestCustomAndRotation();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
