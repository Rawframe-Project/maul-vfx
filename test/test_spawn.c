// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spawning over an emitter's life against exact integer oracles: rates
// with delays, loops and their ends under random splits of time, bursts at
// their ticks, burst counts and chances by their statistics, rate curves
// against their integrals, emitter states, stop, restart.

#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <stdio.h>

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
    instanceDef.seed = 4242;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

static mvfxEmitterStats Stats(const Scene* scene)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, 0, &stats) == mvfx_success,
          "stats read");
    return stats;
}

static uint32_t s_random = 2024;

static int64_t RandomTicks(int64_t most)
{
    s_random = s_random * 1664525u + 1013904223u;
    return 1 + (int64_t)((s_random >> 8u) % (uint32_t)most);
}

// A rate of numerator / denominator a second, exact in binary32.
typedef struct Rate
{
    int64_t numerator;
    int64_t denominator;
} Rate;

// Births owed by clock for a constant rate: the integral runs from the
// delay through every loop, so it is the rate times the time emitted, the
// last loop's end at most; floor of it, in integers.
static int64_t RateOracle(Rate rate, int64_t delay, int64_t duration, int32_t loops, int64_t clock)
{
    int64_t emitted = clock - delay;
    if (emitted <= 0)
    {
        return 0;
    }
    if (loops > 0 && emitted > duration * loops)
    {
        emitted = duration * loops;
    }
    return emitted * rate.numerator / (rate.denominator * MVFX_TICKS_PER_SECOND);
}

static void TestRatesAgainstIntegers(void)
{
    static const Rate rates[] = {{64, 1}, {75, 2}, {3, 4}, {1000, 1}, {9, 8}};
    // Delays and durations in seconds, exact in ticks.
    static const float delays[] = {0.0f, 0.25f, 0.375f};
    static const float durations[] = {0.0f, 0.75f, 0.0625f, 1.5f};
    static const int32_t loops[] = {0, 1, 3};
    for (int32_t r = 0; r < 5; ++r)
    {
        for (int32_t d = 0; d < 3; ++d)
        {
            for (int32_t u = 0; u < 4; ++u)
            {
                for (int32_t l = 0; l < 3; ++l)
                {
                    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
                    emitter.capacity = 64;
                    emitter.rate = (float)rates[r].numerator / (float)rates[r].denominator;
                    emitter.startDelay = delays[d];
                    emitter.duration = durations[u];
                    emitter.loops = loops[l];
                    emitter.lifetime = mvfxRangeSource(0.1f, 0.3f);
                    Scene scene = NewScene(&emitter);
                    int64_t delay = (int64_t)(delays[d] * MVFX_TICKS_PER_SECOND);
                    int64_t duration = (int64_t)(durations[u] * MVFX_TICKS_PER_SECOND);
                    int32_t loopCount = durations[u] > 0.0f ? loops[l] : 0;
                    int64_t clock = 0;
                    bool agreed = true;
                    while (clock < 6 * MVFX_TICKS_PER_SECOND && agreed)
                    {
                        int64_t ticks = RandomTicks(MVFX_TICKS_PER_SECOND / 3);
                        CHECK(mvfxContext_StepTicks(scene.context, ticks) == mvfx_success, "step");
                        clock += ticks;
                        int64_t expected = RateOracle(rates[r], delay, duration, loopCount, clock);
                        agreed = Stats(&scene).owed == (uint64_t)expected;
                    }
                    if (!agreed)
                    {
                        printf("rate %d/%d delay %g duration %g loops %d clock %lld owed %llu "
                               "expected %lld\n",
                               (int)rates[r].numerator, (int)rates[r].denominator,
                               (double)delays[d], (double)durations[u], loops[l], (long long)clock,
                               (unsigned long long)Stats(&scene).owed,
                               (long long)RateOracle(rates[r], delay, duration, loopCount, clock));
                    }
                    CHECK(agreed, "births owed match the integers");
                    mvfxDestroyContext(scene.context);
                }
            }
        }
    }
}

