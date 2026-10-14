// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Value sources and the initial values they draw: each kind against its
// formula, the age a source reads, and every optional value through the
// step (sizes, colors on the line between two, sprite and mesh rotations,
// frames, custom attributes, lives by curve and parameter) with the
// instance parameters that feed them.

#include "birth.h"
#include "context.h"
#include "instance.h"
#include "test_harness.h"
#include "value.h"

#include "maul-vfx/instance.h"

#include <math.h>

#define SAMPLES 20000

static mvfxWords4 Words(uint32_t i)
{
    return mvfxPcg4d((mvfxWords4){i, 0xFACEu, 1u, 0u});
}

static const mvfxCurveKey s_ramp[2] = {{0.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                       {1.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
static const mvfxColorKey s_redToBlue[2] = {{0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}};
static const mvfxAlphaKey s_opaque[1] = {{0.0f, 1.0f}};

static void TestScalarKinds(void)
{
    mvfxCurveTable curves[1];
    mvfxTableSpace space = {.curves = curves};
    static const float parameters[2][4] = {{3.0f, 0, 0, 0}, {-2.0f, 0, 0, 0}};

    mvfxScalarSource range = mvfxRangeSource(2.0f, 4.0f);
    CHECK(mvfxIsScalarSourceValid(&range, false, true, 0), "a range");
    mvfxCompiledScalar compiled;
    mvfxCompileScalar(&range, true, &space, &compiled);
    double sum = 0.0;
    bool within = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        float v = mvfxDrawScalar(&compiled, 0.0f, parameters, Words(i));
        within = within && v >= 2.0f && v <= 4.0f;
        sum += (double)v;
    }
    CHECK(within, "within the range");
    CHECK(fabs(sum / SAMPLES - 3.0) <= 5.0 * (2.0 / sqrt(12.0)) / sqrt((double)SAMPLES),
          "uniform over it");

    mvfxScalarSource curve = {
        .kind = mvfx_sourceCurve, .range = {2.0f, 2.0f}, .curve = {s_ramp, 2, mvfx_tangentsGiven}};
    CHECK(mvfxIsScalarSourceValid(&curve, false, true, 0), "a curve");
    mvfxCompileScalar(&curve, true, &space, &compiled);
    CHECK(fabsf(mvfxDrawScalar(&compiled, 0.25f, parameters, Words(1)) - 0.5f) <= 1e-6f,
          "the curve at the age times the range");

    mvfxScalarSource parameter = {
        .kind = mvfx_sourceParameter, .range = {1.0f, 1.0f}, .parameter = 1};
    CHECK(mvfxIsScalarSourceValid(&parameter, false, true, 2), "a parameter");
    CHECK(!mvfxIsScalarSourceValid(&parameter, false, true, 1), "a parameter past the count");
    mvfxCompileScalar(&parameter, true, &space, &compiled);
    CHECK(mvfxDrawScalar(&compiled, 0.0f, parameters, Words(2)) == -2.0f, "the parameter's x");

    mvfxScalarSource none = {0};
    CHECK(mvfxIsScalarSourceValid(&none, true, true, 0), "none when optional");
    CHECK(!mvfxIsScalarSourceValid(&none, false, true, 0), "none when required");
    mvfxScalarSource unbounded = curve;
    unbounded.curve.keys = (const mvfxCurveKey[1]){{2.5f, 1.0f, 0.0f, 0.0f, 0}};
    unbounded.curve.keyCount = 1;
    CHECK(!mvfxIsScalarSourceValid(&unbounded, false, true, 0), "a key past 1 with a duration");
    CHECK(mvfxIsScalarSourceValid(&unbounded, false, false, 0), "in seconds without one");
    // A curve in seconds, from 2 s to 4 s, read at 3 s: half way.
    static const mvfxCurveKey seconds[2] = {{2.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                            {4.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    mvfxScalarSource inSeconds = {
        .kind = mvfx_sourceCurve, .range = {1.0f, 1.0f}, .curve = {seconds, 2, mvfx_tangentsGiven}};
    mvfxTableSpace secondsSpace = {.curves = curves};
    mvfxCompileScalar(&inSeconds, false, &secondsSpace, &compiled);
    CHECK(fabsf(mvfxDrawScalar(&compiled, 3.0f, parameters, Words(9)) - 0.5f) <= 1e-6f,
          "a curve in seconds over its keys' span");
    static const mvfxScalarSource bad[4] = {
        {.kind = 6, .range = {0.0f, 1.0f}},
        {.kind = mvfx_sourceRange, .range = {2.0f, 1.0f}},
        {.kind = mvfx_sourceRange, .range = {(float)NAN, 1.0f}},
        {.kind = mvfx_sourceRange, .range = {0.0f, 2e30f}},
    };
    for (int32_t i = 0; i < 4; ++i)
    {
        CHECK(!mvfxIsScalarSourceValid(&bad[i], true, true, 0), "refused");
    }
}

static void TestVectorKinds(void)
{
    mvfxCurveTable curves[1];
    mvfxGradientTable gradients[2];
    mvfxTableSpace space = {.curves = curves, .gradients = gradients};
    static const float parameters[1][4] = {{1.0f, 2.0f, 3.0f, 4.0f}};

    // Shared draws: a color between red and blue lies on the line.
    mvfxVectorSource line = {
        .kind = mvfx_sourceRange, .min = {1, 0, 0, 1}, .max = {0, 0, 1, 1}, .uniform = true};
    CHECK(mvfxIsVectorSourceValid(&line, 4, true, 0), "a line of colors");
    mvfxCompiledVector compiled;
    mvfxCompileVector(&line, 4, true, &space, &compiled);
    bool onLine = true;
    for (uint32_t i = 0; i < 2000; ++i)
    {
        float v[4];
        mvfxDrawVector(&compiled, 0.0f, parameters, Words(i), v);
        onLine = onLine && fabsf(v[0] + v[2] - 1.0f) <= 1e-6f && v[1] == 0.0f && v[3] == 1.0f;
    }
    CHECK(onLine, "shared draws on the line between the two");

    // Separate draws: components independent.
    line.uniform = false;
    mvfxCompileVector(&line, 4, true, &space, &compiled);
    int64_t offLine = 0;
    for (uint32_t i = 0; i < 2000; ++i)
    {
        float v[4];
        mvfxDrawVector(&compiled, 0.0f, parameters, Words(i), v);
        offLine += fabsf(v[0] + v[2] - 1.0f) > 1e-3f ? 1 : 0;
    }
    CHECK(offLine > 1800, "separate draws leave the line");

    // A gradient at random from red to blue, scaled by 1: red plus blue 1.
    mvfxVectorSource gradient = {.kind = mvfx_sourceGradient,
                                 .min = {1, 1, 1, 1},
                                 .max = {1, 1, 1, 1},
                                 .gradient = {s_redToBlue, 2, s_opaque, 1}};
    CHECK(mvfxIsVectorSourceValid(&gradient, 4, true, 0), "a gradient");
    CHECK(!mvfxIsVectorSourceValid(&gradient, 3, true, 0), "a gradient needs four components");
    mvfxCompileVector(&gradient, 4, true, &space, &compiled);
    double sumRed = 0.0;
    bool along = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        float v[4];
        mvfxDrawVector(&compiled, 0.0f, parameters, Words(i), v);
        along = along && fabsf(v[0] + v[2] - 1.0f) <= 1e-5f && v[3] == 1.0f;
        sumRed += (double)v[0];
    }
    CHECK(along, "on the gradient");
    CHECK(fabs(sumRed / SAMPLES - 0.5) <= 5.0 * sqrt(1.0 / 12.0) / sqrt((double)SAMPLES),
          "at a uniform point");

    // Over age: the gradient at the age.
    gradient.kind = mvfx_sourceGradientOverAge;
    CHECK(!mvfxIsVectorSourceValid(&gradient, 4, false, 0), "over age needs a duration");
    mvfxCompileVector(&gradient, 4, true, &space, &compiled);
    float v[4];
    mvfxDrawVector(&compiled, 0.75f, parameters, Words(3), v);
    CHECK(fabsf(v[0] - 0.25f) <= 1e-5f && fabsf(v[2] - 0.75f) <= 1e-5f, "the gradient at age");

    // A curve scales every component; a parameter each by its own.
    mvfxVectorSource curve = {.kind = mvfx_sourceCurve,
                              .min = {1, 2, 3, 7},
                              .max = {1, 2, 3, 7},
                              .curve = {s_ramp, 2, mvfx_tangentsGiven}};
    mvfxCompileVector(&curve, 3, true, &space, &compiled);
    mvfxDrawVector(&compiled, 0.5f, parameters, Words(4), v);
    CHECK(fabsf(v[0] - 0.5f) <= 1e-6f && fabsf(v[1] - 1.0f) <= 1e-6f &&
              fabsf(v[2] - 1.5f) <= 1e-6f && v[3] == 0.0f,
          "a curve scales each component; unused ones 0");
    mvfxVectorSource parameter = {
        .kind = mvfx_sourceParameter, .min = {2, 2, 2, 2}, .max = {2, 2, 2, 2}};
    mvfxCompileVector(&parameter, 4, true, &space, &compiled);
    mvfxDrawVector(&compiled, 0.0f, parameters, Words(5), v);
    CHECK(v[0] == 2.0f && v[1] == 4.0f && v[2] == 6.0f && v[3] == 8.0f, "each by its parameter");
}

static void TestAge(void)
{
    mvfxCompiledEmitter looped = {.delay = 1000, .duration = 4000};
    CHECK(mvfxSourceAge(&looped, 1000 + 3000) == 0.75f, "a loop's normalized age");
    CHECK(mvfxSourceAge(&looped, 1000 + 4000 * 7 + 1000) == 0.25f, "in a later loop");
    mvfxCompiledEmitter endless = {.delay = MVFX_TICKS_PER_SECOND};
    CHECK(mvfxSourceAge(&endless, 3 * MVFX_TICKS_PER_SECOND) == 2.0f, "seconds without a loop");
}

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    const mvfxInstance* live;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter, const float* parameters, int32_t count)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    effectDef.parameters = parameters;
    effectDef.parameterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 77;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

static const float* Array(const Scene* scene, mvfxAttribute attribute)
{
    return mvfxAttributeArray(scene->live, 0, attribute);
}

static void TestValuesThroughTheStep(void)
{
    mvfxCustomAttribute custom[2] = {
        {.components = 2, .initial = {.kind = mvfx_sourceRange, .min = {5, 6}, .max = {5, 6}}},
        {.components = 1},
    };
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 4000.0f;
    emitter.capacity = 4096;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 2, 3}, .max = {2, 4, 6}};
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {1, 0, 0, 1}, .max = {0, 0, 1, 1}, .uniform = true};
    emitter.spin = mvfxRangeSource(-1.0f, 1.0f);
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.frame = (mvfxFrameStart){mvfx_frameRandom, 3, 6};
    emitter.custom = custom;
    emitter.customCount = 2;
    Scene scene = NewScene(&emitter, NULL, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    int32_t count = scene.live->emitters[0].count;
    CHECK(count == 2000, "2,000 born");
    const float* size[3] = {Array(&scene, mvfx_attributeSizeX), Array(&scene, mvfx_attributeSizeY),
                            Array(&scene, mvfx_attributeSizeZ)};
    const float* r = Array(&scene, mvfx_attributeColorR);
    const float* b = Array(&scene, mvfx_attributeColorB);
    const float* angle = Array(&scene, mvfx_attributeAngle);
    const float* spin = Array(&scene, mvfx_attributeSpin);
    const float* q[4] = {
        Array(&scene, mvfx_attributeOrientationX), Array(&scene, mvfx_attributeOrientationY),
        Array(&scene, mvfx_attributeOrientationZ), Array(&scene, mvfx_attributeOrientationW)};
    const float* angular = Array(&scene, mvfx_attributeAngularX);
    const uint32_t* frames = mvfxAttributeArray(scene.live, 0, mvfx_attributeFrame);
    const float* custom0 = Array(&scene, mvfx_attributeCustom);
    const float* custom1 = Array(&scene, (mvfxAttribute)(mvfx_attributeCustom + 1));
    const float* custom4 = Array(&scene, (mvfxAttribute)(mvfx_attributeCustom + 4));
    bool ok = true;
    double squares[4] = {0.0};
    int64_t frameHits[4] = {0};
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            ok = ok && size[k][i] >= (float)(k + 1) && size[k][i] <= (float)(2 * (k + 1));
        }
        ok = ok && fabsf(r[i] + b[i] - 1.0f) <= 1e-6f;
        ok = ok && angle[i] == 0.0f && spin[i] >= -1.0f && spin[i] <= 1.0f;
        double length = 0.0;
        for (int32_t k = 0; k < 4; ++k)
        {
            length += (double)q[k][i] * (double)q[k][i];
            squares[k] += (double)q[k][i] * (double)q[k][i];
        }
        ok = ok && fabs(length - 1.0) <= 1e-5 && angular[i] == 0.0f;
        ok = ok && frames[i] >= 3 && frames[i] <= 6;
        frameHits[frames[i] < 3 || frames[i] > 6 ? 0 : frames[i] - 3] += 1;
        ok = ok && custom0[i] == 5.0f && custom1[i] == 6.0f && custom4[i] == 0.0f;
    }
    CHECK(ok, "every value in its range, on its line, of unit length");
    for (int32_t k = 0; k < 4; ++k)
    {
        // A uniform rotation's quaternion: each component's square averages
        // 1/4.
        CHECK(fabs(squares[k] / count - 0.25) <= 0.02, "orientations uniform");
        CHECK(fabs((double)frameHits[k] - count / 4.0) <= 5.0 * sqrt(count * 3.0 / 16.0),
              "frames alike");
    }
    mvfxDestroyContext(scene.context);

    // Euler angles: a quarter turn about X, then one about Z.
    emitter = mvfxDefaultEmitterDef();
    emitter.orientationMode = mvfx_orientationEuler;
    float quarter = 1.57079632679f;
    emitter.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {quarter, 0, quarter}, .max = {quarter, 0, quarter}};
    emitter.frame = (mvfxFrameStart){mvfx_frameFixed, 4, 9};
    scene = NewScene(&emitter, NULL, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    // qz(90) qx(90) = (h, 0, 0, h)(rotated): (cz sx, sz sx, sz cx, cz cx)
    // with every half sine and cosine sqrt(1/2): (1/2, 1/2, 1/2, 1/2).
    bool euler = scene.live->emitters[0].count == 8;
    const uint32_t* fixedFrames = mvfxAttributeArray(scene.live, 0, mvfx_attributeFrame);
    for (int32_t i = 0; i < scene.live->emitters[0].count; ++i)
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            euler =
                euler && fabsf(Array(&scene, (mvfxAttribute)(mvfx_attributeOrientationX + k))[i] -
                               0.5f) <= 1e-6f;
        }
        euler = euler && fixedFrames[i] == 4;
    }
    CHECK(euler, "Euler angles about X, then Z; a fixed frame its first");
    mvfxDestroyContext(scene.context);

    // Three angles at once, against qz qy qx multiplied out in binary64.
    double angles[3] = {0.3, -1.1, 2.0};
    double h[3][2];
    for (int32_t k = 0; k < 3; ++k)
    {
        h[k][0] = sin(0.5 * (double)(float)angles[k]);
        h[k][1] = cos(0.5 * (double)(float)angles[k]);
    }
    // qy qx, then qz (qy qx): Hamilton products of (x, y, z, w).
    double yx[4] = {h[1][1] * h[0][0], h[1][0] * h[0][1], -h[1][0] * h[0][0], h[1][1] * h[0][1]};
    double expected[4] = {
        h[2][1] * yx[0] - h[2][0] * yx[1],
        h[2][1] * yx[1] + h[2][0] * yx[0],
        h[2][1] * yx[2] + h[2][0] * yx[3],
        h[2][1] * yx[3] - h[2][0] * yx[2],
    };
    emitter.orientation =
        (mvfxVectorSource){.kind = mvfx_sourceRange,
                           .min = {(float)angles[0], (float)angles[1], (float)angles[2]},
                           .max = {(float)angles[0], (float)angles[1], (float)angles[2]}};
    scene = NewScene(&emitter, NULL, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 16) == mvfx_success, "step");
    bool product = scene.live->emitters[0].count == 4;
    for (int32_t k = 0; k < 4; ++k)
    {
        product = product &&
                  fabs((double)Array(&scene, (mvfxAttribute)(mvfx_attributeOrientationX + k))[0] -
                       expected[k]) <= 1e-6;
    }
    CHECK(product, "three angles as qz qy qx");
    mvfxDestroyContext(scene.context);

    // Two custom attributes of one source draw apart.
    mvfxCustomAttribute twins[2] = {
        {.components = 1, .initial = {.kind = mvfx_sourceRange, .min = {0}, .max = {1}}},
        {.components = 1, .initial = {.kind = mvfx_sourceRange, .min = {0}, .max = {1}}},
    };
    emitter = mvfxDefaultEmitterDef();
    emitter.custom = twins;
    emitter.customCount = 2;
    scene = NewScene(&emitter, NULL, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    const float* first = Array(&scene, mvfx_attributeCustom);
    const float* second = Array(&scene, (mvfxAttribute)(mvfx_attributeCustom + 4));
    int32_t alike = 0;
    for (int32_t i = 0; i < scene.live->emitters[0].count; ++i)
    {
        alike += first[i] == second[i] ? 1 : 0;
    }
    CHECK(scene.live->emitters[0].count == 16 && alike == 0, "each custom attribute its own draws");
    mvfxDestroyContext(scene.context);
}

