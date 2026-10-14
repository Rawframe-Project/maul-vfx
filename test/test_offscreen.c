// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Off-screen policies: a throttled instance stepping every n-th unseen
// step by their summed length, in its seed's phase, a sum past the
// longest step stepped first, held ticks stepped when seen again; a
// paused one's clock stopped; simulate as when seen; skipped steps'
// events gone; a restart dropping what is held; a policy changed with
// ticks held; the checks.

#include "test_harness.h"

#include "maul-vfx/instance.h"

// Eight particles a second, each living long.
static mvfxContext* s_context;
static mvfxEffectId s_effect;

static void Open(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 200;
    emitter.rate = 8.0f;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.events = mvfx_eventMaskBirth;
    emitter.eventCapacity = 16;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    CHECK(mvfxCreateEffect(s_context, &def, &s_effect) == mvfx_success, "effect");
}

static mvfxInstanceId New(mvfxOffscreen offscreen, int32_t interval, uint32_t seed)
{
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = s_effect;
    def.seed = seed;
    def.offscreen = offscreen;
    def.offscreenInterval = interval;
    mvfxInstanceId id = {0};
    CHECK(mvfxCreateInstance(s_context, &def, &id) == mvfx_success, "instance");
    return id;
}

static uint64_t Owed(mvfxInstanceId id)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_context, id, 0, &stats) == mvfx_success, "stats");
    return stats.owed;
}

static int32_t EventCount(mvfxInstanceId id)
{
    const mvfxEvent* events = NULL;
    int32_t count = -1;
    CHECK(mvfxInstance_GetEvents(s_context, id, 0, &events, &count) == mvfx_success, "events");
    return count;
}

static void Hide(mvfxInstanceId id, bool hidden)
{
    CHECK(mvfxInstance_SetVisible(s_context, id, !hidden) == mvfx_success, "visibility");
}

static void TestThrottle(void)
{
    // An eighth of a second a step: one birth a step when stepped. Seeds 5
    // and 6 at an interval of 4: phases 1 and 2, due at unseen steps 3, 7
    // and 2, 6.
    Open();
    mvfxInstanceId seen = New(mvfx_offscreenThrottle, 4, 7);
    mvfxInstanceId simulated = New(mvfx_offscreenSimulate, 0, 8);
    mvfxInstanceId first = New(mvfx_offscreenThrottle, 4, 5);
    mvfxInstanceId second = New(mvfx_offscreenThrottle, 4, 6);
    Hide(simulated, true);
    Hide(first, true);
    Hide(second, true);
    uint64_t reference[10] = {0};
    bool ok = true;
    for (int32_t n = 1; n <= 8; ++n)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
        reference[n] = Owed(seen);
        int32_t firstDue = n >= 7 ? 7 : n >= 3 ? 3 : 0;
        int32_t secondDue = n >= 6 ? 6 : n >= 2 ? 2 : 0;
        ok = ok && Owed(first) == reference[firstDue] && Owed(second) == reference[secondDue] &&
             Owed(simulated) == reference[n];
        ok = ok && EventCount(first) == (n == 3 || n == 7 ? 3 + (n == 7) : 0);
    }
    CHECK(reference[8] == 8 && ok, "every fourth step, summed, in each one's phase");
    Hide(first, false);
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success &&
              Owed(first) == 9 && Owed(second) == 6,
          "seen again, what it held stepped; the other not yet due");
    mvfxDestroyContext(s_context);
}

static void TestLongestStep(void)
{
    // Half seconds at an interval of 4, due at the fourth: the third sum
    // passes a second, so the held second steps first.
    Open();
    mvfxInstanceId throttled = New(mvfx_offscreenThrottle, 4, 4);
    Hide(throttled, true);
    static const uint64_t owed[5] = {0, 0, 0, 8, 16};
    bool ok = true;
    for (int32_t n = 1; n <= 4; ++n)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
        ok = ok && Owed(throttled) == owed[n];
    }
    CHECK(ok, "at most the longest step at once");
    mvfxDestroyContext(s_context);
}