// Births owed by clock for bursts of fixed counts that always fire.
static int64_t BurstOracle(const mvfxBurst* bursts, int32_t burstCount, int64_t delay,
                           int64_t duration, int32_t loops, int64_t clock)
{
    int64_t owed = 0;
    for (int64_t loop = 0; loops == 0 || loop < loops; ++loop)
    {
        int64_t start = delay + loop * duration;
        if (start > clock)
        {
            break;
        }
        for (int32_t b = 0; b < burstCount; ++b)
        {
            int64_t time = (int64_t)(bursts[b].time * MVFX_TICKS_PER_SECOND);
            int64_t interval = (int64_t)(bursts[b].interval * MVFX_TICKS_PER_SECOND);
            for (int64_t c = 0; bursts[b].cycles == 0 || c < bursts[b].cycles; ++c)
            {
                int64_t tick = time + c * interval;
                if ((duration > 0 && tick >= duration) || start + tick > clock)
                {
                    break;
                }
                owed += bursts[b].minCount;
            }
        }
        if (duration == 0)
        {
            break;
        }
    }
    return owed;
}

static void TestBurstsAtTheirTicks(void)
{
    mvfxBurst bursts[3] = {
        {.time = 0.0f,
         .minCount = 5,
         .maxCount = 5,
         .cycles = 3,
         .interval = 0.125f,
         .probability = 1.0f},
        {.time = 0.25f,
         .minCount = 2,
         .maxCount = 2,
         .cycles = 0,
         .interval = 0.0625f,
         .probability = 1.0f},
        // At or past a loop's end: never fires with a duration of 0.5 s.
        {.time = 0.5f, .minCount = 9, .maxCount = 9, .cycles = 1, .probability = 1.0f},
    };
    static const float durations[] = {0.0f, 0.5f, 0.75f};
    for (int32_t u = 0; u < 3; ++u)
    {
        for (int32_t loops = 0; loops < 3; ++loops)
        {
            mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
            emitter.capacity = 32;
            emitter.rate = 0.0f;
            emitter.bursts = bursts;
            emitter.burstCount = 3;
            emitter.startDelay = 0.125f;
            emitter.duration = durations[u];
            emitter.loops = loops;
            emitter.lifetime = mvfxRangeSource(0.05f, 0.2f);
            Scene scene = NewScene(&emitter);
            int64_t duration = (int64_t)(durations[u] * MVFX_TICKS_PER_SECOND);
            int32_t loopCount = durations[u] > 0.0f ? loops : 0;
            int64_t clock = 0;
            bool agreed = true;
            while (clock < 4 * MVFX_TICKS_PER_SECOND && agreed)
            {
                int64_t ticks = RandomTicks(MVFX_TICKS_PER_SECOND / 5);
                CHECK(mvfxContext_StepTicks(scene.context, ticks) == mvfx_success, "step");
                clock += ticks;
                int64_t expected =
                    BurstOracle(bursts, 3, MVFX_TICKS_PER_SECOND / 8, duration, loopCount, clock);
                agreed = Stats(&scene).owed == (uint64_t)expected;
            }
            CHECK(agreed, "burst births match their ticks");
            mvfxDestroyContext(scene.context);
        }
    }
}

