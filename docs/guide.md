# Maul VFX guide

How a host uses Maul VFX, a part at a time. Every C block here is built
and run by the tests (`test/test_guide*.c`, checked by
`tools/check_guide.py`), so it is the API as it stands. The reference is
the headers and `docs/api.md`; the design and its reasons are in the
decisions record.

## The ground rules

- **A context holds everything.** The host creates it with its
  allocator and limits; effects, instances, layouts and grids live in
  it. Nothing is allocated after an object is made: stepping, writing
  output and reading results allocate nothing.
- **Ids, not pointers.** Objects are named by small id structs that go
  stale when the object is destroyed; a stale id is refused
  (`mvfx_errorStale`), never followed.
- **Every call returns a result.** `mvfx_success`, or why not: invalid
  arguments (counted as misuse, `mvfxGetContextMisuse`), a stale id, a
  limit or capacity reached, a state that refuses the call. Outputs are
  cleared on failure.
- **Time is ticks.** `MVFX_TICKS_PER_SECOND` (2^20) ticks a second; a
  step is a whole number of them, and a def's times are rounded once to
  ticks when its effect is made, so a repeating burst never drifts.
- **The same bits everywhere.** The CPU simulation is deterministic
  across platforms, compilers and the host's threads; its own math
  library and random numbers make it so.
- **Threads are the host's.** A context runs its work on the calling
  thread unless the host gives it task hooks (`enqueueTask` and
  `finishTask`), which spread a step's items over the host's own task
  system; the results are the same bits either way. A context is used
  by one thread at a time.
- **The library draws nothing.** It writes the host's buffers in the
  layouts the host asks for.

## A first effect

A fountain: an emitter spraying particles upward under gravity, stepped
for a second, its particles written as positions and colours.

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}

// One emitter: 50 particles a second, each living two seconds, thrown
// up at 3 to 4 units a second under gravity.
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
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

// A second in steps of a sixtieth.
for (int32_t step = 0; made && step < 60; ++step)
{
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
}

// Each particle as 28 bytes: its place, then its colour.
static const mvfxLayoutField fields[2] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
    {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 12},
};
mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
layoutDef.fields = fields;
layoutDef.fieldCount = 2;
layoutDef.stride = 28;
mvfxLayoutId layout = {0};
made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success;

static float records[128][7];
const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
int64_t count = 0;
made = made && mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0,
                                           records, 128, &count) == mvfx_success;
// About 50 live: a second at 50 a second, none yet old enough to die.
bool fountain = made && count >= 45 && count <= 55;
mvfxDestroyContext(context);
return fountain;
```

Destroying the context destroys everything in it.

## Effects in files

An effect def is written as bytes to keep and ship, and an effect is
made from them. The bytes are the same on every platform, checked by a
hash, and read as hostile input: a file at fault is refused with the
offset of the first byte to blame. Grids, meshes and host modules a def
refers to are the host's objects; a file names them through a table of
assets the host gives when it writes and when it reads.

```c
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 64;
emitter.rate = 20.0f;
mvfxEffectDef def = mvfxDefaultEffectDef();
def.emitters = &emitter;
def.emitterCount = 1;

// Its size first, then the bytes.
int64_t size = 0;
mvfxFileResult sized = mvfxWriteEffect(&def, NULL, 0, NULL, 0, &size);
static unsigned char file[4096];
if (sized.result != mvfx_errorCapacity || size > (int64_t)sizeof file ||
    mvfxWriteEffect(&def, NULL, 0, file, size, &size).result != mvfx_success)
{
    return false;
}

mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
mvfxEffectId effect = {0};
bool made =
    mvfxCreateEffectFromBytes(context, file, size, NULL, 0, &effect).result == mvfx_success;

