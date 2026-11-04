// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code calling back into the context it serves: from the allocator's
// alloc and free, the task hooks' enqueue and finish, and a host module,
// each of the context's calls that take it mutable is refused with
// mvfx_errorBusy and counted as misuse (a planned instance's setters among
// them, each before it looks at its arguments), a reading call still
// answers, and the context is whole after: a step, a creation and a
// destruction work again, the callbacks then calling nothing. Under a host's float setting
// (src/fpenv.h): the allocator and the task hooks, which compute nothing
// of the library's, run in the host's own, even when the library calls
// them from inside its environment; a host module, whose results feed the
// simulation, runs in the library's.

#include "test_fenv.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/file.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/modules.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    uint64_t before = mvfxContext_GetMisuse(target);
    bool refused = mvfxContext_StepTicks(target, 1) == mvfx_errorBusy &&
                   mvfxInstance_SetVisible(target, s_instance, false) == mvfx_errorBusy &&
                   mvfxDestroyInstance(target, s_instance) == mvfx_errorBusy;
    mvfxDestroyContext(target);
    s_refused += refused && mvfxContext_GetMisuse(target) == before + 4 ? 1 : 0;
    s_made += 1;
}

// While destructions run, the context and the ids the allocator's free
// reads after each block it returns but the context's own: each reading
// call answers from a live object or finds its id gone, and never reads
// a block already returned. How many times it read; how many times the
// mesh's hull read back stale, and how many times it read back neither
// whole nor stale.
static const mvfxContext* s_reader;
static mvfxMeshId s_readMesh;
static mvfxGridId s_readGrid;
static mvfxLayoutId s_readLayout;
static mvfxEffectId s_readEffect;
static mvfxInstanceId s_readInstance;
static int32_t s_reads;
static int32_t s_hullsStale;
static int32_t s_hullsWrong;

static void Read(const void* freed)
{
    const mvfxContext* reader = s_reader;
    if (reader == NULL || freed == (const void*)reader)
    {
        return;
    }
    mvfxMeshView mesh;
    mvfxGridView grid;
    mvfxLayoutView layout;
    static mvfxEmitterView emitter;
    mvfxMeshView spawning;
    (void)mvfxMesh_GetView(reader, s_readMesh, &mesh);
    (void)mvfxGrid_GetView(reader, s_readGrid, &grid);
    (void)mvfxLayout_GetView(reader, s_readLayout, &layout);
    (void)mvfxEffect_GetEmitterView(reader, s_readEffect, 0, &emitter);
    (void)mvfxInstance_GetEmitterMesh(reader, s_readInstance, 0, &spawning);
    bool hull = false;
    float center[3];
    int32_t vertices = 0;
    mvfxResult centered = mvfxMesh_GetHullCenter(reader, s_readMesh, &hull, center);
    mvfxResult counted = mvfxMesh_GetHullVertexCount(reader, s_readMesh, &vertices);
    if (centered == mvfx_errorStale && counted == mvfx_errorStale)
    {
        s_hullsStale += 1;
    }
    else if (centered != mvfx_success || counted != mvfx_success || !hull || vertices != 4)
    {
        s_hullsWrong += 1;
    }
    s_reads += 1;
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
    Read(memory);
}

// Task hooks running every range at once, calling back in from each.
static void* Enqueue(mvfxTaskFunction* task, int32_t itemCount, int32_t minRange, void* taskContext,
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
    CHECK(mvfxCreateEffect(context, &effectDef, effectOut, NULL) == mvfx_success, "effect");
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

// Whether every byte of a buffer is value.
static bool Filled(const void* buffer, size_t size, unsigned char value)
{
    const unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != value)
        {
            return false;
        }
    }
    return true;
}

// From a host module, each call that writes outputs and takes the context
// mutable, called back in: refused as busy and writing none of them,
// counts and single values as well as buffers, since any may be one the
// call in progress fills. The layout is live, so any other refusal would
// zero them. The reads that take a const context answer instead.
static mvfxLayoutId s_layout;
static bool s_left;
// Whether the reads taking a const context answered from the module.
static bool s_answered;

