// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Value sources and the initial values they draw, read where a host reads
// them (the particles' records, and a host module after motion for spin,
// spin axes, births and lives): each kind against its formula, the age a
// source reads in a loop and without one, the refusals, and every
// optional value through the step (sizes, colors on the line between two,
// sprite and mesh rotations, frames, custom attributes, lives by curve
// and parameter) with the instance parameters that feed them; and the
// largest a source may draw.

#define PEEK_MOST 4096

#include "particles.h"
#include "peek.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

static const mvfxCurveKey s_ramp[2] = {{0.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                       {1.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
static const mvfxColorKey s_redToBlue[2] = {{0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}};
static const mvfxAlphaKey s_opaque[1] = {{0.0f, 1.0f}};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
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
    return scene;
}

// Steps a scene by ticks, at most a second a step.
static void Run(const Scene* scene, int64_t ticks)
{
    for (int64_t left = ticks; left > 0; left -= MVFX_TICKS_PER_SECOND)
    {
        int64_t step = left < MVFX_TICKS_PER_SECOND ? left : MVFX_TICKS_PER_SECOND;
        CHECK(mvfxContext_StepTicks(scene->context, step) == mvfx_success, "step");
    }
}

// The scene's particles' records, in the pool's order (the peeking
// module's too), and their count.
static TestParticle s_records[PEEK_MOST];
static int32_t Records(const Scene* scene)
{
    int32_t count = ReadParticles(scene->context, scene->instance, 0, s_records, PEEK_MOST);
    CHECK(count >= 0, "records");
    return count;
}

// Whether an effect of the emitter and count parameters of these
// defaults is taken.
static bool TakenWith(const mvfxEmitterDef* emitter, const float* parameters, int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    effectDef.parameters = parameters;
    effectDef.parameterCount = count;
    mvfxEffectId effect = {0};
    bool taken = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    mvfxDestroyContext(context);
    return taken;
}

// Whether an effect of the emitter and parameterCount parameters (all 0)
// is taken.
static bool Taken(const mvfxEmitterDef* emitter, int32_t parameterCount)
{
    static const float zeros[8][4] = {{0}};
    return TakenWith(emitter, &zeros[0][0], parameterCount);
}

// An emitter of many long lives at a rate, peeked.
static mvfxEmitterDef Many(float rate)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = rate;
    emitter.capacity = PEEK_MOST;
    emitter.lifetime = mvfxRangeSource(200.0f, 200.0f);
    return emitter;
}

// Particle i's age in its emitter's loop at its birth, as a source reads
// it: over the loop's ticks from the delay, or seconds without a loop.
static double SourceAge(int32_t i, int64_t delay, int64_t duration)
{
    int64_t since = s_peeked.birth[i] - delay;
    return duration > 0 ? (double)(since % duration) / (double)duration
                        : (double)since / (double)MVFX_TICKS_PER_SECOND;
}

static void TestScalarKinds(void)
{
    // Spins drawn by a range of 2 to 4: within it, uniform over it.
    mvfxEmitterDef base = Many(4000.0f);
    base.spin = mvfxRangeSource(2.0f, 4.0f);
    mvfxEmitterDef emitter = Peeking(&base);
    Scene scene = NewScene(&emitter, NULL, 0);
    Run(&scene, MVFX_TICKS_PER_SECOND / 2);
    int32_t count = s_peeked.count;
    double sum = 0.0;
    bool within = count == 2000;
    for (int32_t i = 0; i < count; ++i)
    {
        within = within && s_peeked.spin[i] >= 2.0f && s_peeked.spin[i] <= 4.0f;
        sum += (double)s_peeked.spin[i];
    }
    CHECK(within, "within the range");
    CHECK(fabs(sum / count - 3.0) <= 5.0 * (2.0 / sqrt(12.0)) / sqrt((double)count),
          "uniform over it");
    mvfxDestroyContext(scene.context);

    // A curve times the range, at the age in a loop of a second each was
    // born at.
    base = Many(4000.0f);
    base.duration = 1.0f;
    base.spin = (mvfxScalarSource){
        .kind = mvfx_sourceCurve, .range = {2.0f, 2.0f}, .curve = {s_ramp, 2, mvfx_tangentsGiven}};
    emitter = Peeking(&base);
    scene = NewScene(&emitter, NULL, 0);
    Run(&scene, MVFX_TICKS_PER_SECOND / 2);
    bool curved = s_peeked.count > 1000;
    for (int32_t i = 0; i < s_peeked.count; ++i)
    {
        double expected = 2.0 * SourceAge(i, 0, MVFX_TICKS_PER_SECOND);
        curved = curved && fabs((double)s_peeked.spin[i] - expected) <= 1e-4;
    }
    CHECK(curved, "the curve at the age times the range");
    mvfxDestroyContext(scene.context);

    // A parameter's x times the range.
    static const float parameters[2][4] = {{3.0f, 0, 0, 0}, {-2.0f, 0, 0, 0}};
    base = Many(64.0f);
    base.spin =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {1.0f, 1.0f}, .parameter = 1};
    emitter = Peeking(&base);
    scene = NewScene(&emitter, &parameters[0][0], 2);
    Run(&scene, MVFX_TICKS_PER_SECOND / 8);
    CHECK(s_peeked.count > 0 && s_peeked.spin[0] == -2.0f, "the parameter's x");
    mvfxDestroyContext(scene.context);
    CHECK(Taken(&base, 2) && !Taken(&base, 1), "a parameter past the count refused");

    // None where optional (the spin), not where required (the life).
    base = Many(64.0f);
    base.spin = (mvfxScalarSource){0};
    CHECK(Taken(&base, 0), "none when optional");
    base.lifetime = (mvfxScalarSource){0};
    CHECK(!Taken(&base, 0), "none when required");

    // A key past 1 refused in a loop, taken in seconds without one.
    base = Many(64.0f);
    base.spin = (mvfxScalarSource){
        .kind = mvfx_sourceCurve,
        .range = {1.0f, 1.0f},
        .curve = {(const mvfxCurveKey[1]){{2.5f, 1.0f, 0.0f, 0.0f, 0}}, 1, mvfx_tangentsGiven}};
    CHECK(Taken(&base, 0), "in seconds without a loop");
    base.duration = 1.0f;
    CHECK(!Taken(&base, 0), "a key past 1 with a duration");

    // A curve in seconds, from 2 s to 4 s: half way at 3 s.
    static const mvfxCurveKey seconds[2] = {{2.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                            {4.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    base = Many(500.0f);
    base.spin = (mvfxScalarSource){
        .kind = mvfx_sourceCurve, .range = {1.0f, 1.0f}, .curve = {seconds, 2, mvfx_tangentsGiven}};
    emitter = Peeking(&base);
    scene = NewScene(&emitter, NULL, 0);
    Run(&scene, 4 * MVFX_TICKS_PER_SECOND);
    int32_t between = 0;
    bool spanned = true;
    for (int32_t i = 0; i < s_peeked.count; ++i)
    {
        double age = SourceAge(i, 0, 0);
        if (age >= 2.0 && age <= 4.0)
        {
            between += 1;
            spanned = spanned && fabs((double)s_peeked.spin[i] - (age - 2.0) / 2.0) <= 1e-3;
        }
    }
    CHECK(between > 500 && spanned, "a curve in seconds over its keys' span");
    mvfxDestroyContext(scene.context);

    // Baked over its keys' span, not from 0: a peak at 100.5 s of a curve
    // from 100 s to 101 s keeps its height.
    static const mvfxCurveKey peak[3] = {{100.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                         {100.5f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                         {101.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    base = Many(32.0f);
    base.spin = (mvfxScalarSource){
        .kind = mvfx_sourceCurve, .range = {1.0f, 1.0f}, .curve = {peak, 3, mvfx_tangentsGiven}};
    emitter = Peeking(&base);
    scene = NewScene(&emitter, NULL, 0);
    Run(&scene, 101 * MVFX_TICKS_PER_SECOND);
    float highest = 0.0f;
    bool shaped = true;
    for (int32_t i = 0; i < s_peeked.count && i < PEEK_MOST; ++i)
    {
        double age = SourceAge(i, 0, 0);
        if (age >= 100.0 && age <= 101.0)
        {
            highest = fmaxf(highest, s_peeked.spin[i]);
            shaped =
                shaped && fabs((double)s_peeked.spin[i] - (1.0 - 2.0 * fabs(age - 100.5))) <= 0.01;
        }
    }
    CHECK(shaped && highest >= 0.95f, "a peak far from 0 kept");
    mvfxDestroyContext(scene.context);

    static const mvfxScalarSource bad[4] = {
        {.kind = 6, .range = {0.0f, 1.0f}},
        {.kind = mvfx_sourceRange, .range = {2.0f, 1.0f}},
        {.kind = mvfx_sourceRange, .range = {(float)NAN, 1.0f}},
        {.kind = mvfx_sourceRange, .range = {0.0f, 2e30f}},
    };
    bool refused = true;
    for (int32_t i = 0; i < 4; ++i)
    {
        base = Many(64.0f);
        base.spin = bad[i];
        refused = refused && !Taken(&base, 0);
    }
    CHECK(refused, "refused");
}

static void TestVectorKinds(void)
{
    // Shared draws: a color between red and blue lies on the line.
    mvfxEmitterDef base = Many(4000.0f);
    base.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {1, 0, 0, 1}, .max = {0, 0, 1, 1}, .uniform = true};
    Scene scene = NewScene(&base, NULL, 0);
    Run(&scene, MVFX_TICKS_PER_SECOND / 2);
    int32_t count = Records(&scene);
    bool onLine = count == 2000;
    for (int32_t i = 0; i < count; ++i)
    {
        const float* v = s_records[i].color;
        onLine = onLine && fabsf(v[0] + v[2] - 1.0f) <= 1e-6f && v[1] == 0.0f && v[3] == 1.0f;
    }
    CHECK(onLine, "shared draws on the line between the two");
    mvfxDestroyContext(scene.context);

    // Separate draws: components independent.
    base.color.uniform = false;
    scene = NewScene(&base, NULL, 0);
    Run(&scene, MVFX_TICKS_PER_SECOND / 2);
    count = Records(&scene);
    int64_t offLine = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        const float* v = s_records[i].color;
        offLine += fabsf(v[0] + v[2] - 1.0f) > 1e-3f ? 1 : 0;
    }
    CHECK(count == 2000 && offLine > 1800, "separate draws leave the line");
    mvfxDestroyContext(scene.context);

    // A gradient at random from red to blue, scaled by 1: red plus blue 1.
    base = Many(4000.0f);
    base.color = (mvfxVectorSource){.kind = mvfx_sourceGradient,
                                    .min = {1, 1, 1, 1},
                                    .max = {1, 1, 1, 1},
                                    .gradient = {s_redToBlue, 2, s_opaque, 1}};
    scene = NewScene(&base, NULL, 0);
    Run(&scene, MVFX_TICKS_PER_SECOND / 2);
    count = Records(&scene);
    double sumRed = 0.0;
    bool along = count == 2000;
    for (int32_t i = 0; i < count; ++i)
    {
        const float* v = s_records[i].color;
        along = along && fabsf(v[0] + v[2] - 1.0f) <= 1e-5f && v[3] == 1.0f;
        sumRed += (double)v[0];
    }
    CHECK(along, "on the gradient");
    CHECK(fabs(sumRed / count - 0.5) <= 5.0 * sqrt(1.0 / 12.0) / sqrt((double)count),
          "at a uniform point");
    mvfxDestroyContext(scene.context);
    mvfxEmitterDef sized = Many(64.0f);
    sized.size = base.color;
    CHECK(!Taken(&sized, 0), "a gradient needs four components");

    // Over age: the gradient at the age in the loop each was born at.
    base.color.kind = mvfx_sourceGradientOverAge;
    CHECK(!Taken(&base, 0), "over age needs a duration");
    base.duration = 1.0f;
    mvfxEmitterDef emitter = Peeking(&base);
    scene = NewScene(&emitter, NULL, 0);
    Run(&scene, 3 * MVFX_TICKS_PER_SECOND / 4);
    count = Records(&scene);
    bool aged = count > 1000 && count == s_peeked.count;
    for (int32_t i = 0; i < count; ++i)
    {
        double age = SourceAge(i, 0, MVFX_TICKS_PER_SECOND);
        aged = aged && fabs((double)s_records[i].color[0] - (1.0 - age)) <= 1e-3 &&
               fabs((double)s_records[i].color[2] - age) <= 1e-3;
    }
    CHECK(aged, "the gradient at age");
    mvfxDestroyContext(scene.context);

    // A curve scales every component; a parameter each by its own.
    base = Many(4000.0f);
    base.duration = 1.0f;
    base.size = (mvfxVectorSource){.kind = mvfx_sourceCurve,
                                   .min = {1, 2, 3, 7},
                                   .max = {1, 2, 3, 7},
                                   .curve = {s_ramp, 2, mvfx_tangentsGiven}};
    emitter = Peeking(&base);
    scene = NewScene(&emitter, NULL, 0);
    Run(&scene, MVFX_TICKS_PER_SECOND / 2);
    count = Records(&scene);
    bool scaled = count > 1000 && count == s_peeked.count;
    for (int32_t i = 0; i < count; ++i)
    {
        double age = SourceAge(i, 0, MVFX_TICKS_PER_SECOND);
        for (int32_t k = 0; k < 3; ++k)
        {
            scaled = scaled && fabs((double)s_records[i].size[k] - (double)(k + 1) * age) <= 1e-4;
        }
    }
    CHECK(scaled, "a curve scales each component");
    mvfxDestroyContext(scene.context);
    static const float parameters[1][4] = {{1.0f, 2.0f, 3.0f, 4.0f}};
    base = Many(64.0f);
    base.color =
        (mvfxVectorSource){.kind = mvfx_sourceParameter, .min = {2, 2, 2, 2}, .max = {2, 2, 2, 2}};
    scene = NewScene(&base, &parameters[0][0], 1);
    Run(&scene, MVFX_TICKS_PER_SECOND / 8);
    const float* v = s_records[0].color;
    CHECK(Records(&scene) > 0 && v[0] == 2.0f && v[1] == 4.0f && v[2] == 6.0f && v[3] == 8.0f,
          "each by its parameter");
    mvfxDestroyContext(scene.context);
}

static void TestAge(void)
{
    // A ramp read in loops of a second from a start a quarter second in:
    // each particle's age in its loop, over eight loops; without a loop,
    // the seconds since the start.
    for (int32_t looped = 0; looped < 2; ++looped)
    {
        static const mvfxCurveKey upward[2] = {
            {0.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
            {10.0f, 10.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
        mvfxEmitterDef base = Many(256.0f);
        base.startDelay = 0.25f;
        base.duration = looped != 0 ? 1.0f : 0.0f;
        base.spin =
            (mvfxScalarSource){.kind = mvfx_sourceCurve,
                               .range = {1.0f, 1.0f},
                               .curve = looped != 0 ? (mvfxCurve){s_ramp, 2, mvfx_tangentsGiven}
                                                    : (mvfxCurve){upward, 2, mvfx_tangentsGiven}};
        mvfxEmitterDef emitter = Peeking(&base);
        Scene scene = NewScene(&emitter, NULL, 0);
        Run(&scene, 8 * MVFX_TICKS_PER_SECOND + MVFX_TICKS_PER_SECOND / 4);
        bool right = s_peeked.count > 1800;
        int32_t late = 0;
        for (int32_t i = 0; i < s_peeked.count; ++i)
        {
            double age =
                SourceAge(i, MVFX_TICKS_PER_SECOND / 4, looped != 0 ? MVFX_TICKS_PER_SECOND : 0);
            right = right && fabs((double)s_peeked.spin[i] - age) <= 1e-3 * (age > 1.0 ? age : 1.0);
            late +=
                s_peeked.birth[i] >= 7 * MVFX_TICKS_PER_SECOND + MVFX_TICKS_PER_SECOND / 4 ? 1 : 0;
        }
        CHECK(right && late > 0, looped != 0 ? "a loop's normalized age, in later loops too"
                                             : "seconds without a loop");
        mvfxDestroyContext(scene.context);
    }
}

static void TestValuesThroughTheStep(void)
{
    mvfxCustomAttribute custom[2] = {
        {.components = 2, .initial = {.kind = mvfx_sourceRange, .min = {5, 6}, .max = {5, 6}}},
        {.components = 1},
    };
    mvfxEmitterDef base = mvfxDefaultEmitterDef();
    base.rate = 4000.0f;
    base.capacity = 4096;
    base.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 2, 3}, .max = {2, 4, 6}};
    base.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {1, 0, 0, 1}, .max = {0, 0, 1, 1}, .uniform = true};
    base.spin = mvfxRangeSource(-1.0f, 1.0f);
    base.orientationMode = mvfx_orientationRandom;
    base.frame = (mvfxFrameStart){mvfx_frameRandom, 3, 6};
    base.custom = custom;
    base.customCount = 2;
    mvfxEmitterDef emitter = Peeking(&base);
    Scene scene = NewScene(&emitter, NULL, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    int32_t count = Records(&scene);
    CHECK(count == 2000 && s_peeked.count == count, "2,000 born");
    bool ok = true;
    double squares[4] = {0.0};
    int64_t frameHits[4] = {0};
    for (int32_t i = 0; i < count; ++i)
    {
        const TestParticle* p = &s_records[i];
        for (int32_t k = 0; k < 3; ++k)
        {
            ok = ok && p->size[k] >= (float)(k + 1) && p->size[k] <= (float)(2 * (k + 1));
        }
        ok = ok && fabsf(p->color[0] + p->color[2] - 1.0f) <= 1e-6f;
        // Born at angle 0, turned by its spin since, within the half second.
        float spin = s_peeked.spin[i];
        ok = ok && fabsf(p->angle) <= 0.5f * fabsf(spin) && spin >= -1.0f && spin <= 1.0f;
        double length = 0.0;
        for (int32_t k = 0; k < 4; ++k)
        {
            double q = (double)p->orientation[k];
            length += q * q;
            squares[k] += q * q;
        }
        ok = ok && fabs(length - 1.0) <= 1e-5 && s_peeked.angular[0][i] == 0.0f;
        ok = ok && p->frame >= 3 && p->frame <= 6;
        frameHits[p->frame < 3 || p->frame > 6 ? 0 : p->frame - 3] += 1;
        ok = ok && p->custom[0] == 5.0f && p->custom[1] == 6.0f && p->custom2[0] == 0.0f;
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
    count = Records(&scene);
    bool euler = count == 8;
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            euler = euler && fabsf(s_records[i].orientation[k] - 0.5f) <= 1e-6f;
        }
        euler = euler && s_records[i].frame == 4;
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
    bool product = Records(&scene) == 4;
    for (int32_t k = 0; k < 4; ++k)
    {
        product = product && fabs((double)s_records[0].orientation[k] - expected[k]) <= 1e-6;
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
    count = Records(&scene);
    int32_t alike = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        alike += s_records[i].custom[0] == s_records[i].custom2[0] ? 1 : 0;
    }
    CHECK(count == 16 && alike == 0, "each custom attribute its own draws");
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
    CHECK(Records(&scene) > 0 && s_records[0].size[1] == 9.0f, "a size from a parameter");
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

    uint64_t misuse = mvfxGetContextMisuse(scene.context);
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
    CHECK(mvfxGetContextMisuse(scene.context) == misuse + 5, "each counted");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_SetParameter(scene.context, stale, 0, zero) == mvfx_errorStale, "stale");
    CHECK(mvfxInstance_GetParameter(scene.context, stale, 0, read) == mvfx_errorStale, "stale");
    CHECK(mvfxInstance_SetParameter(NULL, scene.instance, 0, zero) == mvfx_errorInvalid,
          "no context");
    CHECK(mvfxGetContextMisuse(scene.context) == misuse + 5, "stale ids are not misuse");
    mvfxDestroyContext(scene.context);

    // A parameter's life past the longest is held to it.
    static const float forever[4] = {1e20f, 0, 0, 0};
    emitter = mvfxDefaultEmitterDef();
    emitter.lifetime =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {1.0f, 1.0f}, .parameter = 0};
    emitter = Peeking(&emitter);
    scene = NewScene(&emitter, forever, 1);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    CHECK(s_peeked.count > 0 && s_peeked.inverseLife[0] == 1.0f / MVFX_MAX_LIFETIME,
          "held to the longest life");
    mvfxDestroyContext(scene.context);

    // Lives by a curve over the loop: births late in a loop live longer.
    emitter = mvfxDefaultEmitterDef();
    emitter.duration = 1.0f;
    emitter.lifetime = (mvfxScalarSource){
        .kind = mvfx_sourceCurve, .range = {2.0f, 2.0f}, .curve = {s_ramp, 2, mvfx_tangentsGiven}};
    emitter = Peeking(&emitter);
    scene = NewScene(&emitter, NULL, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND - 1) == mvfx_success, "step");
    const float* inverse = s_peeked.inverseLife;
    int32_t count = s_peeked.count;
    bool longer = count > 32;
    for (int32_t i = 1; i < count; ++i)
    {
        longer = longer && inverse[i] < inverse[i - 1];
    }
    CHECK(longer, "later births live longer");
    mvfxDestroyContext(scene.context);
}

static void TestDrawBounds(void)
{
    // A source's base at its largest size times its ends' larger size is
    // within its cap: MVFX_MAX_DRAW (1e38) for an angle or an orientation.
    // Ends of 1e30 take a curve of 1e7 and refuse one of 1e9, for a scalar
    // and for a vector (its ends' largest on any axis).
    static const mvfxCurveKey within = {0.0f, 1e7f, 0, 0, mvfx_interpolationLinear};
    static const mvfxCurveKey past = {0.0f, 1e9f, 0, 0, mvfx_interpolationLinear};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.angle = (mvfxScalarSource){.kind = mvfx_sourceCurve,
                                       .range = {-1e30f, 1.0f},
                                       .curve = {&within, 1, mvfx_tangentsGiven}};
    CHECK(Taken(&emitter, 0), "a scalar's curve within");
    emitter.angle.curve.keys = &past;
    CHECK(!Taken(&emitter, 0), "a scalar's curve past");
    emitter = mvfxDefaultEmitterDef();
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){.kind = mvfx_sourceCurve,
                                             .min = {1, 1, 1},
                                             .max = {1, 1e30f, 1},
                                             .curve = {&within, 1, mvfx_tangentsGiven}};
    CHECK(Taken(&emitter, 0), "a vector's curve within");
    emitter.orientation.curve.keys = &past;
    CHECK(!Taken(&emitter, 0), "a vector's curve past");

    // A cubic between two keys of 0 rises by its tangents (to about 2.2e17
    // a third of the way): what it bakes is bounded, not its keys.
    static const mvfxCurveKey rising[2] = {{0.0f, 0.0f, 0.0f, 1e18f, mvfx_interpolationCubic},
                                           {1.0f, 0.0f, -1e18f, 0.0f, mvfx_interpolationCubic}};
    emitter = mvfxDefaultEmitterDef();
    emitter.angle = (mvfxScalarSource){.kind = mvfx_sourceCurve,
                                       .range = {1e20f, 1e20f},
                                       .curve = {rising, 2, mvfx_tangentsGiven}};
    CHECK(Taken(&emitter, 0), "a rise between keys within");
    emitter.angle.range = (mvfxRange){1e21f, 1e21f};
    CHECK(!Taken(&emitter, 0), "a rise between keys past");

    // A colour's cap is MVFX_MAX_COLOR (1e6): a gradient by its largest
    // key, colour or alpha, at a random point and over the loop.
    mvfxColorKey colors[2] = {{0.0f, 1, 0, 0}, {1.0f, 0, 0, 1e3f}};
    mvfxAlphaKey alphas[2] = {{0.0f, 1}, {1.0f, 1}};
    emitter = mvfxDefaultEmitterDef();
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceGradient,
                                       .min = {1, 1, 1, 1},
                                       .max = {1, 1, 1e3f, 1},
                                       .gradient = {colors, 2, alphas, 2}};
    CHECK(Taken(&emitter, 0), "a gradient within");
    colors[1].blue = 2e3f;
    CHECK(!Taken(&emitter, 0), "a colour key past");
    colors[1].blue = 1;
    alphas[0].alpha = -2e3f;
    CHECK(!Taken(&emitter, 0), "an alpha key past");
    alphas[0].alpha = 1;
    emitter.duration = 1.0f;
    emitter.color.kind = mvfx_sourceGradientOverAge;
    CHECK(Taken(&emitter, 0), "a gradient over the loop within");
    colors[0].green = 2e3f;
    CHECK(!Taken(&emitter, 0), "a gradient over the loop past");

    // A parameter by the ends of the sources reading each of its
    // components over their caps: its defaults, and the values the host
    // sets. A size's cap is MVFX_MAX_EXTENT (1e6), an angle's MVFX_MAX_DRAW.
    emitter = mvfxDefaultEmitterDef();
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceParameter, .min = {1e3f, 1, 1}, .max = {1e3f, 1, 1}, .parameter = 0};
    emitter.angle =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {-1e30f, 0}, .parameter = 1};
    static const float spread[8] = {1e3f, 1e6f, 1e6f, 3e38f, 1e7f, 3e38f, 3e38f, 3e38f};
    CHECK(TakenWith(&emitter, spread, 2), "each component by the ends that read it");
    static const float pastX[8] = {2e3f, 0, 0, 0, 0, 0, 0, 0};
    static const float pastY[8] = {0, 2e6f, 0, 0, 0, 0, 0, 0};
    static const float pastAngle[8] = {0, 0, 0, 0, -1e9f, 0, 0, 0};
    CHECK(!TakenWith(&emitter, pastX, 2), "a vector's default past");
    CHECK(!TakenWith(&emitter, pastY, 2), "another component's default past");
    CHECK(!TakenWith(&emitter, pastAngle, 2), "a scalar's default past");
    Scene scene = NewScene(&emitter, spread, 2);
    uint64_t misuse = mvfxGetContextMisuse(scene.context);
    static const float setPast[4] = {2e3f, 0, 0, 0};
    static const float setAnglePast[4] = {1e9f, 0, 0, 0};
    static const float setWithin[4] = {-1e3f, 1e6f, -1e6f, 3e38f};
    float read[4];
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, setPast) == mvfx_errorLimit &&
              mvfxGetContextMisuse(scene.context) == misuse + 1,
          "a value past, refused as misuse");
    CHECK(mvfxInstance_GetParameter(scene.context, scene.instance, 0, read) == mvfx_success &&
              read[0] == 1e3f && read[1] == 1e6f,
          "and not set");
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 1, setAnglePast) ==
              mvfx_errorLimit,
          "a scalar's value past");
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, setWithin) == mvfx_success &&
              mvfxGetContextMisuse(scene.context) == misuse + 2,
          "a value within");
    mvfxDestroyContext(scene.context);

    // At the cap every draw is finite through the step: sizes of 1e6.
    static const mvfxCurveKey whole = {0.0f, 1.0f, 0, 0, mvfx_interpolationLinear};
    emitter = Many(64.0f);
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceCurve,
                                      .min = {1e6f, -1e6f, 1e6f},
                                      .max = {1e6f, -1e6f, 1e6f},
                                      .curve = {&whole, 1, mvfx_tangentsGiven}};
    scene = NewScene(&emitter, NULL, 0);
    Run(&scene, MVFX_TICKS_PER_SECOND);
    int32_t count = Records(&scene);
    bool finite = count > 0;
    for (int32_t i = 0; i < count; ++i)
    {
        finite = finite && s_records[i].size[0] == 1e6f && s_records[i].size[1] == -1e6f;
    }
    CHECK(finite, "the largest sizes finite");
    mvfxDestroyContext(scene.context);
}

int main(void)
{
    TestScalarKinds();
    TestVectorKinds();
    TestAge();
    TestValuesThroughTheStep();
    TestLivesAndParameters();
    TestDrawBounds();
    return s_failures == 0 ? 0 : 1;
}
