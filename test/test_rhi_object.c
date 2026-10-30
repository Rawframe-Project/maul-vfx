// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's object on Maul RHI's test driver, which every build of
// the part's tests has: each refusal of mvfxCreateGpu with a device at
// hand, its memory failing, a device failing an object, the object not
// ready before its pipelines' notifications, a pipeline that fails,
// notifications that are not its own, and its shaders and pipelines all
// returned, on a device that holds no more than one object's; and a
// simulation on a device failing its buffers.

#include "kernels.h"
#include "test_harness.h"

#include "maul-rhi/instance.h"
#include "maul-rhi/test.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"

#include <stdlib.h>
#include <string.h>

static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// The allocator the next device opened takes (the C library's for a
// zeroed one), and how many GPU objects' shaders and pipelines it holds.
static mrhiAllocator s_deviceAllocator;
static uint32_t s_objectsHeld = 1;

// A device on the test driver, its pipelines answered with an outcome,
// failing every object after objects (0 for none), holding the shaders
// and pipelines of s_objectsHeld GPU objects alone.
static bool Open(Gpu* gpu, mrhiResult pipelines, uint32_t objects)
{
    *gpu = (Gpu){0};
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
        .pipelineOutcome = pipelines,
        .objectsBeforeFailure = objects,
    };
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = NULL, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiInstance_RequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiInstance_TakeNotification(gpu->instance, &record) != mrhi_success ||
        mrhiInstance_GetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count != 1)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    deviceDef.allocator = s_deviceAllocator;
    deviceDef.budgets.shaders = s_objectsHeld * MVFX_CONTAINER_COUNT;
    deviceDef.budgets.pipelines = s_objectsHeld * MVFX_KERNEL_COUNT;
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           mrhiInstance_TakeNotification(gpu->instance, &record) == mrhi_success &&
           record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success;
}

// A device on the test driver opened, its ready notification not yet
// taken, so not ready.
static bool OpenUnready(Gpu* gpu)
{
    *gpu = (Gpu){0};
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = NULL, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiInstance_RequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiInstance_TakeNotification(gpu->instance, &record) != mrhi_success ||
        mrhiInstance_GetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count != 1)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success;
}

static void Close(Gpu* gpu)
{
    if (gpu->device != NULL)
    {
        mrhiDestroyDevice(gpu->device);
    }
    if (gpu->instance != NULL)
    {
        mrhiDestroyInstance(gpu->instance);
    }
}

// The object's memory: the bytes held counted, the calls counted,
// allocation number s_failAt failing (from 1, 0 for none), every block
// handed out filled with a pattern, so a field never written reads as
// garbage. Blocks are aligned by hand, as not every C library has
// aligned_alloc.
static int64_t s_held;
static int32_t s_calls;
static int32_t s_failAt;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)context;
    if (++s_calls == s_failAt)
    {
        return NULL;
    }
    uint8_t* raw = malloc(size + alignment + sizeof(void*));
    if (raw == NULL)
    {
        return NULL;
    }
    uintptr_t at = ((uintptr_t)(raw + sizeof(void*)) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)at)[-1] = raw;
    memset((void*)at, 0xA5, size);
    s_held += (int64_t)size;
    return (void*)at;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_held -= (int64_t)size;
    free(((void**)memory)[-1]);
}

static mvfxGpuDef CountedDef(mrhiDevice* device)
{
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = device;
    def.allocator = (mvfxAllocator){.alloc = Allocate, .free = Free};
    return def;
}

// Hands the object every notification the device has; how many were its
// own. The last of its own is kept in mine.
static int32_t Pump(Gpu* gpu, mvfxGpu* kernels, mrhiDeviceNotification* mine)
{
    int32_t its = 0;
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(gpu->device, &record) == mrhi_success)
    {
        if (mvfxGpu_Notify(kernels, &record))
        {
            *mine = record;
            ++its;
        }
    }
    return its;
}