static void WriteBack(const mvfxModuleCall* call, void* user)
{
    (void)call;
    (void)user;
    mvfxContext* target = s_target;
    if (target == NULL)
    {
        return;
    }
    s_target = NULL;
    static const mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    static const mvfxTubeDef tube = {.sides = 3};
    static const double eye[3] = {0, 0, 0};
    static _Alignas(16) unsigned char records[64];
    static _Alignas(16) unsigned char vertices[64];
    static uint32_t indices[6];
    static mvfxLight lights[1];
    static mvfxDrawItem items[1];
    static mvfxRibbon ribbons[1];
    static float positions[3];
    static mvfxBounds bounds;
    static mvfxEmitterStats stats;
    static double remaining;
    static const mvfxEvent* events;
    static const mvfxSceneQuery* queries;
    static int32_t eventCount;
    static int32_t queryCount;
    memset(records, 0xEE, sizeof records);
    memset(vertices, 0xEE, sizeof vertices);
    memset(indices, 0xEE, sizeof indices);
    memset(lights, 0xEE, sizeof lights);
    memset(items, 0xEE, sizeof items);
    memset(ribbons, 0xEE, sizeof ribbons);
    memset(positions, 0xEE, sizeof positions);
    memset(&bounds, 0xEE, sizeof bounds);
    memset(&stats, 0xEE, sizeof stats);
    memset(&remaining, 0xEE, sizeof remaining);
    memset(&eventCount, 0xEE, sizeof eventCount);
    memset(&queryCount, 0xEE, sizeof queryCount);
    events = (const mvfxEvent*)records;
    queries = (const mvfxSceneQuery*)records;
    const mvfxExpandBuffers expand = {
        .vertices = vertices, .vertexCapacity = 4, .indices = indices, .indexCapacity = 6};
    const mvfxRibbonBuffers list = {
        .points = records, .pointCapacity = 4, .ribbons = ribbons, .ribbonCapacity = 1};
    const mvfxSurfaceMeshBuffers mesh = {
        .positions = positions, .vertexCapacity = 1, .indices = indices, .indexCapacity = 6};
    int64_t n[18];
    memset(n, 0xEE, sizeof n);
    mvfxInstanceId i = s_instance;
    mvfxLayoutId l = s_layout;
    bool busy =
        mvfxInstance_WriteParticles(target, i, 0, l, &view, NULL, 0, records, 4, &n[0]) ==
            mvfx_errorBusy &&
        mvfxInstance_WriteLights(target, i, 0, &view, lights, 1, &n[1]) == mvfx_errorBusy &&
        mvfxContext_OrderEmitters(target, eye, NULL, items, 1, &n[2]) == mvfx_errorBusy &&
        mvfxInstance_ExpandSprites(target, i, 0, l, &view, &expand, &n[3]) == mvfx_errorBusy &&
        mvfxInstance_WriteRibbons(target, i, 0, l, &view, &list, &n[4], &n[5]) == mvfx_errorBusy &&
        mvfxInstance_ExpandRibbons(target, i, 0, l, &view, &expand, &n[6], &n[7]) ==
            mvfx_errorBusy &&
        mvfxInstance_ExpandTubes(target, i, 0, l, &view, &tube, &expand, &n[8], &n[9]) ==
            mvfx_errorBusy &&
        mvfxInstance_ExpandSheets(target, i, 0, l, &view, &expand, &n[10], &n[11]) ==
            mvfx_errorBusy &&
        mvfxInstance_WritePlexus(target, i, 0, l, &view, &list, &n[12], &n[13]) == mvfx_errorBusy &&
        mvfxInstance_ExpandPlexus(target, i, 0, l, &view, &expand, &n[14], &n[15]) ==
            mvfx_errorBusy &&
        mvfxInstance_WriteSurfaceMesh(target, i, 0, eye, 0.0f, &mesh, &n[16], &n[17]) ==
            mvfx_errorBusy &&
        mvfxInstance_GetBounds(target, i, -1, &bounds) == mvfx_errorBusy &&
        mvfxInstance_GetEmitterStats(target, i, 0, &stats) == mvfx_errorBusy &&
        mvfxInstance_GetEvents(target, i, 0, &events, &eventCount) == mvfx_errorBusy &&
        mvfxInstance_GetSceneQueries(target, i, 0, &queries, &queryCount) == mvfx_errorBusy &&
        mvfxInstance_Prewarm(target, i, 1.0, 0.1, 4, &remaining) == mvfx_errorBusy;
    s_left = busy && Filled(n, sizeof n, 0xEE) && Filled(records, sizeof records, 0xEE) &&
             Filled(vertices, sizeof vertices, 0xEE) && Filled(indices, sizeof indices, 0xEE) &&
             Filled(lights, sizeof lights, 0xEE) && Filled(items, sizeof items, 0xEE) &&
             Filled(ribbons, sizeof ribbons, 0xEE) && Filled(positions, sizeof positions, 0xEE) &&
             Filled(&bounds, sizeof bounds, 0xEE) && Filled(&stats, sizeof stats, 0xEE) &&
             Filled(&remaining, sizeof remaining, 0xEE) &&
             Filled(&eventCount, sizeof eventCount, 0xEE) &&
             Filled(&queryCount, sizeof queryCount, 0xEE) && events == (const mvfxEvent*)records &&
             queries == (const mvfxSceneQuery*)records;
    // The reads taking a const context answer, never busy: the quality
    // and the sort scratch (none for this emitter); the parameter and the
    // surface mesh's scratch refused as invalid, the effect having neither
    // a parameter nor a gas, each refusal counted as misuse.
    float values[4];
    int32_t level = -1;
    int64_t bytes = -1;
    int64_t meshBytes = -1;
    uint64_t misuse = mvfxContext_GetMisuse(target);
    s_answered =
        mvfxInstance_GetParameter(target, i, 0, values) == mvfx_errorInvalid &&
        mvfxInstance_GetQuality(target, i, &level) == mvfx_success && level == 0 &&
        mvfxInstance_GetSortScratchBytes(target, i, 0, &bytes) == mvfx_success && bytes == 0 &&
        mvfxInstance_GetSurfaceMeshScratchBytes(target, i, 0, &meshBytes) == mvfx_errorInvalid &&
        meshBytes == 0 && mvfxContext_GetMisuse(target) == misuse + 2;
}