static void TestLivesAndParameters(void)
{
    // Lives by a parameter: 0.25 s from the default, then 0 (held to one
    // tick, so every later birth is owed and gone at once).
    static const float defaults[8] = {0.25f, 0, 0, 0, 9, 9, 9, 9};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.lifetime =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {1.0f, 1.0f}, .parameter = 0};
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceParameter, .min = {1, 1, 1}, .max = {1, 1, 1}, .parameter = 1};
    Scene scene = NewScene(&emitter, defaults, 2);
    float read[4];
    CHECK(mvfxInstance_GetParameter(scene.context, scene.instance, 0, read) == mvfx_success &&
              read[0] == 0.25f,
          "the default");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) == mvfx_success,
          "stats");
    CHECK(stats.owed == 64 && stats.live == 16, "a quarter second's births alive");
    CHECK(Array(&scene, mvfx_attributeSizeY)[0] == 9.0f, "a size from a parameter");
    static const float zero[4] = {0, 0, 0, 0};
    static const float big[4] = {1, 2, 3, 4};
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, zero) == mvfx_success, "set");
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 1, big) == mvfx_success, "set");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) == mvfx_success,
          "stats");
    // Only the one born on the step's last tick (2 s) has not yet lived its
    // tick.
    CHECK(stats.owed == 128 && stats.live == 1, "lives of one tick, gone within the step");
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    CHECK(mvfxInstance_GetParameter(scene.context, scene.instance, 1, read) == mvfx_success &&
              read[3] == 4.0f,
          "a restart keeps the parameters");

    uint64_t misuse = mvfxContext_GetMisuse(scene.context);
    static const float notFinite[4] = {0, INFINITY, 0, 0};
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 2, zero) == mvfx_errorInvalid,
          "past the count");
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, -1, zero) == mvfx_errorInvalid,
          "below 0");
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, notFinite) ==
              mvfx_errorInvalid,
          "not finite");
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, NULL) == mvfx_errorInvalid,
          "no value");
    CHECK(mvfxInstance_GetParameter(scene.context, scene.instance, 5, read) == mvfx_errorInvalid &&
              read[0] == 0.0f,
          "zeroed on failure");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 5, "each counted");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_SetParameter(scene.context, stale, 0, zero) == mvfx_errorStale, "stale");
    CHECK(mvfxInstance_GetParameter(scene.context, stale, 0, read) == mvfx_errorStale, "stale");
    CHECK(mvfxInstance_SetParameter(NULL, scene.instance, 0, zero) == mvfx_errorInvalid,
          "no context");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 5, "stale ids are not misuse");
    mvfxDestroyContext(scene.context);

    // A parameter's life past the longest is held to it.
    static const float forever[4] = {1e20f, 0, 0, 0};
    emitter = mvfxDefaultEmitterDef();
    emitter.lifetime =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {1.0f, 1.0f}, .parameter = 0};
    scene = NewScene(&emitter, forever, 1);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    CHECK(Array(&scene, mvfx_attributeInverseLife)[0] == 1.0f / MVFX_MAX_LIFETIME,
          "held to the longest life");
    mvfxDestroyContext(scene.context);

    // Lives by a curve over the loop: births late in a loop live longer.
    emitter = mvfxDefaultEmitterDef();
    emitter.duration = 1.0f;
    emitter.lifetime = (mvfxScalarSource){
        .kind = mvfx_sourceCurve, .range = {2.0f, 2.0f}, .curve = {s_ramp, 2, mvfx_tangentsGiven}};
    scene = NewScene(&emitter, NULL, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND - 1) == mvfx_success, "step");
    const float* inverse = Array(&scene, mvfx_attributeInverseLife);
    int32_t count = scene.live->emitters[0].count;
    bool longer = count > 32;
    for (int32_t i = 1; i < count; ++i)
    {
        longer = longer && inverse[i] < inverse[i - 1];
    }
    CHECK(longer, "later births live longer");
    mvfxDestroyContext(scene.context);
}

int main(void)
{
    TestScalarKinds();
    TestVectorKinds();
    TestAge();
    TestValuesThroughTheStep();
    TestLivesAndParameters();
    return s_failures == 0 ? 0 : 1;
}
