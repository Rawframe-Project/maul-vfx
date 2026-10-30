// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Running work through the host's task hooks, and the floating-point mode
// every range runs in.

#include "host_mode.h"
#include "tasks.h"
#include "test_harness.h"

#include <float.h>
#include <string.h>

// What each item of a task saw: how often it ran, and the float results
// computed inside the library's mode.
#define ITEMS 1000
typedef struct Visits
{
    int32_t count[ITEMS];
    float subnormal[ITEMS];
    float rounded[ITEMS];
} Visits;

// volatile keeps the compiler from folding the arithmetic at build time,
// which would not see the thread's mode.
static volatile float s_smallest = FLT_MIN;
static volatile float s_half = 0.5f;
static volatile float s_one = 1.0f;
static volatile float s_third = 1.0f / 3.0f;

static void Visit(int32_t startIndex, int32_t endIndex, void* taskContext)
{
    Visits* visits = taskContext;
    for (int32_t i = startIndex; i < endIndex; ++i)
    {
        visits->count[i] += 1;
        visits->subnormal[i] = s_smallest * s_half;
        visits->rounded[i] = s_one - s_third;
    }
}

// A host task system that cuts the work into ranges of minRange items and
// runs them last first, as a scheduler might.
typedef struct ReverseHost
{
    int32_t enqueued;
    int32_t finished;
    int32_t ranges;
    int32_t shortestNotLast;
    bool returnNull;
} ReverseHost;

static void* ReverseEnqueue(mvfxTaskFunction* task, int32_t itemCount, int32_t minRange,
                            void* taskContext, void* userContext)
{
    ReverseHost* host = userContext;
    host->enqueued += 1;
    host->shortestNotLast = itemCount;
    int32_t rangeCount = (itemCount + minRange - 1) / minRange;
    for (int32_t r = rangeCount - 1; r >= 0; --r)
    {
        int32_t start = r * minRange;
        int32_t end = start + minRange < itemCount ? start + minRange : itemCount;
        if (r != rangeCount - 1 && end - start < host->shortestNotLast)
        {
            host->shortestNotLast = end - start;
        }
        task(start, end, taskContext);
        host->ranges += 1;
    }
    return host->returnNull ? NULL : host;
}

static void ReverseFinish(void* userTask, void* userContext)
{
    ReverseHost* host = userContext;
    CHECK(userTask == host, "finish gets the handle enqueue returned");
    host->finished += 1;
}

static bool EachItemOnce(const Visits* visits, int32_t itemCount)
{
    for (int32_t i = 0; i < itemCount; ++i)
    {
        if (visits->count[i] != 1)
        {
            return false;
        }
    }
    return true;
}

static void TestSerialRunsEveryItemOnce(void)
{
    static Visits visits;
    memset(&visits, 0, sizeof visits);
    mvfxTasks tasks = {0};
    mvfxRunTasks(&tasks, Visit, ITEMS, 16, &visits);
    CHECK(EachItemOnce(&visits, ITEMS), "serial: each item once");
}

static void TestHostRangesRunEveryItemOnce(void)
{
    static Visits visits;
    memset(&visits, 0, sizeof visits);
    ReverseHost host = {0};
    mvfxTasks tasks = {ReverseEnqueue, ReverseFinish, &host};
    mvfxRunTasks(&tasks, Visit, ITEMS, 64, &visits);
    CHECK(EachItemOnce(&visits, ITEMS), "host: each item once");
    CHECK(host.enqueued == 1 && host.finished == 1, "one enqueue, one finish");
    CHECK(host.ranges == (ITEMS + 63) / 64, "cut into ranges of minRange");
    CHECK(host.shortestNotLast == 64, "no range but the last is short");
}

static void TestNullHandleIsNotFinished(void)
{
    static Visits visits;
    memset(&visits, 0, sizeof visits);
    ReverseHost host = {.returnNull = true};
    mvfxTasks tasks = {ReverseEnqueue, ReverseFinish, &host};
    mvfxRunTasks(&tasks, Visit, ITEMS, 100, &visits);
    CHECK(EachItemOnce(&visits, ITEMS), "each item once");
    CHECK(host.finished == 0, "a NULL handle is not waited on");
}

static void TestSmallOrEmptyWorkStaysOnTheCaller(void)
{
    static Visits visits;
    memset(&visits, 0, sizeof visits);
    ReverseHost host = {0};
    mvfxTasks tasks = {ReverseEnqueue, ReverseFinish, &host};
    mvfxRunTasks(&tasks, Visit, 10, 64, &visits);
    CHECK(EachItemOnce(&visits, 10), "small work done");
    CHECK(host.enqueued == 0, "no more than minRange items are not handed out");
    mvfxRunTasks(&tasks, Visit, 0, 64, &visits);
    mvfxRunTasks(&tasks, Visit, -5, 64, &visits);
    CHECK(host.enqueued == 0 && visits.count[0] == 1, "no items, no work");
}

static void TestRangesRunInTheLibrarysMode(void)
{
    static Visits visits;
    memset(&visits, 0, sizeof visits);
#if HOST_MODE_SETTABLE
    uint64_t saved = SetHostMode();
    uint64_t hostMode = ReadHostMode();
#endif
    ReverseHost host = {0};
    mvfxTasks tasks = {ReverseEnqueue, ReverseFinish, &host};
    mvfxRunTasks(&tasks, Visit, ITEMS, 128, &visits);
#if HOST_MODE_SETTABLE
    CHECK(ReadHostMode() == hostMode, "the host's mode is restored");
    RestoreHostMode(saved);
#endif
    float expectedRounded = 1.0f - 1.0f / 3.0f;
    for (int32_t i = 0; i < ITEMS; ++i)
    {
        CHECK(visits.subnormal[i] == FLT_MIN * 0.5f && visits.subnormal[i] != 0.0f,
              "subnormals kept");
        CHECK(visits.rounded[i] == expectedRounded, "rounded to nearest");
    }
}

int main(void)
{
    TestSerialRunsEveryItemOnce();
    TestHostRangesRunEveryItemOnce();
    TestNullHandleIsNotFinished();
    TestSmallOrEmptyWorkStaysOnTheCaller();
    TestRangesRunInTheLibrarysMode();
    return s_failures == 0 ? 0 : 1;
}