static const mvfxHostModule s_writeBack = {.stage = mvfx_stageAfterMotion, .function = WriteBack};

static void TestBusyLeavesBuffers(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.hostModules = &s_writeBack;
    emitter.hostModuleCount = 1;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &s_instance) == mvfx_success, "instance");
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &position;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 16;
    CHECK(mvfxCreateLayout(context, &layoutDef, &s_layout) == mvfx_success, "layout");
    s_left = false;
    s_answered = false;
    s_target = context;
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success && s_target == NULL && s_left,
          "a host module's calls back in refused as busy, writing none of their outputs");
    CHECK(s_answered, "a host module's const reads answered");
    mvfxDestroyContext(context);
}

// Every call that takes the context mutable, each called back in from a
// host module through one table: refused as busy, one misuse counted, and
// nothing changed, the objects it names still there after. Each is called
// with arguments it would take, on live objects. The instance is not
// planned and its effect has no gas or water surface, so a setter of a
// planned instance's gas or water that went on would answer
// mvfx_errorInvalid or mvfx_errorUnsupported instead.
// tools/check_host_calls.py refuses a public call taking the context
// mutable that the table lacks.
typedef mvfxResult (*Change)(mvfxContext* context);

static mvfxEffectId s_effect;
static mvfxGridId s_grid;
static mvfxMeshId s_mesh;
static mvfxLayoutId s_changeLayout;
static const float s_gridValues[8];
static const float s_corners[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
static const mvfxView s_view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
static const double s_origin[3];
static _Alignas(16) unsigned char s_out[256];
static uint32_t s_outIndices[6];
static int64_t s_counts[2];
static const mvfxExpandBuffers s_expand = {
    .vertices = s_out, .vertexCapacity = 4, .indices = s_outIndices, .indexCapacity = 6};

static mvfxResult Step(mvfxContext* context)
{
    return mvfxContext_Step(context, 1.0 / 60.0);
}

static mvfxResult StepTicks(mvfxContext* context)
{
    return mvfxContext_StepTicks(context, 1);
}

static mvfxResult CreateEffect(mvfxContext* context)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect;
    return mvfxCreateEffect(context, &def, &effect, NULL);
}