static void TestBurstCountsAndChances(void)
{
    // One firing in the middle of each step: each step's births are one
    // firing's count, drawn uniformly from [2, 5].
    mvfxBurst counted = {.time = 0.03125f,
                         .minCount = 2,
                         .maxCount = 5,
                         .cycles = 0,
                         .interval = 0.0625f,
                         .probability = 1.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 16;
    emitter.rate = 0.0f;
    emitter.bursts = &counted;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(0.01f, 0.01f);
    Scene scene = NewScene(&emitter);
    int64_t histogram[4] = {0};
    uint64_t before = 0;
    const int32_t firings = 4000;
    for (int32_t i = 0; i < firings; ++i)
    {
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 16) == mvfx_success,
              "step");
        uint64_t owed = Stats(&scene).owed;
        uint64_t count = owed - before;
        before = owed;
        if (count < 2 || count > 5)
        {
            CHECK(false, "a count within the range");
            break;
        }
        histogram[count - 2] += 1;
    }
    for (int32_t i = 0; i < 4; ++i)
    {
        // 1,000 expected, five standard deviations of a binomial (27.4).
        CHECK(histogram[i] > 863 && histogram[i] < 1137, "each count about a quarter");
    }
    mvfxDestroyContext(scene.context);

    // A chance of one half over 1,024 firings of 0 to 10 particles: a mean
    // of 2.5 a firing and a variance of 11.25.
    mvfxBurst chance = {.time = 0.0f,
                        .minCount = 0,
                        .maxCount = 10,
                        .cycles = 0,
                        .interval = MVFX_MIN_DURATION,
                        .probability = 0.5f};
    emitter.bursts = &chance;
    emitter.capacity = 64;
    scene = NewScene(&emitter);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND - 1) == mvfx_success, "step");
    double owed = (double)Stats(&scene).owed;
    CHECK(fabs(owed - 2560.0) < 5.0 * sqrt(1024.0 * 11.25), "about half the firings spawn");
    mvfxDestroyContext(scene.context);

    // Firing 23,834,702 of this seed's first burst draws a chance of exactly
    // 0 (found by a search): a chance of 0 still does not spawn. A second
    // burst far ahead keeps the emitter from finishing.
    mvfxBurst zero[2] = {
        {.time = 0.0f,
         .minCount = 1,
         .maxCount = 1,
         .cycles = 0,
         .interval = MVFX_MIN_DURATION,
         .probability = 0.0f},
        {.time = MVFX_MAX_LIFETIME, .minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1.0f},
    };
    emitter.bursts = zero;
    emitter.burstCount = 2;
    scene = NewScene(&emitter);
    mvfxInstance* zeroInstance = scene.context->instances.slots[scene.instance.index1 - 1].object;
    const uint64_t zeroFiring = 23834702;
    zeroInstance->emitters[0].bursts[0].firing = zeroFiring;
    zeroInstance->clock = (int64_t)zeroFiring * 1024 - 1;
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(zeroInstance->emitters[0].bursts[0].firing == zeroFiring + 1, "the firing was drawn");
    CHECK(Stats(&scene).owed == 0, "a draw of exactly 0 against a chance of 0");
    mvfxDestroyContext(scene.context);
    emitter.bursts = &chance;
    emitter.burstCount = 1;

    chance.probability = 0.0f;
    scene = NewScene(&emitter);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Stats(&scene).owed == 0, "a chance of 0 never spawns");
    mvfxDestroyContext(scene.context);
}

// A rate curve's exact integral over a normalized loop age, for the
// curves below: a constant, a ramp up and a ramp down.
static double CurveIntegral(int32_t shape, double age)
{
    switch (shape)
    {
    case 0:
        return age;
    case 1:
        return age * age * 0.5;
    default:
        return age - age * age * 0.5;
    }
}

