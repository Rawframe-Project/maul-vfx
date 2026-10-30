# Ribbons, trails and beams

An emitter's particles become ribbons when its ribbon def (`mvfxRibbonDef`) chains them, each ribbon running through its particles from the oldest to the youngest:

| `ids` | Ribbons |
|---|---|
| `mvfx_ribbonOne` | One ribbon of all the emitter's particles in birth order: a trail. `mvfxInstance_BreakRibbons` ends it, and the next births start another. |
| `mvfx_ribbonPerBatch` | One ribbon per batch of births, such as each burst. |
| `mvfx_ribbonPerSource` | One ribbon per source, so one emitter draws trails behind many sources. |

A trail's head may be pinned to its source (`pinHead`): while the emitter spawns, its ribbon ends at a point on the shape's origin at the step's end, so the trail never lags a frame behind what draws it. A birth farther than `breakDistance` from the youngest point starts a new ribbon, so a teleport does not draw a streak across the world.

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
bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
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

## Contents

- [Shape and texture](#shape-and-texture)
- [Writing ribbons](#writing-ribbons)
- [Beams](#beams)
- [Plexus links](#plexus-links)

## Shape and texture

Each segment between two points is cut into pieces along a centripetal Catmull-Rom spline, which never overshoots or loops between unevenly spaced points. With `bendAngle` the pieces adapt, as few as keep each piece's turn within the angle, so straight runs cost little and tight curves stay smooth.

A ribbon faces the camera, turning about its own length, or a fixed normal. Its texture coordinate along may stretch over the whole ribbon, repeat per segment, or tile by distance so a texture keeps its size as the trail grows, and it may scroll with time. Width and colour vary along the ribbon's length by a curve and a gradient, on top of each particle's own size and colour, so a trail can taper and fade toward its tail.

## Writing ribbons

- `mvfxInstance_WriteRibbons` writes the points in a layout and a list of ribbons (first point and count each), for renderers that build their own strips or read ribbons on the GPU.
- `mvfxInstance_ExpandRibbons` writes triangle strips: two vertices a point, six indices a piece, with `side` telling the two edges apart.
- `mvfxInstance_ExpandTubes` writes round tubes (`mvfxTubeDef`), a ring of vertices around each point turned by rotation-minimizing frames so the tube does not twist, with optional caps. Ropes and chains look solid this way.

## Beams

A beam (`mvfxBeamDef`) places an emitter's particles along a line or a quadratic or cubic Bezier curve from a start to an end, each end fixed in the instance's space or taken from an instance parameter, so the program can aim it at a target each frame. Noise displaces the points across the beam and fades to zero at both ends. Its points are struck together, live one lifetime, and are struck again with new noise when they die, so lightning flickers between fixed ends. A beam is drawn as one ribbon through its points; its points are placed, not moved, so it takes no rate, bursts, forces or collisions.

## Plexus links

A plexus (`mvfxPlexusDef`) links every two particles of an emitter closer than a distance. Links are found with the emitter's neighbor grid, at most a number per particle and per emitter, and the rest are counted. `mvfxInstance_WritePlexus` writes them as two-point ribbons and `mvfxInstance_ExpandPlexus` as strips; a link may fade as it nears the distance, so links appear and vanish smoothly.