static void TestRefusals(Gpu* gpu)
{
    s_calls = 0;
    mvfxGpu* kernels = (mvfxGpu*)&kernels;
    mvfxGpuDef def = CountedDef(gpu->device);
    bool refused = mvfxCreateGpu(NULL, &kernels) == mvfx_errorInvalid && kernels == NULL &&
                   mvfxCreateGpu(&def, NULL) == mvfx_errorInvalid;
    def.cookie = 0;
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    def = CountedDef(NULL);
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    def = CountedDef(gpu->device);
    def.allocator.free = NULL;
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    def = CountedDef(gpu->device);
    def.allocator.alloc = NULL;
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    CHECK(refused && kernels == NULL && s_calls == 0, "each refusal, nothing allocated");
    CHECK(!mvfxGpu_IsReady(NULL) && !mvfxGpu_Notify(NULL, NULL), "NULL objects");
    mvfxDestroyGpu(NULL);

    s_failAt = 1;
    def = CountedDef(gpu->device);
    kernels = (mvfxGpu*)&kernels;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_errorCapacity && kernels == NULL && s_held == 0,
          "memory refused");
    s_failAt = 0;
}

// Notifications that are not the object's own: another kind, another
// request's generation or index, a failed build.
static void TestNotifications(mvfxGpu* kernels, const mrhiDeviceNotification* mine)
{
    CHECK(!mvfxGpu_Notify(kernels, NULL), "no notification");
    mrhiDeviceNotification other = *mine;
    other.kind = mrhi_devicePipelineReady + 1;
    bool ignored = !mvfxGpu_Notify(kernels, &other);
    other = *mine;
    other.request.generation += 1;
    ignored = ignored && !mvfxGpu_Notify(kernels, &other);
    other = *mine;
    other.request.index1 = 0xFFFFFFF0u;
    ignored = ignored && !mvfxGpu_Notify(kernels, &other);
    CHECK(ignored && mvfxGpu_IsReady(kernels), "others' notifications ignored");
    other = *mine;
    other.outcome = mrhi_errorPlatform;
    CHECK(mvfxGpu_Notify(kernels, &other) && !mvfxGpu_IsReady(kernels), "a failed build");
    CHECK(mvfxGpu_Notify(kernels, mine) && mvfxGpu_IsReady(kernels), "built again");
}

static void TestLife(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success, 0), "a device");
    TestRefusals(&gpu);
    // Three objects in turn on a device that holds one's shaders and
    // pipelines: each returns them.
    for (int32_t round = 0; round < 3; ++round)
    {
        mvfxGpuDef def = CountedDef(gpu.device);
        mvfxGpu* kernels = NULL;
        CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "created");
        CHECK(!mvfxGpu_IsReady(kernels), "not ready before its notifications");
        mrhiDeviceNotification mine = {0};
        CHECK(Pump(&gpu, kernels, &mine) == MVFX_KERNEL_COUNT && mvfxGpu_IsReady(kernels),
              "ready on its own notifications");
        if (round == 0)
        {
            TestNotifications(kernels, &mine);
        }
        mvfxDestroyGpu(kernels);
        CHECK(s_held == 0, "its memory returned");
    }
    Close(&gpu);

    // The C library's memory, by a zeroed allocator.
    CHECK(Open(&gpu, mrhi_success, 0), "a device");
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "on the C library's memory");
    mvfxDestroyGpu(kernels);
    Close(&gpu);
}

// The root def's checks, in order: the cookie first, then the ABI
// version, then the rest, so that a def of another layout is refused
// before any field after the first two is read.
static void TestDefChecks(void)
{
    mvfxGpu* kernels = (mvfxGpu*)&kernels;
    mvfxGpuDef zeroed = {0};
    zeroed.version = MVFX_ABI_VERSION + 1;
    CHECK(mvfxCreateGpu(&zeroed, &kernels) == mvfx_errorInvalid && kernels == NULL,
          "a def without its cookie refused before its version is read");
    mvfxGpuDef other = mvfxDefaultGpuDef();
    other.version = MVFX_ABI_VERSION + 1;
    CHECK(mvfxCreateGpu(&other, &kernels) == mvfx_errorVersion,
          "a def of other headers refused before its missing device is read");
    other.version = 0;
    CHECK(mvfxCreateGpu(&other, &kernels) == mvfx_errorVersion, "a zeroed version refused");
    mvfxGpuDef stamped = mvfxDefaultGpuDef();
    CHECK(stamped.version == MVFX_ABI_VERSION &&
              mvfxCreateGpu(&stamped, &kernels) == mvfx_errorInvalid,
          "the default stamps these headers, and still needs a device");
}