static mvfxResult DestroyEffect(mvfxContext* context)
{
    return mvfxDestroyEffect(context, s_effect);
}

static mvfxResult CreateEffectFromBytes(mvfxContext* context)
{
    static const _Alignas(16) unsigned char bytes[64];
    mvfxEffectId effect;
    return mvfxCreateEffectFromBytes(context, bytes, sizeof bytes, NULL, 0, &effect, NULL).result;
}

static mvfxResult CreateGrid(mvfxContext* context)
{
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 2;
    def.size[1] = 2;
    def.size[2] = 2;
    def.values = s_gridValues;
    mvfxGridId grid;
    return mvfxCreateGrid(context, &def, &grid);
}

static mvfxResult DestroyGrid(mvfxContext* context)
{
    return mvfxDestroyGrid(context, s_grid);
}

static mvfxResult GridSetValues(mvfxContext* context)
{
    return mvfxGrid_SetValues(context, s_grid, s_gridValues, 8);
}

static mvfxResult GridSetValuesIn(mvfxContext* context)
{
    static const int32_t first[3] = {0, 0, 0};
    static const int32_t size[3] = {1, 1, 1};
    return mvfxGrid_SetValuesIn(context, s_grid, first, size, s_gridValues, 1);
}

static mvfxResult CreateInstance(mvfxContext* context)
{
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = s_effect;
    mvfxInstanceId instance;
    return mvfxCreateInstance(context, &def, &instance);
}

static mvfxResult DestroyInstance(mvfxContext* context)
{
    return mvfxDestroyInstance(context, s_instance);
}

static mvfxResult SetTransform(mvfxContext* context)
{
    static const float rotation[4] = {0, 0, 0, 1};
    return mvfxInstance_SetTransform(context, s_instance, s_origin, rotation, 1.0f, false);
}

static mvfxResult SetVisible(mvfxContext* context)
{
    return mvfxInstance_SetVisible(context, s_instance, false);
}

static mvfxResult SetOffscreen(mvfxContext* context)
{
    return mvfxInstance_SetOffscreen(context, s_instance, mvfx_offscreenPause, 1);
}

static mvfxResult Spawn(mvfxContext* context)
{
    const mvfxSpawnRequest request = {.count = 1, .positions = s_origin};
    return mvfxInstance_Spawn(context, s_instance, 0, &request);
}

static mvfxResult SetAttachFrames(mvfxContext* context)
{
    static const mvfxAttachFrame frame = {.rotation = {0, 0, 0, 1}};
    return mvfxInstance_SetAttachFrames(context, s_instance, 0, &frame, 1);
}

static mvfxResult Prewarm(mvfxContext* context)
{
    double remaining;
    return mvfxInstance_Prewarm(context, s_instance, 1.0, 0.1, 4, &remaining);
}

static mvfxResult SetColliders(mvfxContext* context)
{
    static const mvfxCollider plane = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    return mvfxInstance_SetColliders(context, s_instance, &plane, 1);
}

static mvfxResult GetSceneQueries(mvfxContext* context)
{
    const mvfxSceneQuery* queries;
    int32_t count;
    return mvfxInstance_GetSceneQueries(context, s_instance, 0, &queries, &count);
}

static mvfxResult AnswerSceneQueries(mvfxContext* context)
{
    static const mvfxSceneAnswer answer;
    return mvfxInstance_AnswerSceneQueries(context, s_instance, 0, &answer, 0);
}

static mvfxResult SetDepthImage(mvfxContext* context)
{
    return mvfxInstance_SetDepthImage(context, s_instance, NULL);
}

static mvfxResult WakeBodies(mvfxContext* context)
{
    static const float center[3];
    return mvfxInstance_WakeBodies(context, s_instance, 0, center, 1.0f);
}

static mvfxResult GetEvents(mvfxContext* context)
{
    const mvfxEvent* events;
    int32_t count;
    return mvfxInstance_GetEvents(context, s_instance, 0, &events, &count);
}