// A byte changed: refused, at an offset in the file.
file[size / 2] ^= 0x40u;
mvfxEffectId damaged = {0};
mvfxFileResult refused = mvfxCreateEffectFromBytes(context, file, size, NULL, 0, &damaged);
made = made && refused.result == mvfx_errorInvalid && refused.offset >= 0;
mvfxDestroyContext(context);
return made;
```

`tools/effect_json.py` turns a file into JSON and back, for diffs,
reviews and authoring by hand.

## Instances

An instance plays an effect at a place. Its transform (an origin in
binary64, a rotation, a uniform scale) moves its emitters; particles in
the world stay where they were born, those in the instance's space move
with it. A move that is not a teleport sweeps what it spawns along the
way. Bounds are read per emitter after a step, in the world. Stopping
ends emission (or clears at once); the instance is finished when its
last particle dies. An instance plays at a quality level chosen when it
is made (`mvfxInstanceDef.quality`, lowered until its particles fit the
context's budget), and off screen it simulates, throttles or pauses as
the host sets (`mvfxInstance_SetVisible`, `mvfxInstance_SetOffscreen`).

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 64;
emitter.rate = 30.0f;
emitter.lifetime = mvfxRangeSource(0.5f, 0.5f);
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

// Far from the world's origin, turned a quarter about Y, twice the size;
// placed there at once.
const double far[3] = {100000.0, 0.0, -25000.0};
const float turn[4] = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
made =
    made && mvfxInstance_SetTransform(context, instance, far, turn, 2.0f, true) == mvfx_success;
for (int32_t step = 0; made && step < 30; ++step)
{
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
}
mvfxBounds bounds;
made = made && mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success &&
       !bounds.empty && bounds.min[0] <= far[0] && bounds.max[0] >= far[0];

// Stopped: no more births; the last particles live out their half second.
made = made && mvfxInstance_Stop(context, instance, mvfx_stopEmitting) == mvfx_success;
int32_t steps = 0;
while (made && !mvfxInstance_IsFinished(context, instance) && steps < 120)
{
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    ++steps;
}
made = made && mvfxInstance_IsFinished(context, instance) && steps <= 31;
mvfxDestroyContext(context);
return made;
```

`mvfxInstance_SetVisible` and `mvfxInstance_SetOffscreen` let an
instance the host does not see step less often or not at all;
`mvfxInstance_GetQuality` reads the level the context's budget gives
it.

## Output

