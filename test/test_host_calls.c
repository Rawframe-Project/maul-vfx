// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code calling back into the context it serves: from the allocator's
// alloc and free, the task hooks' enqueue and finish, and a host module,
// each of the context's calls that take it mutable is refused with
// mvfx_errorState and counted as misuse, a reading call still answers, and
// the context is whole after: a step, a creation and a destruction work
// again, the callbacks then calling nothing.

#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/modules.h"

#include <stdint.h>
#include <stdlib.h>

// The callback armed to call back in, the context it calls into, the
// instance it names, and how many calls back in were refused as they
// should be, of how many made.
typedef enum Callback
{
    NONE,
    ALLOC,
    FREE,
    ENQUEUE,
    FINISH,
    MODULE
} Callback;
static Callback s_armed;
static mvfxContext* s_target;
static mvfxInstanceId s_instance;
static int32_t s_refused;
static int32_t s_made;

// Calls back in once from the callback armed: a step, an instance's
// visibility, its destruction and the context's, each refused and
// counted; the misuse count, a reading call, still answers.
static void CallBack(Callback from)
{
    mvfxContext* target = s_target;
    if (target == NULL || from != s_armed)
    {
        return;
    }
    s_target = NULL;
    s_armed = NONE;
    uint64_t before = mvfxGetContextMisuse(target);
    bool refused = mvfxContext_StepTicks(target, 1) == mvfx_errorState &&
                   mvfxInstance_SetVisible(target, s_instance, false) == mvfx_errorState &&
                   mvfxDestroyInstance(target, s_instance) == mvfx_errorState;
    mvfxDestroyContext(target);
    s_refused += refused && mvfxGetContextMisuse(target) == before + 4 ? 1 : 0;
    s_made += 1;
}

// Memory aligned by hand (not every C library has aligned_alloc), the
// block's start kept before it.
static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    CallBack(ALLOC);
    uint8_t* raw = malloc(size + alignment + sizeof(void*));
    if (raw == NULL)
    {
        return NULL;
    }
    uintptr_t at = ((uintptr_t)(raw + sizeof(void*)) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)at)[-1] = raw;
    return (void*)at;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    CallBack(FREE);
    free(((void**)memory)[-1]);
}

// Task hooks running every range at once, calling back in from each.
static void* Enqueue(mvfxTaskFn* task, int32_t itemCount, int32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    (void)userContext;
    CallBack(ENQUEUE);
    task(0, itemCount, taskContext);
    return &s_made;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
    CallBack(FINISH);
}

static void Module(const mvfxModuleCall* call, void* user)
{
    (void)call;
    (void)user;
    CallBack(MODULE);
}

static const mvfxHostModule s_module = {.stage = mvfx_stageAfterMotion, .function = Module};

// A context with the counted allocator and the task hooks, an effect of
// one emitter running the host module, and an instance of it.
static mvfxContext* Open(mvfxEffectId* effectOut)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.allocator = (mvfxAllocator){.alloc = Alloc, .free = Free};
    def.enqueueTask = Enqueue;
    def.finishTask = Finish;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.hostModules = &s_module;
    emitter.hostModuleCount = 1;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    CHECK(mvfxCreateEffect(context, &effectDef, effectOut) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = *effectOut;
    CHECK(mvfxCreateInstance(context, &instanceDef, &s_instance) == mvfx_success, "instance");
    return context;
}

// Arms a callback to call back in.
static void Arm(mvfxContext* context, Callback from)
{
    s_target = context;
    s_armed = from;
    s_made = 0;
    s_refused = 0;
}

// Whether the armed callback called back in once and was refused, and the
// context steps after.
static void Expect(mvfxContext* context, const char* what)
{
    CHECK(s_made == 1 && s_refused == 1, what);
    s_target = NULL;
    s_armed = NONE;
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "a step after");
}

static void TestCallsBackIn(void)
{
    mvfxEffectId effect;
    mvfxContext* context = Open(&effect);
    // A step: its tasks (1,024 instance slots, more than a range) and the
    // emitter's host module.
    static const Callback stepping[3] = {ENQUEUE, FINISH, MODULE};
    static const char* const names[3] = {"from the task hooks' enqueue",
                                         "from the task hooks' finish", "from a host module"};
    for (int32_t c = 0; c < 3; ++c)
    {
        Arm(context, stepping[c]);
        CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "stepped");
        Expect(context, names[c]);
    }
    // An allocation, a second instance; its release.
    Arm(context, ALLOC);
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId second;
    CHECK(mvfxCreateInstance(context, &instanceDef, &second) == mvfx_success, "allocated");
    Expect(context, "from the allocator's alloc");
    Arm(context, FREE);
    CHECK(mvfxDestroyInstance(context, second) == mvfx_success, "released");
    Expect(context, "from the allocator's free");
    // Whole after: a creation, a destruction and the context's.
    CHECK(mvfxCreateInstance(context, &instanceDef, &second) == mvfx_success &&
              mvfxDestroyInstance(context, second) == mvfx_success,
          "a creation and a destruction after");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestCallsBackIn();
    return s_failures == 0 ? 0 : 1;
}
