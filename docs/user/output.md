# Output

The library draws nothing. After a step the program asks, per view and per emitter, for what it draws, and the library writes it into the program's buffers in the program's layout. Output functions read the context and change nothing, so the outputs of different views may be written from different threads at once.

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

## Contents

- [Layouts](#layouts)
- [Order](#order)
- [Quads for renderers without instancing](#quads-for-renderers-without-instancing)
- [Lights](#lights)
- [Bounds and draw order](#bounds-and-draw-order)
- [More output](#more-output)

## Layouts

A layout (`mvfxCreateLayout`) names the fields of a record, each at an offset with a format and a component count, and the stride between records. Fields cover what a particle is (position, size, colour, angle, orientation, velocity, age, flipbook frame, mesh index, custom attributes, serial), what its neighbors are ([interaction](interaction.md)), and what ribbon and quad vertices need (tangent, width, length, place along, side, face normal, corner). Formats are `float32`, `float16`, `unorm8`, `snorm8`, `unorm16`, `snorm16` and `uint32`.

The library converts values itself, by integer arithmetic, so a record has the same bytes on every platform: half floats round to nearest even, and normalized integers clamp and round to nearest. A colour field in `unorm8` can be written as sRGB; `mvfxGetSrgbSteps` gives the exact thresholds for a GPU path that must write the same bytes.

Positions are written relative to the view's `eye`, so a world far from its origin keeps full precision in `float32` records. A renderer adds the eye back in its view matrix, or keeps the eye at the origin.

## Order

`mvfxInstance_WriteParticles` writes one record per live particle, in the order the emitter's def asks (`sortMode`):

| Order | Use |
|---|---|
| The pool's, or youngest first | Order-independent blending, or newest on top. |
| Back to front, front to back | Alpha blending; depth from the view's direction, or distance from its eye (`sortMeasure`). |

Sorting by place is a stable radix sort, so particles at equal depth keep their order and the result never flickers. It works in scratch memory the program gives; `mvfxInstance_GetSortScratchBytes` says how much. A buffer smaller than the live count takes the first records of that order, and the count returned is always the live count.

A refused output call gives counts of 0 and zeroes the buffers it was given over the span it was told, so nothing from an earlier frame reads as this one's. Buffers of layout records (particle records, ribbon points, quad and strip vertices) are zeroed only while the layout is live: with no context or a stale layout the call cannot know a record's size, so it leaves them. A call refused as busy, which only the program's own allocator, task hooks or host modules can meet by calling back in, writes none of its outputs, counts included, since any of them may be one the call in progress fills.

## Quads for renderers without instancing

`mvfxInstance_ExpandSprites` writes four vertices and six indices a particle, turned by the emitter's sprite def: facing the camera, turned by the particle's angle, stretched along its velocity, upright on the world's Y, or in the plane of the particle's own orientation. The vertex layout uses the same fields as records, plus `corner`.

## Lights

`mvfxInstance_WriteLights` writes the lights an emitter's particles carry (`mvfxLightDef`): every n-th particle by serial carries one, its colour the particle's own times an intensity, its radius a base plus a share of the particle's size. A buffer of n lights takes the first n, so a renderer bounds its light count by its buffer.

## Bounds and draw order

`mvfxInstance_GetBounds` reads an emitter's box in the world after the last step, grown by the largest size its particles can have, or its fixed box (`fixedBounds`) when the def gives one; emitter -1 gives the box around the whole instance. A program culls with it and passes the result to `mvfxInstance_SetVisible`.

`mvfxContext_OrderEmitters` lists every visible emitter of the context in the order to draw: by the effect's `drawPriority`, then farther first, then by a fixed tie-break, skipping those outside an optional frustum. The order is the same for the same inputs.

## More output

- Ribbons, trails, beams, tubes and plexus links: [ribbons](ribbons.md).
- Sheets of cloth as meshes, and rigid bodies' orientations: [interaction](interaction.md).
- Gas fields, water surfaces and liquid surfaces: [fluids](fluids.md).
- Events: [events](events.md).