Records are written in the layout the host defines, in the order the
emitter's def asks: the pool's, youngest first, or by distance or depth
from a view (a stable radix sort, so equal keys keep the pool's order).
A sort by place takes scratch the host gives. Renderers that cannot draw
instances take quads instead: four vertices and six indices a particle,
facing the camera, moving along their velocity or held to an axis, as
the emitter's sprite def says.

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 32;
emitter.rate = 60.0f;
emitter.velocity = (mvfxVelocity){
    .mode = mvfx_directionFixed, .direction = {0.0f, 0.0f, 1.0f}, .speed = {1.0f, 3.0f}};
emitter.sortMode = mvfx_sortBackToFront;
emitter.sortMeasure = mvfx_sortByDepth;
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success &&
       mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success;

// A view looking down -Z from 10 units out: the farthest first.
const mvfxView view = {
    .eye = {0.0, 0.0, 10.0}, .forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
static const mvfxLayoutField place = {
    .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
layoutDef.fields = &place;
layoutDef.fieldCount = 1;
layoutDef.stride = 12;
mvfxLayoutId layout = {0};
int64_t scratchBytes = 0;
made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
       mvfxInstance_GetSortScratchBytes(context, instance, 0, &scratchBytes) == mvfx_success;
static uint32_t scratch[32 * 4];
static float records[32][3];
int64_t count = 0;
made =
    made && scratchBytes <= (int64_t)sizeof scratch &&
    mvfxInstance_WriteParticles(context, instance, 0, layout, &view, scratch,
                                (int64_t)sizeof scratch, records, 32, &count) == mvfx_success;
for (int64_t i = 1; made && i < count; ++i)
{
    // Back to front: each no nearer the eye along Z than the one after.
    made = records[i - 1][2] <= records[i][2];
}

// The same particles as camera-facing quads: a vertex's place and its
// corner.
static const mvfxLayoutField corners[2] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 12},
};
layoutDef.fields = corners;
layoutDef.fieldCount = 2;
layoutDef.stride = 20;
mvfxLayoutId vertexLayout = {0};
static float vertices[4 * 32][5];
static uint32_t indices[6 * 32];
const mvfxExpandBuffers buffers = {.vertices = vertices,
                                   .vertexCapacity = 4 * 32,
                                   .indices = indices,
                                   .indexCapacity = 6 * 32,
                                   .scratch = scratch,
                                   .scratchBytes = (int64_t)sizeof scratch};
int64_t quads = 0;
made = made && mvfxCreateLayout(context, &layoutDef, &vertexLayout) == mvfx_success &&
       mvfxInstance_ExpandSprites(context, instance, 0, vertexLayout, &view, &buffers,
                                  &quads) == mvfx_success &&
       quads == count && count > 0;
mvfxDestroyContext(context);
return made;
```

`mvfxInstance_WriteLights` writes the particles that carry lights;
`mvfxContext_OrderEmitters` lists every visible emitter of the context
in the order to draw them, by priority and then back to front.

## Ribbons and beams

An emitter's particles become ribbons when its ribbon def chains them:
one ribbon of all its particles in birth order (a trail), one a batch,
or one a source (trails of many sources from one emitter). A ribbon is
tessellated along a centripetal Catmull-Rom spline, textured along its
length, and faces the camera or a normal. `mvfxInstance_WriteRibbons`
writes its points and a ribbon list for renderers that build strips
themselves; `mvfxInstance_ExpandRibbons` writes the strips (two vertices
a point, six indices a piece), and `mvfxInstance_ExpandTubes` tubes
round them. A beam (`mvfxBeamDef`) strikes a ribbon between two points
along a bent curve with noise, renewed at its own rate.

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
// A trail: a point every thirtieth of a second, living a second, chained
// into one ribbon whose head is pinned to the source.
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 64;
emitter.rate = 30.0f;
emitter.lifetime = mvfxRangeSource(1.0f, 1.0f);
emitter.ribbon.ids = mvfx_ribbonOne;
emitter.ribbon.pieces = 2;
emitter.ribbon.pinHead = true;
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

// The source moves along X; its trail follows.
static const float still[4] = {0.0f, 0.0f, 0.0f, 1.0f};
for (int32_t step = 0; made && step < 30; ++step)
{
    const double at[3] = {0.1 * (double)step, 0.0, 0.0};
    made = mvfxInstance_SetTransform(context, instance, at, still, 1.0f, step == 0) ==
               mvfx_success &&
           mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 30) == mvfx_success;
}

// The strip: each vertex's place and its coordinate along the ribbon.
static const mvfxLayoutField fields[2] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 12},
};
mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
layoutDef.fields = fields;
layoutDef.fieldCount = 2;
layoutDef.stride = 16;
mvfxLayoutId layout = {0};
static float vertices[512][4];
static uint32_t indices[1536];
static uint32_t scratch[64 * 4];
const mvfxExpandBuffers buffers = {.vertices = vertices,
                                   .vertexCapacity = 512,
                                   .indices = indices,
                                   .indexCapacity = 1536,
                                   .scratch = scratch,
                                   .scratchBytes = (int64_t)sizeof scratch};
const mvfxView view = {
    .eye = {1.5, 0.0, 10.0}, .forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
int64_t vertexCount = 0;
int64_t indexCount = 0;
made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
       mvfxInstance_ExpandRibbons(context, instance, 0, layout, &view, &buffers, &vertexCount,
                                  &indexCount) == mvfx_success;
// About 30 points, two pieces a segment: a strip of two vertices a point.
made =
    made && vertexCount > 100 && vertexCount <= 512 && indexCount > 0 && vertexCount % 2 == 0;
mvfxDestroyContext(context);
return made;
```

## Events, sub-emitters and the host's own particles

An emitter writes the events its def asks for (births, deaths,
collisions, tears, crossings of a water surface) each step, in a fixed
order, up to its capacity; the rest are counted, not lost silently.
Another emitter of the effect spawns from them (a sub-emitter), taking
some of the parent's velocity, colour and size. The host reads them
after the step, and spawns particles itself with the values it gives.

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
// Rockets: two a second, rising for a second, writing their deaths.
mvfxEmitterDef emitters[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                              mvfxDefaultEmitterDef()};
emitters[0].capacity = 8;
emitters[0].rate = 2.0f;
emitters[0].lifetime = mvfxRangeSource(1.0f, 1.0f);
emitters[0].velocity = (mvfxVelocity){
    .mode = mvfx_directionFixed, .direction = {0.0f, 1.0f, 0.0f}, .speed = {10.0f, 10.0f}};
emitters[0].events = mvfx_eventMaskDeath;
emitters[0].eventCapacity = 8;
// Sparks: 20 from each rocket's death, with half its velocity.
emitters[1].capacity = 256;
emitters[1].rate = 0.0f;
emitters[1].lifetime = mvfxRangeSource(0.5f, 0.8f);
emitters[1].velocity.speed = (mvfxRange){2.0f, 4.0f};
emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                          .source = 0,
                                          .kind = mvfx_eventDeath,
                                          .minCount = 20,
                                          .maxCount = 20,
                                          .inheritVelocity = 0.5f,
                                          .capacity = 64};