static mvfxResult SetEmitterMesh(mvfxContext* context)
{
    return mvfxInstance_SetEmitterMesh(context, s_instance, 0, s_mesh);
}

static mvfxResult SetParameter(mvfxContext* context)
{
    static const float value[4];
    return mvfxInstance_SetParameter(context, s_instance, 0, value);
}

static mvfxResult GetEmitterStats(mvfxContext* context)
{
    mvfxEmitterStats stats;
    return mvfxInstance_GetEmitterStats(context, s_instance, 0, &stats);
}

static mvfxResult Stop(mvfxContext* context)
{
    return mvfxInstance_Stop(context, s_instance, mvfx_stopAndClear);
}

static mvfxResult Restart(mvfxContext* context)
{
    return mvfxInstance_Restart(context, s_instance);
}

static mvfxResult BreakRibbons(mvfxContext* context)
{
    return mvfxInstance_BreakRibbons(context, s_instance, 0);
}

static mvfxResult PushWater(mvfxContext* context)
{
    static const mvfxWaterPush push;
    return mvfxInstance_PushWater(context, s_instance, 0, &push, 0);
}

static mvfxResult CreateMesh(mvfxContext* context)
{
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = s_corners;
    def.vertexCount = 4;
    mvfxMeshId mesh;
    return mvfxCreateMesh(context, &def, &mesh);
}

static mvfxResult DestroyMesh(mvfxContext* context)
{
    return mvfxDestroyMesh(context, s_mesh);
}

static mvfxResult MeshSetPositions(mvfxContext* context)
{
    return mvfxMesh_SetPositions(context, s_mesh, s_corners, NULL, 4);
}

static mvfxResult CreateLayout(mvfxContext* context)
{
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = &position;
    def.fieldCount = 1;
    def.stride = 16;
    mvfxLayoutId layout;
    return mvfxCreateLayout(context, &def, &layout);
}

static mvfxResult DestroyLayout(mvfxContext* context)
{
    return mvfxDestroyLayout(context, s_changeLayout);
}

static mvfxResult WriteParticles(mvfxContext* context)
{
    return mvfxInstance_WriteParticles(context, s_instance, 0, s_changeLayout, &s_view, NULL, 0,
                                       s_out, 4, &s_counts[0]);
}

static mvfxResult WriteRibbons(mvfxContext* context)
{
    static mvfxRibbon ribbons[1];
    const mvfxRibbonBuffers list = {
        .points = s_out, .pointCapacity = 4, .ribbons = ribbons, .ribbonCapacity = 1};
    return mvfxInstance_WriteRibbons(context, s_instance, 0, s_changeLayout, &s_view, &list,
                                     &s_counts[0], &s_counts[1]);
}

static mvfxResult ExpandSprites(mvfxContext* context)
{
    return mvfxInstance_ExpandSprites(context, s_instance, 0, s_changeLayout, &s_view, &s_expand,
                                      &s_counts[0]);
}

static mvfxResult ExpandRibbons(mvfxContext* context)
{
    return mvfxInstance_ExpandRibbons(context, s_instance, 0, s_changeLayout, &s_view, &s_expand,
                                      &s_counts[0], &s_counts[1]);
}

static mvfxResult ExpandTubes(mvfxContext* context)
{
    static const mvfxTubeDef tube = {.sides = 3};
    return mvfxInstance_ExpandTubes(context, s_instance, 0, s_changeLayout, &s_view, &tube,
                                    &s_expand, &s_counts[0], &s_counts[1]);
}

static mvfxResult WritePlexus(mvfxContext* context)
{
    static mvfxRibbon ribbons[1];
    const mvfxRibbonBuffers list = {
        .points = s_out, .pointCapacity = 4, .ribbons = ribbons, .ribbonCapacity = 1};
    return mvfxInstance_WritePlexus(context, s_instance, 0, s_changeLayout, &s_view, &list,
                                    &s_counts[0], &s_counts[1]);
}

static mvfxResult ExpandPlexus(mvfxContext* context)
{
    return mvfxInstance_ExpandPlexus(context, s_instance, 0, s_changeLayout, &s_view, &s_expand,
                                     &s_counts[0], &s_counts[1]);
}

