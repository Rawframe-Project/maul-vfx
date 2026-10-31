// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying contexts, and every refusal of a def; a NULL
// context's counts.

#include "test_harness.h"

#include "maul-vfx/context.h"

#include <stdlib.h>

// An allocator that counts what it holds and can be told to fail.
typedef struct CountingAllocator
{
    int64_t blocks;
    int64_t bytes;
    bool fail;
    // When above 0, the allocation of that number (from 1) fails.
    int64_t failAt;
    int64_t calls;
} CountingAllocator;

static void* CountingAlloc(size_t size, size_t alignment, void* context)
{
    CountingAllocator* counter = context;
    counter->calls += 1;
    if (counter->fail || counter->calls == counter->failAt)
    {
        return NULL;
    }
    // The test asks for nothing aligned beyond max_align_t of the context.
    (void)alignment;
    counter->blocks += 1;
    counter->bytes += (int64_t)size;
    return malloc(size);
}

static void CountingFree(void* memory, size_t size, size_t alignment, void* context)
{
    CountingAllocator* counter = context;
    (void)alignment;
    counter->blocks -= 1;
    counter->bytes -= (int64_t)size;
    free(memory);
}

static mvfxAllocator CountingAllocatorFor(CountingAllocator* counter)
{
    mvfxAllocator allocator = {CountingAlloc, CountingFree, counter};
    return allocator;
}

static void* InlineEnqueue(mvfxTaskFunction* task, int32_t itemCount, int32_t minRange,
                           void* taskContext, void* userContext)
{
    (void)minRange;
    (void)userContext;
    task(0, itemCount, taskContext);
    return NULL;
}

static void NoFinish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

static void TestDefaultDefCreatesAndDestroys(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "default def creates");
    CHECK(context != NULL, "context returned");
    CHECK(mvfxContext_GetMisuse(context) == 0, "no misuse yet");
    mvfxDestroyContext(context);
}

static void TestDestroyNullDoesNothing(void)
{
    mvfxDestroyContext(NULL);
    CHECK(true, "destroying NULL returns");
}

// A NULL context's counts are 0, not a read through it.
static void TestNullCountsNothing(void)
{
    CHECK(mvfxContext_GetMisuse(NULL) == 0, "no misuse for a NULL context");
    CHECK(mvfxContext_GetParticles(NULL) == 0, "no particles for a NULL context");
}

static void TestHostAllocatorHoldsEverything(void)
{
    CountingAllocator counter = {0};
    mvfxContextDef def = mvfxDefaultContextDef();
    def.allocator = CountingAllocatorFor(&counter);
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "creates on the host allocator");
    CHECK(counter.blocks == 6, "the context and its five tables while alive");
    mvfxDestroyContext(context);
    CHECK(counter.blocks == 0 && counter.bytes == 0, "everything returned");
}

static void TestEachAllocationFailing(void)
{
    // The context and each of its tables failing in turn: capacity, and
    // what was taken returned.
    for (int64_t n = 1; n <= 6; ++n)
    {
        CountingAllocator counter = {.failAt = n};
        mvfxContextDef def = mvfxDefaultContextDef();
        def.allocator = CountingAllocatorFor(&counter);
        mvfxContext* context = NULL;
        CHECK(mvfxCreateContext(&def, &context) == mvfx_errorCapacity && context == NULL,
              "capacity when one allocation fails");
        CHECK(counter.blocks == 0 && counter.bytes == 0, "everything taken returned");
    }
}

static void TestAllocatorFailureIsCapacity(void)
{
    CountingAllocator counter = {.fail = true};
    mvfxContextDef def = mvfxDefaultContextDef();
    def.allocator = CountingAllocatorFor(&counter);
    mvfxContext* context = (mvfxContext*)&counter;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorCapacity, "capacity on failure");
    CHECK(context == NULL, "out-parameter cleared");
}

static void TestOtherVersionIsRefusedAfterTheCookie(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.version = MVFX_ABI_VERSION + 1;
    def.cookie = 0;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorInvalid, "cookie before version");
    def.cookie = MVFX_CONTEXT_DEF_COOKIE;
    def.limits.effects = 0;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorVersion, "version before the rest");
    def.version = 0;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorVersion, "a zeroed version");
    CHECK((MVFX_ABI_VERSION & 0x80000000u) != 0, "the top bit is set");
}

static void TestInvalidDefsAreRefused(void)
{
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(NULL, &context) == mvfx_errorInvalid, "null def");
    mvfxContextDef def = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&def, NULL) == mvfx_errorInvalid, "null out-parameter");

    def.cookie = 0;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorInvalid, "no cookie");

    def = mvfxDefaultContextDef();
    def.allocator.alloc = CountingAlloc;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorInvalid, "alloc without free");
    def = mvfxDefaultContextDef();
    def.allocator.free = CountingFree;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorInvalid, "free without alloc");

    def = mvfxDefaultContextDef();
    def.enqueueTask = InlineEnqueue;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorInvalid, "enqueue without finish");
    def = mvfxDefaultContextDef();
    def.finishTask = NoFinish;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_errorInvalid, "finish without enqueue");
    CHECK(context == NULL, "nothing created");
}

static void TestTaskHooksAreAccepted(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.enqueueTask = InlineEnqueue;
    def.finishTask = NoFinish;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "both hooks accepted");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestDefaultDefCreatesAndDestroys();
    TestDestroyNullDoesNothing();
    TestNullCountsNothing();
    TestHostAllocatorHoldsEverything();
    TestAllocatorFailureIsCapacity();
    TestEachAllocationFailing();
    TestOtherVersionIsRefusedAfterTheCookie();
    TestInvalidDefsAreRefused();
    TestTaskHooksAreAccepted();
    return s_failures == 0 ? 0 : 1;
}