static void TestRateCurvesAgainstIntegrals(void)
{
    static const mvfxCurveKey constant[1] = {{0.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    static const mvfxCurveKey up[2] = {{0.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                       {1.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    static const mvfxCurveKey down[2] = {{0.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                         {1.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    const mvfxCurveKey* keys[3] = {constant, up, down};
    int32_t keyCounts[3] = {1, 2, 2};
    for (int32_t shape = 0; shape < 3; ++shape)
    {
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.capacity = 512;
        emitter.rate = 300.0f;
        emitter.duration = 0.5f;
        emitter.rateCurve = (mvfxCurve){keys[shape], keyCounts[shape], mvfx_tangentsGiven};
        emitter.lifetime = mvfxRangeSource(0.1f, 0.4f);
        Scene scene = NewScene(&emitter);
        int64_t clock = 0;
        bool close = true;
        while (clock < 3 * MVFX_TICKS_PER_SECOND && close)
        {
            int64_t ticks = RandomTicks(MVFX_TICKS_PER_SECOND / 7);
            CHECK(mvfxContext_StepTicks(scene.context, ticks) == mvfx_success, "step");
            clock += ticks;
            double loops = (double)clock / (0.5 * MVFX_TICKS_PER_SECOND);
            double whole = floor(loops);
            double integral =
                300.0 * 0.5 *
                (whole * CurveIntegral(shape, 1.0) + CurveIntegral(shape, loops - whole));
            double owed = (double)Stats(&scene).owed;
            // Within one particle of the exact integral's floor; a baked
            // linear curve is exact up to float samples.
            close = fabs(owed - floor(integral)) <= 1.0;
        }
        CHECK(close, "a curve's births follow its integral");
        mvfxDestroyContext(scene.context);
    }

    // A constant curve owes the constant rate's births, loop after loop,
    // for rates and loops whose products are whole and not.
    static const float curveRates[] = {10.0f, 37.0f, 3.0f, 7.5f, 100.0f, 1000.0f};
    static const float curveDurations[] = {0.75f, 0.3f, 1.0f, 0.0625f};
    for (int32_t r = 0; r < 6; ++r)
    {
        for (int32_t d = 0; d < 4; ++d)
        {
            mvfxEmitterDef shaped = mvfxDefaultEmitterDef();
            shaped.capacity = 16;
            shaped.rate = curveRates[r];
            shaped.duration = curveDurations[d];
            shaped.rateCurve = (mvfxCurve){constant, 1, mvfx_tangentsGiven};
            shaped.lifetime = mvfxRangeSource(0.01f, 0.01f);
            mvfxEmitterDef plain = shaped;
            plain.rateCurve = (mvfxCurve){0};
            Scene withCurve = NewScene(&shaped);
            Scene without = NewScene(&plain);
            int64_t loop = (int64_t)floor((double)curveDurations[d] * MVFX_TICKS_PER_SECOND + 0.5);
            bool same = true;
            for (int32_t i = 0; i < 40 && same; ++i)
            {
                CHECK(mvfxContext_StepTicks(withCurve.context, loop) == mvfx_success, "step");
                CHECK(mvfxContext_StepTicks(without.context, loop) == mvfx_success, "step");
                same = Stats(&withCurve).owed == Stats(&without).owed;
            }
            CHECK(same, "a constant curve owes the constant rate's births");
            mvfxDestroyContext(withCurve.context);
            mvfxDestroyContext(without.context);
        }
    }

    // A constant curve owes the constant rate's births exactly, loop after
    // loop.
    mvfxEmitterDef curved = mvfxDefaultEmitterDef();
    curved.capacity = 128;
    curved.rate = 64.0f;
    curved.duration = 1.0f;
    curved.rateCurve = (mvfxCurve){constant, 1, mvfx_tangentsGiven};
    mvfxEmitterDef flat = curved;
    flat.rateCurve = (mvfxCurve){0};
    Scene a = NewScene(&curved);
    Scene b = NewScene(&flat);
    for (int32_t loop = 1; loop <= 5; ++loop)
    {
        CHECK(mvfxContext_StepTicks(a.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        CHECK(mvfxContext_StepTicks(b.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        CHECK(Stats(&a).owed == (uint64_t)(64 * loop) && Stats(&b).owed == (uint64_t)(64 * loop),
              "64 a loop either way");
    }
    mvfxDestroyContext(a.context);
    mvfxDestroyContext(b.context);
}

static void TestEmitterStates(void)
{
    // Delayed a quarter second, two loops of half a second, lives of a
    // quarter second: delayed, active, stopping at 1.25 s, stopped at 1.5 s.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 64.0f;
    emitter.startDelay = 0.25f;
    emitter.duration = 0.5f;
    emitter.loops = 2;
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    Scene scene = NewScene(&emitter);
    const int64_t quarter = MVFX_TICKS_PER_SECOND / 4;
    CHECK(Stats(&scene).status == mvfx_emitterDelayed, "delayed at first");
    CHECK(mvfxContext_StepTicks(scene.context, quarter - 1) == mvfx_success, "step");
    CHECK(Stats(&scene).status == mvfx_emitterDelayed, "delayed a tick before");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(Stats(&scene).status == mvfx_emitterActive, "active at its delay");
    CHECK(mvfxContext_StepTicks(scene.context, 4 * quarter - 1) == mvfx_success, "step");
    CHECK(Stats(&scene).status == mvfx_emitterActive, "active a tick before the loops end");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.status == mvfx_emitterStopping && stats.owed == 64, "stopping, 64 owed");
    CHECK(!mvfxInstance_IsFinished(scene.context, scene.instance), "not finished");
    CHECK(mvfxContext_StepTicks(scene.context, quarter) == mvfx_success, "step");
    stats = Stats(&scene);
    CHECK(stats.status == mvfx_emitterStopped && stats.live == 0, "stopped and empty");
    CHECK(mvfxInstance_IsFinished(scene.context, scene.instance), "finished");
    CHECK(mvfxContext_StepTicks(scene.context, 4 * quarter) == mvfx_success, "step");
    CHECK(Stats(&scene).owed == 64, "nothing more");
    CHECK(!mvfxInstance_IsFinished(scene.context, (mvfxInstanceId){0}), "a null id");
    mvfxDestroyContext(scene.context);

    // Without a rate or a burst that can fire, an endless emitter is
    // stopped from the start.
    emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    scene = NewScene(&emitter);
    CHECK(Stats(&scene).status == mvfx_emitterStopped, "nothing to spawn");
    CHECK(mvfxInstance_IsFinished(scene.context, scene.instance), "finished at once");
    mvfxDestroyContext(scene.context);

    // A looping emitter whose only burst falls at its loops' end never
    // spawns.
    mvfxBurst atEnd = {
        .time = 0.5f, .minCount = 3, .maxCount = 3, .cycles = 1, .probability = 1.0f};
    emitter.bursts = &atEnd;
    emitter.burstCount = 1;
    emitter.duration = 0.5f;
    scene = NewScene(&emitter);
    CHECK(Stats(&scene).status == mvfx_emitterStopped, "a burst past its loop never fires");
    mvfxDestroyContext(scene.context);
    emitter.duration = 0.0f;

    // A burst that fires its cycles and no more, without a duration.
    mvfxBurst once = {.time = 0.5f,
                      .minCount = 3,
                      .maxCount = 3,
                      .cycles = 2,
                      .interval = 0.25f,
                      .probability = 1.0f};
    emitter.bursts = &once;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(0.125f, 0.125f);
    scene = NewScene(&emitter);
    CHECK(Stats(&scene).status == mvfx_emitterActive, "a burst ahead");
    CHECK(mvfxContext_StepTicks(scene.context, 3 * quarter) == mvfx_success, "step");
    stats = Stats(&scene);
    CHECK(stats.status == mvfx_emitterStopping && stats.owed == 6, "both firings done");
    CHECK(mvfxContext_StepTicks(scene.context, quarter) == mvfx_success, "step");
    CHECK(Stats(&scene).status == mvfx_emitterStopped, "and their particles gone");
    mvfxDestroyContext(scene.context);
}

static void TestStop(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 64.0f;
    emitter.lifetime = mvfxRangeSource(1.0f, 1.0f);
    Scene scene = NewScene(&emitter);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopEmitting) == mvfx_success,
          "stop");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.status == mvfx_emitterStopping && stats.owed == 32, "stopping with 32");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopEmitting) == mvfx_success,
          "a second stop keeps the first's tick");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    stats = Stats(&scene);
    CHECK(stats.owed == 32 && stats.live == 32, "no births after; all live a second");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    CHECK(Stats(&scene).status == mvfx_emitterStopped, "stopped once empty");
    CHECK(mvfxInstance_IsFinished(scene.context, scene.instance), "finished");

    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    stats = Stats(&scene);
    CHECK(stats.status == mvfx_emitterActive && stats.owed == 0 && stats.live == 0,
          "active again from nothing");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopAndClear) == mvfx_success,
          "stop and clear");
    stats = Stats(&scene);
    CHECK(stats.live == 0 && stats.owed == 16 && stats.status == mvfx_emitterStopped,
          "cleared at once");

    uint64_t misuse = mvfxContext_GetMisuse(scene.context);
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, 2) == mvfx_errorInvalid, "a bad mode");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 1, "counted");
    CHECK(mvfxInstance_Stop(NULL, scene.instance, mvfx_stopEmitting) == mvfx_errorInvalid,
          "no context");
    CHECK(mvfxInstance_Restart(NULL, scene.instance) == mvfx_errorInvalid, "no context");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_Stop(scene.context, stale, mvfx_stopEmitting) == mvfx_errorStale, "stale");
    CHECK(mvfxInstance_Restart(scene.context, stale) == mvfx_errorStale, "stale");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 1, "stale ids are not misuse");
    mvfxDestroyContext(scene.context);
}