// Markers the host places itself.
emitters[2].capacity = 4;
emitters[2].rate = 0.0f;
emitters[2].hostSpawnCapacity = 4;
emitters[2].lifetime = mvfxRangeSource(10.0f, 10.0f);
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = emitters;
effectDef.emitterCount = 3;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

static const double marks[6] = {1.0, 0.0, 0.0, -1.0, 0.0, 0.0};
const mvfxSpawnRequest request = {.count = 2, .positions = marks};
made = made && mvfxInstance_Spawn(context, instance, 2, &request) == mvfx_success;

// Two seconds: each step's deaths read back after it.
int32_t deaths = 0;
for (int32_t step = 0; made && step < 120; ++step)
{
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
           mvfxInstance_GetEvents(context, instance, 0, &events, &count) == mvfx_success;
    deaths += count;
}
mvfxEmitterStats sparks;
mvfxEmitterStats markers;
made = made && mvfxInstance_GetEmitterStats(context, instance, 1, &sparks) == mvfx_success &&
       mvfxInstance_GetEmitterStats(context, instance, 2, &markers) == mvfx_success;
// The first rockets died, each into 20 sparks; both markers live.
made = made && deaths >= 1 && sparks.live > 0 && markers.live == 2;
mvfxDestroyContext(context);
return made;
```

## Interaction

Particles of an emitter meet each other through its neighbors
(`mvfxNeighborDef`): a grid of hashed cells built once a step from the
particles alive at the step's start, of its own emitter or another in
the same space. Each particle gets the count of its neighbors and the
offsets to their mean place and mean velocity, which host modules and
output records read. Built on them:

- **Steering** (`mvfxSteeringDef`, Reynolds 1987 and 1999): separation,
  alignment and cohesion, each with a weight and a radius within a view
  cone; seeking, fleeing and following a path; the sum held to a most
  force.
- **Pair forces** (`mvfxPairForceDef`): a pull or a push by the
  distance, equal and opposite for each pair.
- **Contacts** (`mvfxContactDef`): particles kept apart by position
  projection, with restitution and friction.
- **Plexus links** (`mvfxPlexusDef`): a line between each two particles
  closer than a distance, written as two-point ribbons.

Two kinds of emitters are bodies rather than points. **Units**
(`mvfxUnitDef`) are chains and sheets of points held by constraints in
XPBD substeps: ropes, tails and cloth, pinned to a frame or free, torn
past a stretch. **Rigid particles** (`mvfxRigidDef`) are spheres,
capsules, boxes and convex hulls that tumble, roll and come to rest on
the colliders.

A flock: 32 birds set off within a wide cone, and alignment turns them
one way. The order of a flock, the length of its summed velocity over
the sum of its speeds, rises from about a half toward 1 (without
alignment it stays near a half).

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
// 32 birds at once, in a ball of radius 2, flying at 1 within 1.5
// radians of +X.
static const mvfxBurst burst = {
    .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f};
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 32;
emitter.rate = 0.0f;
emitter.bursts = &burst;
emitter.burstCount = 1;
emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
emitter.shape.kind = mvfx_shapeSphere;
emitter.shape.radius = 2.0f;
emitter.shape.thickness = 1.0f;
emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                  .direction = {1.0f, 0.0f, 0.0f},
                                  .coneAngle = 1.5f,
                                  .speed = {1.0f, 1.0f}};
// Each sees the rest of its own emitter within 4, in every direction.
emitter.neighbors =
    (mvfxNeighborDef){.enabled = true, .source = -1, .radius = 4.0f, .maxNeighbors = 32};
emitter.steering.enabled = true;
emitter.steering.separation = 1.0f;
emitter.steering.separationRadius = 0.5f;
emitter.steering.alignment = 2.0f;
emitter.steering.alignmentRadius = 4.0f;
emitter.steering.cohesion = 0.5f;
emitter.steering.cohesionRadius = 4.0f;
emitter.steering.viewAngle = 3.14159265f;
emitter.steering.maxForce = 4.0f;
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

static const mvfxLayoutField field = {
    .field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3};
mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
layoutDef.fields = &field;
layoutDef.fieldCount = 1;
layoutDef.stride = 12;
mvfxLayoutId layout = {0};
made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success;

// The order after the first step, then after three seconds more.
double order[2] = {0.0, 0.0};
for (int32_t pass = 0; made && pass < 2; ++pass)
{
    for (int32_t step = 0; made && step < (pass == 0 ? 1 : 180); ++step)
    {
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    }
    static float velocities[32][3];
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t count = 0;
    made = made &&
           mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, velocities,
                                       32, &count) == mvfx_success &&
           count == 32;
    double sum[3] = {0.0, 0.0, 0.0};
    double speeds = 0.0;
    for (int64_t i = 0; made && i < count; ++i)
    {
        const double x = (double)velocities[i][0];
        const double y = (double)velocities[i][1];
        const double z = (double)velocities[i][2];
        sum[0] += x;
        sum[1] += y;
        sum[2] += z;
        speeds += sqrt(x * x + y * y + z * z);
    }
    order[pass] =
        speeds > 0.0 ? sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]) / speeds : 0.0;
}
// Spread at first; one way after three seconds.
made = made && order[0] < 0.7 && order[1] > 0.9;
mvfxDestroyContext(context);
return made;
```

