# Interaction

Particles of an emitter can meet each other, and an emitter's births can be bodies rather than points: chains and sheets held by constraints, or rigid bodies that tumble and stack.

## Contents

- [Neighbors](#neighbors)
- [Ropes, chains and cloth](#ropes-chains-and-cloth)
- [Rigid particles](#rigid-particles)
- [Limits worth knowing](#limits-worth-knowing)

## Neighbors

An emitter finds its particles' neighbors (`mvfxNeighborDef`) in a grid built once a step from the particles alive at the step's start: its own, or another emitter's of the effect in the same space, which then steps first. Each particle gets the number of its neighbors within a radius and the offsets to their mean place and mean velocity; host modules and output records (`mvfx_recordNeighborCount` and the like) read them. A query counts at most `maxNeighbors`, in a fixed order, and counts the queries that reached the cap. Everything below builds on the neighbors.

**Steering** (`mvfxSteeringDef`, after Reynolds' boids and steering behaviors): separation, alignment and cohesion, each with a weight and a radius within a view cone; seeking and fleeing points, following a path; the sum held to a most force.

**Pair forces** (`mvfxPairForceDef`): a pull or a push between particles by their distance, along a curve, equal and opposite for each pair.

**Contacts** (`mvfxContactDef`): particles kept apart by their radii, by position projections over a number of iterations, with restitution and friction, so they pile instead of passing through each other.

**Plexus links** join every two close particles; see [ribbons](ribbons.md#plexus-links).

A flock: 32 birds set off within a wide cone, and alignment turns them one way. The order of a flock, the length of its summed velocity over the sum of its speeds, rises from about a half toward 1:

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
    (mvfxNeighborDef){.enable = true, .source = -1, .radius = 4.0f, .maxNeighbors = 32};
emitter.steering.enable = true;
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
bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
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

## Ropes, chains and cloth

An emitter whose `unit` is a chain or a sheet spawns, for each birth, a whole unit of points held by distance constraints, solved by XPBD in substeps (Macklin, Muller and Chentanez's extended position-based dynamics). Compliance sets how much the distance, the shear across a sheet's quads and the bending between points two apart may give; zero is rigid, and the result does not depend on the substep count the way plain iteration counts would.

- **Pins.** A unit's first point, or a sheet's first row, may be pinned: held to its birth place in the emitter's space, to the instance, to frames the program sets each step (`mvfxInstance_SetAttachFrames`, such as a character's hands), or to the particles of another emitter, so a tail follows a projectile.
- **Tearing.** A constraint stretched past `tearStrain` tears for good, writing a tear event; a chain drawn as ribbons splits there and a sheet's mesh drops the torn quads.
- **Air.** A sheet takes drag along its normal and across it from the emitter's winds and vector fields, so a flag flutters and a falling cloth tilts and glides.
- **Self-collision.** With `selfThickness`, points of different units, and points of one unit that are not neighbors, are kept apart, so cloth does not pass through itself.

`mvfxInstance_ExpandSheets` writes sheets as meshes with normals; chains draw as ribbons or tubes ([ribbons](ribbons.md)).

## Rigid particles

An emitter whose `rigid` def is set makes each particle a rigid body: a sphere, a capsule, a box, or the convex hull of one of the context's meshes, with a density. Bodies tumble, roll, bounce with restitution and slide with static and dynamic friction, solved as XPBD rigid bodies in substeps (Muller et al. 2020). They meet every kind of collider an emitter has, the instance's colliders, the program's scene and the depth image ([collision](collision.md)); a box meets a box face to face, edge to edge or on an edge. With `bodyContacts` they also meet each other and stack. Output gives each body's orientation (`mvfx_recordOrientation`) for drawing a mesh.

A hull is that of the positions its mesh was created with. `mvfxCreateMesh` builds it once, about 1 ms per 1,000 positions, and `mvfxMesh_SetPositions` leaves it as it is, so a rigid hull never follows a skinned mesh's pose. `mvfxMesh_GetHullCenter` gives its centroid, where a host draws the mesh from, and `mvfxMesh_GetHullVertexCount` how far a mesh refused for one is past `MVFX_MAX_HULL_VERTICES`.

A body's size is its extent along each of its axes, so a mesh scaled by it fits the body. A sphere's diameter is its size X, and its Y and Z must equal X; a capsule's diameter is X and its length Y, and its Z must equal X. An effect whose size source's ends or per-axis size over life differ from X's on those axes is refused, and so is a spawn request with such a size; a host module that writes one kills the particle. A birth takes X's draw for those axes, and a size the effect cannot fix in advance, an instance parameter's or one inherited from an event, keeps X (and a capsule's Y) and takes X for the rest.

Bodies at rest go to sleep: once a group of bodies joined by contacts has been still for `sleepTime`, it holds its pose and costs nothing until something wakes it (an awake body coming near, a changed force or collider, or `mvfxInstance_WakeBodies`). `mvfxEmitterStats.asleep` counts them.

## Limits worth knowing

Every capacity here counts what it drops in `mvfxEmitterStats`: neighbors past `maxNeighbors`, contacts past a particle's most, links past their caps, self-collision candidates, rigid contacts and capped nearest-point searches. A count above zero is the sign to raise a limit or lower a density. The headers state each one's measured behavior, such as how far a body under a much heavier one sinks into its support.