static uint64_t Hash(const Scene* scene)
{
    const mvfxInstance* instance =
        scene->context->instances.slots[scene->instance.index1 - 1].object;
    return mvfxHashInstance(instance, MVFX_HASH_INIT);
}

static void TestRestartReplays(void)
{
    mvfxBurst burst = {.time = 0.1f,
                       .minCount = 1,
                       .maxCount = 9,
                       .cycles = 0,
                       .interval = 0.2f,
                       .probability = 0.7f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 40;
    emitter.rate = 50.0f;
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.startDelay = 0.05f;
    emitter.duration = 0.6f;
    emitter.lifetime = mvfxRangeSource(0.2f, 1.2f);
    Scene scene = NewScene(&emitter);
    enum
    {
        steps = 40
    };
    int64_t spans[steps];
    uint64_t hashes[steps];
    for (int32_t i = 0; i < steps; ++i)
    {
        spans[i] = RandomTicks(MVFX_TICKS_PER_SECOND / 6);
        CHECK(mvfxContext_StepTicks(scene.context, spans[i]) == mvfx_success, "step");
        hashes[i] = Hash(&scene);
    }
    CHECK(Stats(&scene).refused > 0, "the capacity was reached");
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopEmitting) == mvfx_success,
          "stop");
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    bool same = true;
    for (int32_t i = 0; i < steps; ++i)
    {
        CHECK(mvfxContext_StepTicks(scene.context, spans[i]) == mvfx_success, "step");
        same = same && Hash(&scene) == hashes[i];
    }
    CHECK(same, "a restart plays the same particles again");
    mvfxDestroyContext(scene.context);
}

