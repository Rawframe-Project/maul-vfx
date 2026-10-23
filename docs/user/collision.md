# Collision

An emitter's particles collide when its `collision` def is enabled: with its own colliders, with the instance's colliders, with the program's scene through queries, and with the camera's depth image. Each step's whole segment is tested, so a fast particle never passes through a thin wall, and up to four contacts a step are resolved in order.

## Colliders

A collider (`mvfxCollider`) is a plane, a sphere, a box, a heightfield or a signed distance grid, given in the world's or the emitter's space. Spheres and boxes keep particles outside or inside. Heightfields and distance grids read one of the context's grids (`mvfxCreateGrid`), spread over a box; the program may give the grid new values between steps (`mvfxGrid_SetValuesIn` for a box of them), as terrain is dug or a level changes.

- **The emitter's colliders** (`mvfxEmitterDef.colliders`) are part of the effect: the floor a fountain falls on.
- **The instance's colliders** (`mvfxInstance_SetColliders`, with `hostColliders` set) are the program's and change between steps: a character, a moving door.

A collision bounces by `restitution` and slows along the surface by `friction`, may take a share of the particle's life or kill it, stops a particle slower than `restSpeed` where it hit, and writes a collision event. A particle `radius` grows every collider by it, so particles of a size rest on a surface rather than in it.

## The program's scene

The library knows nothing of the program's world. With `sceneQueries`, each step ends with a query for each moving particle: its place and the segment it will cover in `lookahead` seconds. The program answers each with a contact plane from its own ray casts, or none (`mvfxInstance_GetSceneQueries`, `mvfxInstance_AnswerSceneQueries`), before the next step, which collides the particle's segment with its plane. A particle keeps its last plane until an answer replaces it, so a program may answer only some queries each frame. Queries past `queryCapacity` are counted, not lost.

Rain on an emitter's ball and the program's ground: the ball is the effect's collider, the ground the program's answer to the scene queries.

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

## The depth image

With `depthImage`, particles collide with the depth buffer the program renders (`mvfxInstance_SetDepthImage`): its pixels in one of the depth formats, normal or reversed depth, the view's projection and place, and optionally a normal for each pixel, made from the depth otherwise. A particle that passes behind the image's surface by less than `depthThickness` has met it. A step is tested at points along its segment, one a pixel it crosses, so fast particles do not pass thin walls. Outside the image, or behind the camera, there is no depth: particles do not collide there, or die when `killOutsideView` is set. This is the screen-space collision a GPU particle system does, on the CPU and the same everywhere.

## Kill rules

Kill rules are not collision, but end particles at surfaces too: past a plane, inside or outside a sphere or a box. See [effects](effects.md#ending-particles-early).
