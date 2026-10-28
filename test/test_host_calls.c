// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code calling back into the context it serves: from the allocator's
// alloc and free, the task hooks' enqueue and finish, and a host module,
// each of the context's calls that take it mutable is refused with
// mvfx_errorBusy and counted as misuse, a reading call still answers, and
// the context is whole after: a step, a creation and a destruction work
// again, the callbacks then calling nothing. Under a host's float setting
// (src/fpenv.h): the allocator and the task hooks, which compute nothing
// of the library's, run in the host's own, even when the library calls
// them from inside its environment; a host module, whose results feed the
// simulation, runs in the library's.

#include "test_fenv.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
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

// While watching, the setting each callback should see, and how many
// calls saw another, of how many made.
static bool s_watching;
static TestFloatSetting s_host;
static TestFloatSetting s_library;
static int32_t s_seen[MODULE + 1];
static int32_t s_wrong[MODULE + 1];

static void Watch(Callback from)
{
    if (!s_watching)
    {
        return;
    }
    // Inside the library's environment only the register is the
    // library's: on x86-64 <fenv.h> may report the x87 control word,
    // which the library leaves alone.
    TestFloatSetting now = TestFloatGet();
    bool right = from == MODULE ? now.control == s_library.control : TestFloatSame(now, s_host);
    s_seen[from] += 1;
    s_wrong[from] += right ? 0 : 1;
}

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
    bool refused = mvfxContext_StepTicks(target, 1) == mvfx_errorBusy &&
                   mvfxInstance_SetVisible(target, s_instance, false) == mvfx_errorBusy &&
                   mvfxDestroyInstance(target, s_instance) == mvfx_errorBusy;
    mvfxDestroyContext(target);
    s_refused += refused && mvfxGetContextMisuse(target) == before + 4 ? 1 : 0;
    s_made += 1;
}

// Memory aligned by hand (not every C library has aligned_alloc), the
// block's start kept before it.
static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    Watch(ALLOC);
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
    Watch(FREE);
    CallBack(FREE);
    free(((void**)memory)[-1]);
}

// Task hooks running every range at once, calling back in from each.
static void* Enqueue(mvfxTaskFn* task, int32_t itemCount, int32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    (void)userContext;
    Watch(ENQUEUE);
    CallBack(ENQUEUE);
    task(0, itemCount, taskContext);
    return &s_made;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
    Watch(FINISH);
    CallBack(FINISH);
}

static void Module(const mvfxModuleCall* call, void* user)
{
    (void)call;
    (void)user;
    Watch(MODULE);
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

static void TestHostSettings(void)
{
    // An effect's compile allocates inside the library's environment, a
    // rigid body's hull frees its scratch inside it as the effect is made,
    // and a step calls the task hooks and the host module inside it; the
    // instance's release frees outside it.
    s_library = TestFloatGet();
    s_host = TestFloatHostile();
    TestFloatSet(s_host);
    s_watching = true;
    mvfxEffectId effect;
    mvfxContext* context = Open(&effect);
    bool stepped = mvfxContext_StepTicks(context, 1) == mvfx_success;
    static const float corners[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static const uint32_t faces[4][3] = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = &corners[0][0];
    meshDef.vertexCount = 4;
    meshDef.triangles = &faces[0][0];
    meshDef.triangleCount = 4;
    mvfxMeshId mesh = {0};
    mvfxEmitterDef body = mvfxDefaultEmitterDef();
    body.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1}, .max = {1, 1, 1}};
    body.orientationMode = mvfx_orientationEuler;
    body.orientation = (mvfxVectorSource){.kind = mvfx_sourceRange};
    body.rigid = (mvfxRigidDef){.enabled = true,
                                .shape = mvfx_rigidHull,
                                .density = 1.0f,
                                .substeps = 8,
                                .staticFriction = 0.6f,
                                .dynamicFriction = 0.4f};
    mvfxEffectDef bodies = mvfxDefaultEffectDef();
    bodies.emitters = &body;
    bodies.emitterCount = 1;
    mvfxEffectId hulled = {0};
    int32_t freedBefore = s_seen[FREE];
    stepped = stepped && mvfxCreateMesh(context, &meshDef, &mesh) == mvfx_success;
    body.rigid.hull = mesh;
    stepped = stepped && mvfxCreateEffect(context, &bodies, &hulled) == mvfx_success;
    bool freedInside = s_seen[FREE] > freedBefore;
    mvfxDestroyContext(context);
    s_watching = false;
    TestFloatSetting after = TestFloatGet();
    TestFloatSet(s_library);
    CHECK(stepped && freedInside && TestFloatSame(after, s_host),
          "stepped and a hull made, the host's setting kept");
    CHECK(s_seen[ALLOC] > 0 && s_wrong[ALLOC] == 0, "the allocator's alloc in the host's setting");
    CHECK(s_seen[FREE] > 0 && s_wrong[FREE] == 0, "the allocator's free in the host's setting");
    CHECK(s_seen[ENQUEUE] > 0 && s_wrong[ENQUEUE] == 0,
          "the task hooks' enqueue in the host's setting");
    CHECK(s_seen[FINISH] > 0 && s_wrong[FINISH] == 0,
          "the task hooks' finish in the host's setting");
    CHECK(s_seen[MODULE] > 0 && s_wrong[MODULE] == 0, "a host module in the library's setting");
}

int main(void)
{
    TestCallsBackIn();
    TestHostSettings();
    return s_failures == 0 ? 0 : 1;
}
