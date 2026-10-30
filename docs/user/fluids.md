# Fluids

An effect holds gases and water surfaces beside its emitters (`mvfxEffectDef.gases`, `.waters`), each stepped with its instance. The library simulates them and gives the program their fields as data; it draws no volume and no water.

## Contents

- [Gases](#gases)
- [Liquids](#liquids)
- [Water surfaces](#water-surfaces)
- [Costs](#costs)

## Gases

A gas (`mvfxGasDef`) is a box of cells, 2D or 3D, holding a velocity at the cells' faces and density, temperature and fuel at their centers. Its box lies in the instance's frame and moves with it (a torch's flame), or stays in the world where the instance began and is left behind as it moves (smoke). Walls are closed or open per side. Its kinds:

- **Smoke and fire** (`mvfx_gasSmoke`): sources add density, heat and fuel; the velocity carries everything, itself included (Stam's stable fluids, semi-Lagrangian or with MacCormack's correction); heat and density lift or sink; vortex confinement keeps small curls alive; fuel burns into heat, smoke and expansion; obstacles push and block; each field may decay. A pressure solve keeps the flow from compressing.
- **A liquid's grid** (`mvfx_gasLiquid`): the grid a liquid emitter solves on; see [liquids](#liquids).
- **A surface** (`mvfx_gasSurface`): the signed distance to the surface of an emitter's particles, made anew each step, for meshing.

A cell's edge is at least `MVFX_MIN_GAS_CELL` (1e-6 of the instance's lengths). Each substep holds every face's velocity within `MVFX_MAX_SPEED` in the box's lengths, after the forces and after the pressure solve, and a burn's expansion within what such faces carry out of a cell, as a moved particle's speed is held: buoyancy, confinement and expansion have no bound of their own, and only such extremes reach it.

Particles meet gases two ways: a gas force (`mvfx_forceGas`) makes them follow its velocity, as embers ride smoke, and `gasSplat` pours their density, heat, fuel and push into it, as a jet feeds a fire.

Smoke from a hot source rises:

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
// A 2D gas of 64 by 64 cells of 0.1 about the origin, a hot source low in
// it, heat lifting against gravity.
static const mvfxGasSource source = {.shape = mvfx_gasSphere,
                                     .center = {0.0f, -2.5f, 0.0f},
                                     .radius = 0.4f,
                                     .density = 1.0f,
                                     .temperature = 5.0f};
mvfxGasDef gas = mvfxDefaultGasDef();
gas.sources = &source;
gas.sourceCount = 1;
gas.heatLift = 1.0f;
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.gases = &gas;
effectDef.gasCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
for (int32_t step = 0; made && step < 60; ++step)
{
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
}

// Above the source, and off to its side.
static const double points[6] = {0.0, -1.5, 0.0, 2.5, -2.5, 0.0};
float density[2] = {0.0f, 0.0f};
float rise[2] = {0.0f, 0.0f};
made = made &&
       mvfxInstance_SampleGas(context, instance, 0, mvfx_gasDensity, points, 2, density) ==
           mvfx_success &&
       mvfxInstance_SampleGas(context, instance, 0, mvfx_gasVelocityY, points, 2, rise) ==
           mvfx_success;
// The smoke went up, not sideways.
made = made && density[0] > 0.01f && rise[0] > 0.0f && density[1] < MVFX_GAS_EMPTY;
mvfxDestroyContext(context);
return made;
```

The program reads a field over a box of cells (`mvfxInstance_ReadGas`, or `mvfxInstance_ReadGasAs` straight into a texture's format such as `unorm8`) and samples fields at points in the world (`mvfxInstance_SampleGas`) to light a scene or move its own objects. `mvfxInstance_GetGasInfo` gives the box's place in the world and its cell counts.

## Liquids

An emitter whose `liquid` def is set is a liquid, by one of two solvers:

- **Particles** (`mvfx_liquidParticles`): position-based fluids (Macklin and Muller 2013). The particles hold their rest density among their neighbors by iterations on their places, with viscosity and vorticity confinement. It needs no gas.
- **A grid** (`mvfx_liquidGrid`): APIC (Jiang et al. 2015). The particles give a liquid gas their velocities, the gas makes them incompressible, and gives them back with each particle's local swirl. A fast flow may make the instance take substeps, each a whole step of the instance; events and plans count them.

A surface gas wraps the particles of a liquid (or any emitter), and `mvfxInstance_WriteSurfaceMesh` meshes it at a level of the distance by marching cubes, or marching squares in 2D. `mvfxInstance_GetSurfaceMeshScratchBytes` gives the scratch it needs. The mesh has no cracks, and its winding faces out.

A disc of particles wrapped by a surface gas and meshed: in 2D the mesh is segments, closed loops of them, so each vertex begins one segment and ends another.

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
// 400 particles at rest on a disc of radius 1.
static const mvfxBurst burst = {
    .minCount = 400, .maxCount = 400, .cycles = 1, .probability = 1.0f};
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 400;
emitter.rate = 0.0f;
emitter.bursts = &burst;
emitter.burstCount = 1;
emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
emitter.shape.kind = mvfx_shapeCircle;
emitter.shape.radius = 1.0f;
emitter.shape.thickness = 1.0f;
// Their surface in a 2D gas, each particle of radius 0.1.
mvfxGasDef gas = mvfxDefaultGasDef();
gas.kind = mvfx_gasSurface;
gas.surface.emitter = 0;
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
effectDef.gases = &gas;
effectDef.gasCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
made = made && mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;

int64_t scratchBytes = 0;
made = made && mvfxInstance_GetSurfaceMeshScratchBytes(context, instance, 0, &scratchBytes) ==
                   mvfx_success;
static uint32_t scratch[1024];
static float positions[512][3];
static uint32_t indices[1024];
const mvfxSurfaceMeshBuffers buffers = {.positions = &positions[0][0],
                                        .vertexCapacity = 512,
                                        .indices = indices,
                                        .indexCapacity = 1024,
                                        .scratch = scratch,
                                        .scratchBytes = sizeof scratch};
const double origin[3] = {0.0, 0.0, 0.0};
int64_t vertices = 0;
int64_t indexCount = 0;
made = made && scratchBytes <= (int64_t)sizeof scratch &&
       mvfxInstance_WriteSurfaceMesh(context, instance, 0, origin, 0.0f, &buffers, &vertices,
                                     &indexCount) == mvfx_success;
// A closed loop, every vertex near the disc's rim.
bool rim = made && vertices > 20 && vertices <= 512 && indexCount == 2 * vertices;
for (int64_t i = 0; rim && i < vertices; ++i)
{
    const float x = positions[i][0];
    const float y = positions[i][1];
    rim = x * x + y * y > 0.8f * 0.8f && x * x + y * y < 1.2f * 1.2f;
}
mvfxDestroyContext(context);
return rim;
```

## Water surfaces

A water surface (`mvfxWaterDef`) is a grid of heights over X and Z of the instance's frame, along its Y, by one of two models:

- **Waves** on water at rest (the wave equation): ripples, wakes and rain, waves moving at the def's speed and reflecting from closed walls or leaving through open ones.
- **Shallow water** over terrain (the virtual pipe model): water that runs downhill and pools, its ground read from a heightfield grid, filled at the start, from sources, or by particles poured into it.

The program pushes the surface where its bodies meet the water (`mvfxInstance_PushWater`): a push sinks or lifts the water within a radius at the start of the next step, and waves spread from it. It reads fields over a box of cells (`mvfxInstance_ReadWater`) and samples the surface at points (`mvfxInstance_SampleWater`: the surface point above or below each, its normal and the water's velocity there, and whether the point lies over the grid), to float boats, place cameras and play sounds.

An emitter meets a water surface through `water` (`mvfxWaterCoupling`): its particles float with a buoyancy and drift with the water's flow, push the water where they cross it, pour into shallow water, and write a crossing event where they enter, for a splash.

A surface stands at most `MVFX_MAX_WATER_DEPTH` (1e12 of its lengths) from where it rests. Each step holds a wave's height and shallow water's depth within it after the step's pushes, crossings, pours and sources. Pushes of the most depth on an instance at the least scale, or a large particle pouring into small cells, reach the bound, and the water then moves from there.

A stone pushed into still water: the water it displaces lifts the rest, and the hollow spreads out as a ring at the waves' speed.

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
// 64 by 64 cells of 0.1 about the origin, waves at 1 a second, closed
// walls.
mvfxWaterDef water = mvfxDefaultWaterDef();
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.waters = &water;
effectDef.waterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

// The stone sinks the water 0.1 at the center, within 0.5, at the next
// step's start.
const mvfxWaterPush stone = {.point = {0.0, 0.0, 0.0}, .radius = 0.5f, .depth = 0.1f};
made = made && mvfxInstance_PushWater(context, instance, 0, &stone, 1) == mvfx_success;
made = made && mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;

// The center, and a point 1 away the waves have not reached.
static const double points[6] = {0.0, 5.0, 0.0, 1.0, 5.0, 0.0};
mvfxWaterSample samples[2];
made =
    made && mvfxInstance_SampleWater(context, instance, 0, points, 2, samples) == mvfx_success;
const bool pushed = made && samples[0].surface[1] < -0.05 && samples[1].surface[1] > 0.0;

// A second on, the hollow has filled in and its ring has come 1 out.
for (int32_t step = 0; made && step < 60; ++step)
{
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
}
made =
    made && mvfxInstance_SampleWater(context, instance, 0, points, 2, samples) == mvfx_success;
made = made && pushed && samples[0].inside && samples[0].surface[1] > -0.02 &&
       samples[1].surface[1] < -0.005;
mvfxDestroyContext(context);
return made;
```

## Costs

A gas costs its cell count times its substeps each step, and its pressure solve dominates; a 2D gas is far cheaper than a 3D one. A water surface costs its cell count times its substeps. Gases and water surfaces step after every instance's emitters, each spread over the program's task hooks.