static void TestFailures(void)
{
    // A pipeline the device fails to build: answered, never ready.
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_errorPlatform, 0), "a device failing pipelines");
    mvfxGpuDef def = CountedDef(gpu.device);
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "created");
    mrhiDeviceNotification mine = {0};
    CHECK(Pump(&gpu, kernels, &mine) == MVFX_KERNEL_COUNT && !mvfxGpu_IsReady(kernels),
          "a failed pipeline answered, not ready");
    mvfxDestroyGpu(kernels);
    Close(&gpu);

    // A device failing its objects after a few: refused with the device's
    // status (the test driver's is the platform error) and all returned, until the device makes the
    // shaders and the first requests; made then, but ready only once it makes every kernel's
    // pipeline (the later ones requested as the first are answered).
    int32_t refusedCount = 0;
    int32_t madeCount = 0;
    bool readyAsMade = true;
    for (uint32_t objects = 1; objects <= MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT + 2; ++objects)
    {
        CHECK(Open(&gpu, mrhi_success, objects), "a device failing objects");
        def = CountedDef(gpu.device);
        kernels = (mvfxGpu*)&kernels;
        mvfxResult result = mvfxCreateGpu(&def, &kernels);
        refusedCount += result == mvfx_errorPlatform && kernels == NULL && s_held == 0;
        madeCount += result == mvfx_success;
        if (result == mvfx_success)
        {
            mrhiDeviceNotification last = {0};
            (void)Pump(&gpu, kernels, &last);
            bool every = objects >= MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT;
            readyAsMade = readyAsMade && mvfxGpu_IsReady(kernels) == every;
        }
        mvfxDestroyGpu(result == mvfx_success ? kernels : NULL);
        CHECK(s_held == 0, "its memory returned");
        Close(&gpu);
    }
    CHECK(refusedCount > 0 && madeCount > 0 &&
              refusedCount + madeCount == MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT + 2,
          "refused while the device fails, made once it does not");
    CHECK(readyAsMade && refusedCount < MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT,
          "made with its first requests, ready once every pipeline is");
}

// A simulation on a device that fails its objects a few past the GPU
// object's: refused with the device's status, the test driver's platform
// error, its memory returned, until the device makes every buffer; and
// with each of its allocations failing in turn, refused with the capacity
// error, its memory returned.
static void TestSimFailures(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 16;
    // A gas too, whose passes' host arrays are allocated while its
    // buffers are made.
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 8;
    gas.size[1] = 8;
    gas.size[2] = 8;
    gas.cellSize = 0.1f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    int32_t refused = 0;
    int32_t made = 0;
    bool answered = true;
    for (uint32_t more = 0; made == 0 && more < 1024; ++more)
    {
        Gpu gpu;
        CHECK(Open(&gpu, mrhi_success, MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT + more),
              "a device failing buffers");
        mvfxGpuDef def = CountedDef(gpu.device);
        mvfxGpu* kernels = NULL;
        mrhiDeviceNotification mine = {0};
        bool ready = mvfxCreateGpu(&def, &kernels) == mvfx_success &&
                     Pump(&gpu, kernels, &mine) == MVFX_KERNEL_COUNT && mvfxGpu_IsReady(kernels);
        CHECK(ready, "its kernels made");
        mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
        simDef.gpu = kernels;
        simDef.context = context;
        simDef.effect = effect;
        simDef.instance = instance;
        simDef.allocator = def.allocator;
        int64_t held = s_held;
        mvfxGpuSim* sim = (mvfxGpuSim*)&sim;
        mvfxResult result = ready ? mvfxCreateGpuSim(&simDef, &sim, NULL) : mvfx_errorInvalid;
        made += result == mvfx_success ? 1 : 0;
        refused += result == mvfx_errorPlatform && sim == NULL && s_held == held ? 1 : 0;
        answered = answered && (result == mvfx_success || result == mvfx_errorPlatform);
        mvfxDestroyGpuSim(result == mvfx_success ? sim : NULL);
        mvfxDestroyGpu(kernels);
        Close(&gpu);
    }
    CHECK(refused > 0 && made == 1 && answered,
          "refused with the platform error while the device fails, made once it does not");
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success, 0), "a device");
    mvfxGpuDef def = CountedDef(gpu.device);
    mvfxGpu* kernels = NULL;
    mrhiDeviceNotification mine = {0};
    bool ready = mvfxCreateGpu(&def, &kernels) == mvfx_success &&
                 Pump(&gpu, kernels, &mine) == MVFX_KERNEL_COUNT && mvfxGpu_IsReady(kernels);
    CHECK(ready, "its kernels made");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    simDef.allocator = def.allocator;
    refused = 0;
    made = 0;
    answered = true;
    for (int32_t failing = 1; ready && made == 0 && failing < 1024; ++failing)
    {
        int64_t held = s_held;
        s_calls = 0;
        s_failAt = failing;
        mvfxGpuSim* sim = (mvfxGpuSim*)&sim;
        mvfxResult result = mvfxCreateGpuSim(&simDef, &sim, NULL);
        s_failAt = 0;
        made += result == mvfx_success ? 1 : 0;
        refused += result == mvfx_errorCapacity && sim == NULL && s_held == held ? 1 : 0;
        answered = answered && (result == mvfx_success || result == mvfx_errorCapacity);
        mvfxDestroyGpuSim(result == mvfx_success ? sim : NULL);
    }
    CHECK(refused > 1 && made == 1 && answered,
          "refused with the capacity error while its allocator fails, made once it does not");
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    mvfxDestroyContext(context);
}