static void TestPause(void)
{
    Open();
    mvfxInstanceId paused = New(mvfx_offscreenPause, 0, 1);
    mvfxInstanceId seen = New(mvfx_offscreenPause, 0, 2);
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success &&
              EventCount(paused) == 2,
          "seen, stepped");
    Hide(paused, true);
    for (int32_t n = 0; n < 5; ++n)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    }
    CHECK(Owed(paused) == 2 && EventCount(paused) == 0 && Owed(seen) == 12,
          "paused: no births, the last events gone");
    Hide(paused, false);
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success &&
              Owed(paused) == 4,
          "it goes on from where it stopped");
    mvfxDestroyContext(s_context);
}

static void TestRestartAndChange(void)
{
    Open();
    mvfxInstanceId held = New(mvfx_offscreenThrottle, 8, 0x100u);
    mvfxInstanceId changed = New(mvfx_offscreenThrottle, 8, 0x200u);
    Hide(held, true);
    Hide(changed, true);
    for (int32_t n = 0; n < 3; ++n)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    }
    CHECK(Owed(held) == 0 && Owed(changed) == 0, "three steps held");
    CHECK(mvfxInstance_Restart(s_context, held) == mvfx_success &&
              mvfxInstance_SetOffscreen(s_context, changed, mvfx_offscreenPause, 0) == mvfx_success,
          "restarted; paused");
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success &&
              Owed(changed) == 0,
          "paused, still held");
    Hide(held, false);
    Hide(changed, false);
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success &&
              Owed(held) == 2 && Owed(changed) == 4,
          "a restart drops what it held before; a change keeps it");
    mvfxDestroyContext(s_context);
}

static void TestChecks(void)
{
    Open();
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = s_effect;
    mvfxInstanceId id = {0};
    uint64_t misuse = mvfxContext_GetMisuse(s_context);
    def.offscreen = 3;
    int32_t refused = mvfxCreateInstance(s_context, &def, &id) == mvfx_errorInvalid;
    def.offscreen = mvfx_offscreenThrottle;
    def.offscreenInterval = 1;
    refused += mvfxCreateInstance(s_context, &def, &id) == mvfx_errorInvalid;
    def.offscreenInterval = MVFX_MAX_OFFSCREEN_INTERVAL + 1;
    refused += mvfxCreateInstance(s_context, &def, &id) == mvfx_errorInvalid;
    def.offscreenInterval = MVFX_MAX_OFFSCREEN_INTERVAL;
    CHECK(refused == 3 && mvfxCreateInstance(s_context, &def, &id) == mvfx_success,
          "the policy and interval checked");
    def.offscreen = mvfx_offscreenPause;
    def.offscreenInterval = -7;
    mvfxInstanceId other = {0};
    CHECK(mvfxCreateInstance(s_context, &def, &other) == mvfx_success, "an interval unread");
    refused = mvfxInstance_SetOffscreen(s_context, id, 3, 0) == mvfx_errorInvalid;
    refused +=
        mvfxInstance_SetOffscreen(s_context, id, mvfx_offscreenThrottle, 1) == mvfx_errorInvalid;
    refused +=
        mvfxInstance_SetOffscreen(s_context, id, mvfx_offscreenThrottle, 65) == mvfx_errorInvalid;
    CHECK(refused == 3 && mvfxContext_GetMisuse(s_context) == misuse + 6, "refused, counted");
    CHECK(mvfxInstance_SetOffscreen(s_context, id, mvfx_offscreenThrottle, 2) == mvfx_success &&
              mvfxInstance_SetOffscreen(s_context, id, mvfx_offscreenSimulate, 0) == mvfx_success,
          "changed");
    mvfxInstanceId stale = id;
    stale.generation += 1;
    CHECK(mvfxInstance_SetVisible(s_context, stale, false) == mvfx_errorStale &&
              mvfxInstance_SetOffscreen(s_context, stale, mvfx_offscreenPause, 0) ==
                  mvfx_errorStale &&
              mvfxInstance_SetVisible(NULL, id, false) == mvfx_errorInvalid &&
              mvfxInstance_SetOffscreen(NULL, id, mvfx_offscreenPause, 0) == mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestThrottle();
    TestLongestStep();
    TestPause();
    TestRestartAndChange();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
