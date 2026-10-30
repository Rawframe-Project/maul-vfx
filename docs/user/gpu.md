# The GPU part

`maul-vfx-rhi` runs effects on the GPU through [Maul RHI](https://github.com/Rawframe-Project/maul-rhi), on any device Maul RHI opens: Vulkan, Direct3D 12, Metal or WebGPU. It is a separate static library, built with `-DMAUL_VFX_RHI=ON` and linked as `maul-vfx-rhi::maul-vfx-rhi`; it finds Maul RHI installed or fetches its release. It takes the Maul RHI version pinned in `cmake/Rhi.cmake` (`MAUL_VFX_RHI_VERSION`), and a host that uses Maul RHI itself builds against that version, as it hands the GPU part its own device. Its kernels come compiled for every backend, so a build needs no shader tools. The core never depends on it.

## Contents

- [How it fits](#how-it-fits)
- [What matches the CPU path](#what-matches-the-cpu-path)
- [What it does not run](#what-it-does-not-run)
- [Budgets](#budgets)
- [Depth and meshes](#depth-and-meshes)

## How it fits

The CPU path stays in charge of what happens: a **planned** instance (`mvfxInstanceDef.planned`) steps in its context like any other, but holds no particles. Each step it writes plans (`maul-vfx/plan.h`): the births the step owes with their serials and ticks, the program's births, the forces, colliders and moves resolved for the step, from the same arithmetic as the CPU path. A **simulation** (`mvfxGpuSim`) holds the instance's particles, gases and water surfaces in device buffers and runs each plan in compute passes.

The program builds each frame as it does for its own rendering:

1. `mvfxContext_StepTicks` steps the context, planned instances included.
2. After `mrhiDevice_BeginFrame`, `mvfxGpuSim_AddPasses` adds the step's passes to the frame, and the output passes the program asks for follow (records, lights, ribbons, sprites, gas and water reads and samples, surface meshes).
3. After `mrhiDevice_CompileFrame`, `mvfxGpuSim_Record` records them.

Nothing is read back within a step. Each emitter keeps a statistics record (its live count, its bounds and its capacity counts) on the device, which the program reads back as late as its frames lag; `mvfxGpuSim_GetBounds` maps it to bounds in the world. A program reading an emitter's buffers directly (`mvfxGpuSim_GetParticles`) reads only what the record counts: the first live count of the particles, and the events and links the record says were written; the slots and entries past those counts are undefined, and so is every buffer until the simulation's first step has run. Gases and water surfaces have records too, which the program hands back (`mvfxGpuSim_ReportGas`, `mvfxGpuSim_ReportWater`) so the instance knows when it has finished.

A GPU object (`mvfxCreateGpu`) holds the part's kernels for one device. Their pipelines build asynchronously: the program passes the device's notifications to `mvfxGpu_Notify` until `mvfxGpu_IsReady`. Its def is a root, as the context's is: `mvfxDefaultGpuDef` is built into the program from the headers it includes and stamps their ABI version, so `mvfxCreateGpu` refuses a program built against another release with `mvfx_errorVersion`.

The fountain of [getting started](getting-started.md) on the device, with a device and a ready GPU object (`kernels`) in hand:

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 128;
emitter.rate = 50.0f;
emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                  .direction = {0.0f, 1.0f, 0.0f},
                                  .coneAngle = 0.2f,
                                  .speed = {3.0f, 4.0f}};
emitter.motion.gravity[1] = -9.8f;
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
// Planned: the context plans its steps, the simulation runs them.
instanceDef.effect = effect;
instanceDef.planned = true;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
simDef.gpu = kernels;
simDef.context = context;
simDef.effect = effect;
simDef.instance = instance;
mvfxGpuSim* sim = NULL;
mvfxGpuRefusal refusal;
made = made && mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success;

// A second of frames: the context steps, the simulation adds its passes
// to the frame and records them once it is compiled.
for (int32_t step = 0; made && step < 60; ++step)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiRequestId submitted = {0};
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
           mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
           mvfxGpuSim_AddPasses(sim) == mvfx_success &&
           mrhiDevice_CompileFrame(device) == mrhi_success &&
           mvfxGpuSim_Record(sim) == mvfx_success &&
           mrhiDevice_SubmitFrame(device, &submitted) == mrhi_success &&
           mrhiRequest_WaitFrame(device, submitted, UINT64_C(10000000000)) == mrhi_success;
    if (!made)
    {
        (void)mrhiDevice_DropFrame(device); // a frame left open by a failure
    }
    mrhiDeviceNotification note;
    while (mrhiDevice_TakeNotification(device, &note) == mrhi_success)
    {
        (void)mvfxGpu_Notify(kernels, &note);
    }
}

// The statistics record read back in a frame of its own (a host reads it
// in a later frame's pass, as late as its frames lag).
mvfxGpuParticles particles;
mrhiFrameDef frame = mrhiDefaultFrameDef();
mrhiResourceId statistics = {0};
mrhiPassId pass = {0};
mrhiRequestId read = {0};
mrhiRequestId submitted = {0};
made = made && mvfxGpuSim_GetParticles(sim, 0, &particles) == mvfx_success &&
       mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
       mrhiImportBuffer(device, particles.statistics, &statistics) == mrhi_success;
const mrhiAccess reading = {.resource = statistics, .kind = mrhi_accessCopySource};
mrhiPassDef passDef = mrhiDefaultPassDef();
passDef.passClass = mrhi_passTransfer;
passDef.accesses = &reading;
passDef.accessCount = 1;
passDef.neverCull = true;
static uint32_t record[MVFX_GPU_STATISTICS_WORDS];
made = made && mrhiAddPass(device, &passDef, &pass) == mrhi_success &&
       mrhiDevice_CompileFrame(device) == mrhi_success &&
       mrhiPass_Begin(device, pass) == mrhi_success &&
       mrhiPass_ReadBuffer(device, pass, statistics, 0, sizeof record, &read) == mrhi_success &&
       mrhiPass_End(device, pass) == mrhi_success &&
       mrhiDevice_SubmitFrame(device, &submitted) == mrhi_success &&
       mrhiRequest_WaitFrame(device, submitted, UINT64_C(10000000000)) == mrhi_success;
if (!made)
{
    (void)mrhiDevice_DropFrame(device);
}
mrhiDeviceNotification note;
while (mrhiDevice_TakeNotification(device, &note) == mrhi_success)
{
    (void)mvfxGpu_Notify(kernels, &note);
}
size_t taken = 0;
made = made &&
       mrhiRequest_TakeReadback(device, read, record, sizeof record, &taken) == mrhi_success;

// About 50 live, as on the CPU, and their bounds above the ground.
mvfxBounds bounds;
made = made && record[0] >= 45 && record[0] <= 55 &&
       mvfxGpuSim_GetBounds(sim, 0, record, &bounds) == mvfx_success && bounds.max[1] > 1.0;
mvfxDestroyGpuSim(sim);
mvfxDestroyContext(context);
return made;
```

## What matches the CPU path

The simulation numbers, draws and ends its particles as the CPU path does: its integer outputs (counts, serials, birth and death ticks) are the CPU path's. Floating-point results are the CPU path's within stated tolerances, since the device's arithmetic is not the CPU's; the headers state each output's tolerance. A device may flush subnormals to zero, as WGSL allows, so there a subnormal value, an angle for one, reads as zero. On one device and driver the GPU path gives the same bits run after run.

## What it does not run

`mvfxCreateGpuSim` refuses an effect it cannot run, with the first emitter, gas or water surface at fault and the features why (`mvfxGpuRefusal`, the `MVFX_FEATURE_*` bits of `plan.h`):

- **Host modules and scene queries**, which are the program's code and its scene, on the CPU.
- **Contacts beside anything that kills within a step** (kill rules, colliders that kill, an attractor's kill radius), and **contacts beside ribbons and beams**, whose order the CPU path's contacts depend on.
- **Rigid particles beside anything that kills within a step.**

Rigid bodies that meet each other step in one workgroup, as the CPU path solves them in order, so they use one compute unit of the device.

## Budgets

A simulation keeps its memory on the device from its creation: each emitter's buffers at its capacity, room for the instance's grids (`hostGridBytes`, sized with `mvfxContext_GetGpuHostGridBytes`) and meshes (`hostMeshBytes`, `mvfxMesh_GetGpuBytes`). A mesh or a grid too large for that room is refused when it is set, not every step after.

An emitter's step adds a few passes, more with neighbors, contacts, units and liquids; a liquid of many substeps and iterations can add thousands. `mvfxGpuSim_GetEmitterPasses`, `mvfxGpuSim_GetGasPasses` and `mvfxGpuSim_GetWaterPasses` give the most each adds, and `mvfxGpuSim_GetUploadBytes` the bytes a frame uploads, so a program can size Maul RHI's frame limits (`framePasses`, `frameUploadBytes`) to its effects.

## Depth and meshes

An emitter that collides with the depth image reads the depth texture the program rendered, given each frame with `mvfxGpuSim_SetDepthTextures`; the planned instance's depth image then needs no pixels. A mesh shape's mesh is uploaded with the simulation and replaced with `mvfxGpuSim_SetEmitterMesh`.
