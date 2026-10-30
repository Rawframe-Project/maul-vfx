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
// a block already returned. How many times it read.
static const mvfxContext* s_reader;
static mvfxMeshId s_readMesh;
static mvfxGridId s_readGrid;
static mvfxLayoutId s_readLayout;
static mvfxEffectId s_readEffect;
static mvfxInstanceId s_readInstance;
static int32_t s_reads;

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

// From a host module, each call that writes outputs, called back in:
// refused as busy and writing none of them, counts and single values as
// well as buffers, since any may be one the call in progress fills. The
// layout is live, so any other refusal would zero them.
static mvfxLayoutId s_layout;
static bool s_left;

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
    static unsigned char records[64];
    static unsigned char vertices[64];
    static uint32_t indices[6];
    static mvfxLight lights[1];
    static mvfxDrawItem items[1];
    static mvfxRibbon ribbons[1];
    static float positions[3];
    static mvfxBounds bounds;
    static mvfxEmitterStats stats;
    static float values[4];
    static float center[3];
    static bool hull;
    static int32_t level;
    static int32_t vertexCount;
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
    memset(values, 0xEE, sizeof values);
    memset(center, 0xEE, sizeof center);
    memset(&hull, 0xEE, sizeof hull);
    memset(&level, 0xEE, sizeof level);
    memset(&vertexCount, 0xEE, sizeof vertexCount);
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
    const mvfxMeshId m = {1, 1};
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
        mvfxInstance_GetSortScratchBytes(target, i, 0, &n[0]) == mvfx_errorBusy &&
        mvfxInstance_GetSurfaceMeshScratchBytes(target, i, 0, &n[1]) == mvfx_errorBusy &&
        mvfxInstance_GetBounds(target, i, -1, &bounds) == mvfx_errorBusy &&
        mvfxInstance_GetEmitterStats(target, i, 0, &stats) == mvfx_errorBusy &&
        mvfxInstance_GetParameter(target, i, 0, values) == mvfx_errorBusy &&
        mvfxInstance_GetQuality(target, i, &level) == mvfx_errorBusy &&
        mvfxInstance_GetEvents(target, i, 0, &events, &eventCount) == mvfx_errorBusy &&
        mvfxInstance_GetSceneQueries(target, i, 0, &queries, &queryCount) == mvfx_errorBusy &&
        mvfxInstance_Prewarm(target, i, 1.0, 0.1, 4, &remaining) == mvfx_errorBusy &&
        mvfxMesh_GetHullCenter(target, m, &hull, center) == mvfx_errorBusy &&
        mvfxMesh_GetHullVertexCount(target, m, &vertexCount) == mvfx_errorBusy;
    s_left = busy && Filled(n, sizeof n, 0xEE) && Filled(records, sizeof records, 0xEE) &&
             Filled(vertices, sizeof vertices, 0xEE) && Filled(indices, sizeof indices, 0xEE) &&
             Filled(lights, sizeof lights, 0xEE) && Filled(items, sizeof items, 0xEE) &&
             Filled(ribbons, sizeof ribbons, 0xEE) && Filled(positions, sizeof positions, 0xEE) &&
             Filled(&bounds, sizeof bounds, 0xEE) && Filled(&stats, sizeof stats, 0xEE) &&
             Filled(values, sizeof values, 0xEE) && Filled(center, sizeof center, 0xEE) &&
             Filled(&hull, sizeof hull, 0xEE) && Filled(&level, sizeof level, 0xEE) &&
             Filled(&vertexCount, sizeof vertexCount, 0xEE) &&
             Filled(&remaining, sizeof remaining, 0xEE) &&
             Filled(&eventCount, sizeof eventCount, 0xEE) &&
             Filled(&queryCount, sizeof queryCount, 0xEE) && events == (const mvfxEvent*)records &&
             queries == (const mvfxSceneQuery*)records;
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
    s_target = context;
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success && s_target == NULL && s_left,
          "a host module's calls back in refused as busy, writing none of their outputs");
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
    // reports one).
    mvfxContext* context = OpenObjects();
    s_reads = 0;
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
    TestHostSettings();
    TestFreedAfterItsId();
    return s_failures == 0 ? 0 : 1;
}