static void TestShapesThroughTheStep(void)
{
    // A burst of 8 spread evenly on a ring of radius 2 lifted by 1, setting
    // off outward at 3: particle k (in serial order) at k eighths of a turn.
    mvfxBurst ring = {
        .time = 0.125f, .minCount = 8, .maxCount = 8, .cycles = 1, .probability = 1.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.bursts = &ring;
    emitter.burstCount = 1;
    emitter.shape.kind = mvfx_shapeCircle;
    emitter.shape.radius = 2.0f;
    emitter.shape.thickness = 0.0f;
    emitter.shape.offset[2] = 1.0f;
    emitter.shape.arcMode = mvfx_arcBurstSpread;
    emitter.velocity.speed = (mvfxRange){3.0f, 3.0f};
    Scene scene = NewScene(&emitter);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    const mvfxInstance* instance = scene.context->instances.slots[scene.instance.index1 - 1].object;
    CHECK(instance->emitters[0].count == 8, "eight born");
    const float* x = mvfxAttributeArray(instance, 0, mvfx_attributePositionX);
    const float* y = mvfxAttributeArray(instance, 0, mvfx_attributePositionY);
    const float* z = mvfxAttributeArray(instance, 0, mvfx_attributePositionZ);
    const float* vx = mvfxAttributeArray(instance, 0, mvfx_attributeVelocityX);
    const float* vy = mvfxAttributeArray(instance, 0, mvfx_attributeVelocityY);
    const uint32_t* serial = mvfxAttributeArray(instance, 0, mvfx_attributeSerial);
    const uint32_t* birth = mvfxAttributeArray(instance, 0, mvfx_attributeBirthLow);
    bool spread = true;
    for (int32_t k = 0; k < 8; ++k)
    {
        double angle = 2.0 * 3.14159265358979323846 * k / 8.0;
        // Where each was born: its place less its flight since.
        double age = (double)(instance->clock - (int64_t)birth[k]) / MVFX_TICKS_PER_SECOND;
        spread = spread && serial[k] == (uint32_t)k &&
                 fabs((double)x[k] - (double)vx[k] * age - 2.0 * cos(angle)) <= 1e-5 &&
                 fabs((double)y[k] - (double)vy[k] * age - 2.0 * sin(angle)) <= 1e-5 &&
                 z[k] == 1.0f && fabs((double)vx[k] - 3.0 * cos(angle)) <= 1e-5 &&
                 fabs((double)vy[k] - 3.0 * sin(angle)) <= 1e-5;
    }
    CHECK(spread, "spread evenly on the ring, outward at 3");
    mvfxDestroyContext(scene.context);
}

int main(void)
{
    TestRatesAgainstIntegers();
    TestBurstsAtTheirTicks();
    TestBurstCountsAndChances();
    TestRateCurvesAgainstIntegrals();
    TestEmitterStates();
    TestStop();
    TestRestartReplays();
    TestShapesThroughTheStep();
    return s_failures == 0 ? 0 : 1;
}