static mvfxResult ExpandSheets(mvfxContext* context)
{
    return mvfxInstance_ExpandSheets(context, s_instance, 0, s_changeLayout, &s_view, &s_expand,
                                     &s_counts[0], &s_counts[1]);
}

static mvfxResult WriteLights(mvfxContext* context)
{
    static mvfxLight lights[1];
    return mvfxInstance_WriteLights(context, s_instance, 0, &s_view, lights, 1, &s_counts[0]);
}

static mvfxResult GetBounds(mvfxContext* context)
{
    mvfxBounds bounds;
    return mvfxInstance_GetBounds(context, s_instance, -1, &bounds);
}

static mvfxResult OrderEmitters(mvfxContext* context)
{
    static mvfxDrawItem items[1];
    return mvfxContext_OrderEmitters(context, s_origin, NULL, items, 1, &s_counts[0]);
}

static mvfxResult WriteSurfaceMesh(mvfxContext* context)
{
    static float positions[3];
    const mvfxSurfaceMeshBuffers mesh = {
        .positions = positions, .vertexCapacity = 1, .indices = s_outIndices, .indexCapacity = 6};
    return mvfxInstance_WriteSurfaceMesh(context, s_instance, 0, s_origin, 0.0f, &mesh,
                                         &s_counts[0], &s_counts[1]);
}

static mvfxResult SetGasHolds(mvfxContext* context)
{
    return mvfxInstance_SetGasHolds(context, s_instance, 0, 0, true);
}

static mvfxResult SetLiquidSpeed(mvfxContext* context)
{
    return mvfxInstance_SetLiquidSpeed(context, s_instance, 0, 0, 1.0f);
}

static mvfxResult SetWaterHolds(mvfxContext* context)
{
    return mvfxInstance_SetWaterHolds(context, s_instance, 0, 0, true);
}

static mvfxResult SetWaterSpeed(mvfxContext* context)
{
    return mvfxInstance_SetWaterSpeed(context, s_instance, 0, 0, 1.0f);
}

static mvfxResult SetWaterCounts(mvfxContext* context)
{
    return mvfxInstance_SetWaterCounts(context, s_instance, 0, 0, 1.0f, 1, 1.0);
}

// Each call by its public name, which tools/check_host_calls.py reads.
static const struct
{
    Change call;
    const char* name;
} s_changes[] = {
    {Step, "mvfxContext_Step"},
    {StepTicks, "mvfxContext_StepTicks"},
    {CreateEffect, "mvfxCreateEffect"},
    {DestroyEffect, "mvfxDestroyEffect"},
    {CreateEffectFromBytes, "mvfxCreateEffectFromBytes"},
    {CreateGrid, "mvfxCreateGrid"},
    {DestroyGrid, "mvfxDestroyGrid"},
    {GridSetValues, "mvfxGrid_SetValues"},
    {GridSetValuesIn, "mvfxGrid_SetValuesIn"},
    {CreateInstance, "mvfxCreateInstance"},
    {DestroyInstance, "mvfxDestroyInstance"},
    {SetTransform, "mvfxInstance_SetTransform"},
    {SetVisible, "mvfxInstance_SetVisible"},
    {SetOffscreen, "mvfxInstance_SetOffscreen"},
    {Spawn, "mvfxInstance_Spawn"},
    {SetAttachFrames, "mvfxInstance_SetAttachFrames"},
    {Prewarm, "mvfxInstance_Prewarm"},
    {SetColliders, "mvfxInstance_SetColliders"},
    {GetSceneQueries, "mvfxInstance_GetSceneQueries"},
    {AnswerSceneQueries, "mvfxInstance_AnswerSceneQueries"},
    {SetDepthImage, "mvfxInstance_SetDepthImage"},
    {WakeBodies, "mvfxInstance_WakeBodies"},
    {GetEvents, "mvfxInstance_GetEvents"},
    {SetEmitterMesh, "mvfxInstance_SetEmitterMesh"},
    {SetParameter, "mvfxInstance_SetParameter"},
    {GetEmitterStats, "mvfxInstance_GetEmitterStats"},
    {Stop, "mvfxInstance_Stop"},
    {Restart, "mvfxInstance_Restart"},
    {BreakRibbons, "mvfxInstance_BreakRibbons"},
    {PushWater, "mvfxInstance_PushWater"},
    {CreateMesh, "mvfxCreateMesh"},
    {DestroyMesh, "mvfxDestroyMesh"},
    {MeshSetPositions, "mvfxMesh_SetPositions"},
    {CreateLayout, "mvfxCreateLayout"},
    {DestroyLayout, "mvfxDestroyLayout"},
    {WriteParticles, "mvfxInstance_WriteParticles"},
    {WriteRibbons, "mvfxInstance_WriteRibbons"},
    {ExpandSprites, "mvfxInstance_ExpandSprites"},
    {ExpandRibbons, "mvfxInstance_ExpandRibbons"},
    {ExpandTubes, "mvfxInstance_ExpandTubes"},
    {WritePlexus, "mvfxInstance_WritePlexus"},
    {ExpandPlexus, "mvfxInstance_ExpandPlexus"},
    {ExpandSheets, "mvfxInstance_ExpandSheets"},
    {WriteLights, "mvfxInstance_WriteLights"},
    {GetBounds, "mvfxInstance_GetBounds"},
    {OrderEmitters, "mvfxContext_OrderEmitters"},
    {WriteSurfaceMesh, "mvfxInstance_WriteSurfaceMesh"},
    {SetGasHolds, "mvfxInstance_SetGasHolds"},
    {SetLiquidSpeed, "mvfxInstance_SetLiquidSpeed"},
    {SetWaterHolds, "mvfxInstance_SetWaterHolds"},
    {SetWaterSpeed, "mvfxInstance_SetWaterSpeed"},
    {SetWaterCounts, "mvfxInstance_SetWaterCounts"},
};