## Fluids

An effect holds gases and water surfaces beside its emitters
(`mvfxEffectDef.gases`, `.waters`), each stepped with its instance.

A **gas** (`mvfxGasDef`) is a box of cells, 2D or 3D, in the instance's
frame or left where the instance began. Its kinds:

- **Smoke and fire**: density, temperature and fuel carried by a
  velocity (Stam's stable fluids), from sources, under buoyancy, vortex
  confinement, combustion, obstacles and forces, kept from compressing
  by a multigrid pressure solve.
- **A liquid's grid**: the particles of a liquid emitter
  (`mvfxLiquidDef`, solver `mvfx_liquidGrid`) give it their velocity and
  take back the projected one (APIC). A liquid of particles alone
  (`mvfx_liquidParticles`, position-based fluids) needs no gas.
- **A surface**: the signed distance to the surface of an emitter's
  particles, made anew each step, which `mvfxInstance_WriteSurfaceMesh`
  meshes by marching cubes (squares in 2D).

Particles follow a gas or splat into it (`mvfxEmitterDef.gasSplat`).
The host reads a gas's fields (`mvfxInstance_ReadGas`, or
`mvfxInstance_ReadGasAs` into a texture's format) and samples them at
points (`mvfxInstance_SampleGas`); the library draws no volume.

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
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
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

A disc of particles wrapped by a surface gas and meshed: in 2D the mesh
is segments, closed loops of them, so each vertex begins one segment
and ends another.

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
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
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

A **water surface** (`mvfxWaterDef`) is a grid of heights across X and
Z, along the instance's Y: waves on water at rest (the wave equation),
or shallow water that runs and pools over terrain (the virtual pipe
model), filled from sources or by particles poured into it
(`mvfxEmitterDef.water`). The host pushes it where bodies meet it
(`mvfxInstance_PushWater`), reads its fields (`mvfxInstance_ReadWater`)
and samples it at points (`mvfxInstance_SampleWater`) to float things,
place cameras and play sounds. Particles float on it and drift with it,
and push it where they cross it, writing an event there for a splash.

A stone pushed into still water: the water it displaces lifts the rest,
and the hollow spreads out as a ring at the waves' speed.

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
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
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

## Collision

An emitter's particles collide (`mvfxCollision`) with its own colliders
and, with `hostColliders`, the instance's (`mvfxInstance_SetColliders`):
planes, spheres and boxes, heightfields and signed distance grids
(`mvfxCreateGrid`), each kept outside or inside. Each step's whole
segment is tested, so a fast particle never passes through. A collision
bounces by restitution and slows by friction, can take life or kill,
and writes an event.

The host's own scene answers in two ways. **Scene queries**: each step
ends with a query for each moving particle (its place and where it is
going), and the host answers each with a contact plane, from its
physics engine's ray casts, before the next step. **A depth image**
(`mvfxInstance_SetDepthImage`): the camera's depth buffer, which
particles meet where they pass behind its surface, as screen-space
collision does on a GPU.

Rain on an emitter's ball and the host's ground: the ball is the
effect's collider, the ground the host's answer to the scene queries.

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
// 64 drops at once over a square 3 up, falling.
static const mvfxBurst burst = {
    .minCount = 64, .maxCount = 64, .cycles = 1, .probability = 1.0f};
static const mvfxCollider ball = {
    .kind = mvfx_colliderSphere, .center = {0.0f, 1.0f, 0.0f}, .radius = 0.5f};
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 64;
emitter.rate = 0.0f;
emitter.bursts = &burst;
emitter.burstCount = 1;
emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
emitter.shape.kind = mvfx_shapeBox;
emitter.shape.offset[1] = 3.0f;
emitter.shape.halfExtents[0] = 1.0f;
emitter.shape.halfExtents[2] = 1.0f;
emitter.motion.gravity[1] = -9.8f;
emitter.colliders = &ball;
emitter.colliderCount = 1;
emitter.collision.enabled = true;
emitter.collision.restitution = 0.3f;
emitter.collision.friction = 0.5f;
emitter.collision.sceneQueries = true;
emitter.collision.queryCapacity = 64;
emitter.collision.lookahead = 0.25f;
mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

// Two seconds; after each step the host answers every query with its
// ground, the plane y = 0 (a real host casts each query's segment).
for (int32_t step = 0; made && step < 120; ++step)
{
    const mvfxSceneQuery* queries = NULL;
    int32_t count = 0;
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
           mvfxInstance_GetSceneQueries(context, instance, 0, &queries, &count) == mvfx_success;
    static mvfxSceneAnswer answers[64];
    for (int32_t i = 0; i < count; ++i)
    {
        answers[i] = (mvfxSceneAnswer){.hit = true, .normal = {0.0f, 1.0f, 0.0f}};
    }
    made = made && mvfxInstance_AnswerSceneQueries(context, instance, 0, answers, count) ==
                       mvfx_success;
}

static const mvfxLayoutField field = {
    .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
layoutDef.fields = &field;
layoutDef.fieldCount = 1;
layoutDef.stride = 12;
mvfxLayoutId layout = {0};
made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success;
static float places[64][3];
const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
int64_t count = 0;
made = made &&
       mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, places, 64,
                                   &count) == mvfx_success &&
       count == 64;
// Every drop came to rest on the ground; those that struck the ball were
// thrown off it, some past the square they fell from.
int32_t thrown = 0;
for (int64_t i = 0; made && i < count; ++i)
{
    made = places[i][1] > -1e-3f && places[i][1] < 1e-2f;
    thrown += fabsf(places[i][0]) > 1.0f || fabsf(places[i][2]) > 1.0f ? 1 : 0;
}
made = made && thrown > 0;
mvfxDestroyContext(context);
return made;
```

## The GPU part

`maul-vfx-rhi` (CMake option `MAUL_VFX_RHI`) runs a planned instance on
the GPU through Maul RHI. A planned instance (`mvfxInstanceDef.planned`)
steps on the CPU as any other, but only plans its step (its births,
host births, forces, colliders and moves, `maul-vfx/plan.h`); a
simulation (`mvfxGpuSim`) holds its particles, gases and water surfaces
in device buffers and runs each step's plan in compute passes of WGSL
kernels, its results the CPU path's within a stated tolerance. The
host builds each frame: after the context's step, the simulation adds
its passes to the frame (`mvfxGpuSim_AddPasses`, then records,
lights, ribbons, sprites, reads, samples and meshes it is asked for)
and records them once the frame is compiled (`mvfxGpuSim_Record`).

Nothing is read back within a step. Each emitter's statistics record
(its live count and bounds) is the host's to read back as late as its
frames lag; `mvfxGpuSim_BoundsOf` maps it to the world. What the part
cannot run is refused when the simulation is made, with the first
emitter at fault and the features why (`mvfxGpuRefusal`): host modules
and scene queries, which are the host's code, and a few pairings the
header states with their reasons.

The first section's fountain on the device: the host has a device and
the part's GPU object (`mvfxCreateGpu`, its pipelines ready).

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
           mrhiBeginFrame(device, &frame) == mrhi_success &&
           mvfxGpuSim_AddPasses(sim) == mvfx_success &&
           mrhiCompileFrame(device) == mrhi_success && mvfxGpuSim_Record(sim) == mvfx_success &&
           mrhiSubmitFrame(device, &submitted) == mrhi_success &&
           mrhiWaitFrame(device, submitted, UINT64_C(10000000000)) == mrhi_success;
    if (!made)
    {
        (void)mrhiDropFrame(device); // a frame left open by a failure
    }
    mrhiDeviceNotification note;
    while (mrhiNextDeviceNotification(device, &note) == mrhi_success)
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
       mrhiBeginFrame(device, &frame) == mrhi_success &&
       mrhiImportBuffer(device, particles.statistics, &statistics) == mrhi_success;
const mrhiAccess reading = {.resource = statistics, .kind = mrhi_accessCopySource};
mrhiPassDef passDef = mrhiDefaultPassDef();
passDef.passClass = mrhi_passTransfer;
passDef.accesses = &reading;
passDef.accessCount = 1;
passDef.neverCull = true;
static uint32_t record[MVFX_GPU_STATISTICS_WORDS];
made = made && mrhiAddPass(device, &passDef, &pass) == mrhi_success &&
       mrhiCompileFrame(device) == mrhi_success &&
       mrhiBeginPass(device, pass) == mrhi_success &&
       mrhiReadBuffer(device, pass, statistics, 0, sizeof record, &read) == mrhi_success &&
       mrhiEndPass(device, pass) == mrhi_success &&
       mrhiSubmitFrame(device, &submitted) == mrhi_success &&
       mrhiWaitFrame(device, submitted, UINT64_C(10000000000)) == mrhi_success;
if (!made)
{
    (void)mrhiDropFrame(device);
}
mrhiDeviceNotification note;
while (mrhiNextDeviceNotification(device, &note) == mrhi_success)
{
    (void)mvfxGpu_Notify(kernels, &note);
}
size_t taken = 0;
made = made && mrhiTakeReadback(device, read, record, sizeof record, &taken) == mrhi_success;

// About 50 live, as on the CPU, and their bounds above the ground.
mvfxBounds bounds;
made = made && record[0] >= 45 && record[0] <= 55 &&
       mvfxGpuSim_BoundsOf(sim, 0, record, &bounds) == mvfx_success && bounds.max[1] > 1.0;
mvfxDestroyGpuSim(sim);
mvfxDestroyContext(context);
return made;
```

## Building and using it

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix "$PWD/package"
```

A program finds the installed package with
`find_package(maul-vfx REQUIRED CONFIG)` and links `maul-vfx::maul-vfx`.
`mvfxGetVersion` gives the version of the library linked, which a
program can compare with the `MVFX_VERSION_*` macros it was compiled
with. The headers compile as C17, C23 and C++17; the library itself is
C23 (GCC 14, Clang 19 or newer; clang-cl on Windows) and builds on
Linux (x86-64 and arm64), macOS, Windows and the web.

Options (each `MAUL_VFX_*`): `BUILD_SHARED` for a shared library,
`BUILD_TESTS` and `BUILD_BENCH` (on when it is the top-level project),
`INSTALL`, `WERROR`, `SANITIZE` and `TSAN`, `COVERAGE`, `FUZZ` (its
libFuzzer targets), and `RHI` for the GPU part.

`MAUL_VFX_RHI` builds `maul-vfx-rhi` beside the core, linked as
`maul-vfx-rhi::maul-vfx-rhi`: it finds Maul RHI installed or fetches its
release, and its WGSL kernels come precompiled into containers for
every backend, so a build needs no shader tools. The core never
depends on it. Its tests run on a device on the first adapter
(lavapipe on CI) and skip without one.

On the web (Emscripten) the core builds and its tests run under Node;
the web has no threads, so a host there gives its context no task hooks.