// A device not ready yet refuses the GPU object's shaders with the state
// error, which the device's ready notification changes: made then.
static void TestOpening(void)
{
    Gpu gpu;
    CHECK(OpenUnready(&gpu), "a device opening");
    mvfxGpuDef def = CountedDef(gpu.device);
    mvfxGpu* kernels = (mvfxGpu*)&kernels;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_errorState && kernels == NULL && s_held == 0,
          "refused with the state error before the device is ready");
    mrhiInstanceNotification record;
    CHECK(mrhiInstance_TakeNotification(gpu.instance, &record) == mrhi_success &&
              record.kind == mrhi_instanceDeviceReady,
          "the device ready");
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "made once it is");
    mvfxDestroyGpu(kernels);
    Close(&gpu);
}

// What the device's allocator calls once, from inside its next call:
// the GPU part's creations on its own device, and their answers.
static mvfxGpuDef s_innerGpu;
static mvfxGpuSimDef s_innerSim;
static bool s_inside;
static mvfxResult s_innerGpuResult;
static mvfxResult s_innerSimResult;
static bool s_innerNull;

static void CallInside(void)
{
    if (!s_inside)
    {
        return;
    }
    s_inside = false;
    mvfxGpu* kernels = (mvfxGpu*)&kernels;
    s_innerGpuResult = mvfxCreateGpu(&s_innerGpu, &kernels);
    mvfxGpuSim* sim = (mvfxGpuSim*)&sim;
    s_innerSimResult = mvfxCreateGpuSim(&s_innerSim, &sim, NULL);
    s_innerNull = kernels == NULL && sim == NULL;
}

static void* DeviceAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    CallInside();
    return malloc(size);
}

static void DeviceFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

// The GPU object and a simulation made from inside the device's allocator,
// while it makes another GPU object's shaders: the device refuses them as
// busy, and so do they, their memory returned; the object being made is
// made.
static void TestBusy(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 16;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    s_deviceAllocator = (mrhiAllocator){.alloc = DeviceAlloc, .free = DeviceFree};
    s_objectsHeld = 2;
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success, 0), "a device on an allocator of its own, for two objects");
    s_deviceAllocator = (mrhiAllocator){0};
    s_objectsHeld = 1;
    mvfxGpuDef def = CountedDef(gpu.device);
    mvfxGpu* kernels = NULL;
    mrhiDeviceNotification mine = {0};
    bool ready = mvfxCreateGpu(&def, &kernels) == mvfx_success &&
                 Pump(&gpu, kernels, &mine) == MVFX_KERNEL_COUNT && mvfxGpu_IsReady(kernels);
    CHECK(ready, "its kernels made");
    s_innerGpu = def;
    s_innerSim = mvfxDefaultGpuSimDef();
    s_innerSim.gpu = kernels;
    s_innerSim.context = context;
    s_innerSim.effect = effect;
    s_innerSim.instance = instance;
    s_innerSim.allocator = def.allocator;
    int64_t held = s_held;
    s_inside = true;
    mvfxGpu* other = NULL;
    mvfxResult made = mvfxCreateGpu(&def, &other);
    CHECK(!s_inside && made == mvfx_success, "the object being made made");
    CHECK(s_innerGpuResult == mvfx_errorBusy && s_innerSimResult == mvfx_errorBusy && s_innerNull,
          "made from inside the device's allocator: busy");
    mvfxDestroyGpu(other);
    CHECK(s_held == held, "their memory returned");
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    mvfxDestroyContext(context);
}

int main(void)
{
    TestDefChecks();
    TestLife();
    TestFailures();
    TestSimFailures();
    TestOpening();
    TestBusy();
    return s_failures == 0 ? 0 : 1;
}