enum
{
    CHANGES = (int32_t)(sizeof s_changes / sizeof s_changes[0])
};

// Each call's result from the host module, and the misuse it counted.
static mvfxResult s_changed[CHANGES];
static uint64_t s_changeMisuse[CHANGES];

static void ChangeBack(const mvfxModuleCall* call, void* user)
{
    (void)call;
    (void)user;
    mvfxContext* target = s_target;
    if (target == NULL)
    {
        return;
    }
    s_target = NULL;
    for (int32_t c = 0; c < CHANGES; ++c)
    {
        uint64_t before = mvfxContext_GetMisuse(target);
        s_changed[c] = s_changes[c].call(target);
        s_changeMisuse[c] = mvfxContext_GetMisuse(target) - before;
    }
}

static const mvfxHostModule s_changeBack = {.stage = mvfx_stageAfterMotion, .function = ChangeBack};

static void TestChangesRefused(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.hostModules = &s_changeBack;
    emitter.hostModuleCount = 1;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    CHECK(mvfxCreateEffect(context, &effectDef, &s_effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s_effect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.values = s_gridValues;
    CHECK(mvfxCreateGrid(context, &gridDef, &s_grid) == mvfx_success, "grid");
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = s_corners;
    meshDef.vertexCount = 4;
    CHECK(mvfxCreateMesh(context, &meshDef, &s_mesh) == mvfx_success, "mesh");
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &position;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 16;
    CHECK(mvfxCreateLayout(context, &layoutDef, &s_changeLayout) == mvfx_success, "layout");
    for (int32_t c = 0; c < CHANGES; ++c)
    {
        s_changed[c] = mvfx_success;
        s_changeMisuse[c] = 0;
    }
    s_target = context;
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success && s_target == NULL,
          "the host module called back in");
    for (int32_t c = 0; c < CHANGES; ++c)
    {
        if (s_changed[c] != mvfx_errorBusy || s_changeMisuse[c] != 1)
        {
            printf("%s from a host module: %d, %llu misuse\n", s_changes[c].name, (int)s_changed[c],
                   (unsigned long long)s_changeMisuse[c]);
        }
        CHECK(s_changed[c] == mvfx_errorBusy && s_changeMisuse[c] == 1,
              "a change from a host module refused as busy and counted");
    }
    // Nothing the refused calls named was destroyed or replaced.
    static mvfxEmitterView emitterView;
    mvfxGridView gridView;
    mvfxMeshView meshView;
    mvfxLayoutView layoutView;
    int32_t level = -1;
    CHECK(mvfxEffect_GetEmitterView(context, s_effect, 0, &emitterView) == mvfx_success &&
              mvfxInstance_GetQuality(context, s_instance, &level) == mvfx_success &&
              mvfxGrid_GetView(context, s_grid, &gridView) == mvfx_success &&
              mvfxMesh_GetView(context, s_mesh, &meshView) == mvfx_success &&
              mvfxLayout_GetView(context, s_changeLayout, &layoutView) == mvfx_success,
          "the objects the refused calls named still there");
    mvfxDestroyContext(context);
}

static void TestHostSettings(void)
{
    // An effect's compile allocates inside the library's environment, a
    // mesh's hull frees its scratch inside it as the mesh is made,
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
    body.rigid = (mvfxRigidDef){.enable = true,
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
    stepped = stepped && mvfxCreateEffect(context, &bodies, &hulled, NULL) == mvfx_success;
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

// A context with the counted allocator; a mesh, a grid and a layout; an
// effect spawning on the mesh and an instance of it: their ids for Read.
static mvfxContext* OpenObjects(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.allocator = (mvfxAllocator){.alloc = Alloc, .free = Free};
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    static const float corners[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static const uint32_t faces[4][3] = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = &corners[0][0];
    meshDef.vertexCount = 4;
    meshDef.triangles = &faces[0][0];
    meshDef.triangleCount = 4;
    CHECK(mvfxCreateMesh(context, &meshDef, &s_readMesh) == mvfx_success, "mesh");
    static const float values[8] = {0};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.components = 1;
    gridDef.values = values;
    CHECK(mvfxCreateGrid(context, &gridDef, &s_readGrid) == mvfx_success, "grid");
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &position;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 16;
    CHECK(mvfxCreateLayout(context, &layoutDef, &s_readLayout) == mvfx_success, "layout");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.shape.kind = mvfx_shapeMesh;
    emitter.shape.mesh = s_readMesh;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    CHECK(mvfxCreateEffect(context, &effectDef, &s_readEffect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s_readEffect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &s_readInstance) == mvfx_success, "instance");
    mvfxMeshView view;
    CHECK(mvfxInstance_GetEmitterMesh(context, s_readInstance, 0, &view) == mvfx_success,
          "the instance spawning on the mesh");
    return context;
}

static void TestFreedAfterItsId(void)
{
    // The allocator's free reads every id after each block a destruction
    // returns: each destruction frees its object's slot before its
    // memory, and the context each table before its memory, so a reading
    // call finds the id gone rather than a returned block (AddressSanitizer
    // reports one). The mesh's hull, kept in its block, reads back whole
    // until the mesh's slot is freed and stale from then on.
    mvfxContext* context = OpenObjects();
    s_reads = 0;
    s_hullsStale = 0;
    s_hullsWrong = 0;
    s_reader = context;
    bool destroyed = mvfxDestroyInstance(context, s_readInstance) == mvfx_success &&
                     mvfxDestroyEffect(context, s_readEffect) == mvfx_success &&
                     mvfxDestroyMesh(context, s_readMesh) == mvfx_success &&
                     mvfxDestroyGrid(context, s_readGrid) == mvfx_success &&
                     mvfxDestroyLayout(context, s_readLayout) == mvfx_success;
    s_reader = NULL;
    mvfxMeshView view;
    CHECK(destroyed && s_reads >= 5 &&
              mvfxMesh_GetView(context, s_readMesh, &view) == mvfx_errorStale,
          "each object destroyed, its id read as it went");
    CHECK(s_hullsStale > 0 && s_hullsWrong == 0,
          "the mesh's hull read whole while it lived, stale once destroyed");
    mvfxDestroyContext(context);
    context = OpenObjects();
    s_reads = 0;
    s_reader = context;
    mvfxDestroyContext(context);
    s_reader = NULL;
    CHECK(s_reads >= 10, "a context destroyed with its objects, their ids read as they went");
}

int main(void)
{
    TestCallsBackIn();
    TestBusyLeavesBuffers();
    TestChangesRefused();
    TestHostSettings();
    TestFreedAfterItsId();
    return s_failures == 0 ? 0 : 1;
}
