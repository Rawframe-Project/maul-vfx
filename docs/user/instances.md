# Instances

An instance is one playing of an effect, at a place, with a seed. `mvfxCreateInstance` allocates everything the instance will hold, at the capacities of the quality level it gets, and `mvfxContext_StepTicks` advances every instance of the context together. An effect may not be destroyed while an instance plays it.

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
bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
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

## Placing and moving

An instance's transform is an origin in double precision, a rotation and a uniform scale. The origin is a `double` so that particles keep their precision far from the world's origin: a world-space emitter stores its particles as offsets from an anchor near the instance, and output writes them relative to the view's eye.

`mvfxInstance_SetTransform` sets where the instance will be at the end of the next step. Over that step it moves in a line from where it was, and every birth within the step takes the place the instance had at that birth's tick, so a fast-moving emitter leaves an even trail, not clumps at each frame. The move also feeds spawning by distance and the velocity emitters may inherit. A teleport places the instance at once, with no travel, no velocity, and its ribbons broken.

## Seeds

Every random draw depends on the instance's seed, the emitter and the particle's serial, never on the order things happen in. A seed of 0 takes one from the order the context made its instances, so the same calls give the same particles. A program that needs an instance to look the same in a replay or on another machine gives it the same seed and the same steps.

## Starting and stopping

- `mvfxInstance_Stop` stops spawning, either letting the particles alive live out their lives (`mvfx_stopEmitting`) or removing them at once (`mvfx_stopAndClear`).
- `mvfxInstance_IsFinished` is true once nothing will spawn and nothing is alive; a finished instance costs nothing to step, and the program may destroy it.
- `mvfxInstance_Restart` plays it again from its start with the same seed, so the same steps give the same particles.
- `mvfxInstance_Prewarm` advances one instance alone toward a span in steps of a given length, a bounded number a call, so a long prewarm (a fire already burning when a level loads) can be spread over frames.

## Off screen

`mvfxInstance_SetVisible` tells an instance whether the program sees it; `mvfxInstance_GetBounds` gives the bounds to decide by. An unseen instance follows its off-screen policy (`mvfxInstanceDef.offscreen`, `mvfxInstance_SetOffscreen`):

| Policy | While unseen |
|---|---|
| `mvfx_offscreenSimulate` | Steps as if seen. |
| `mvfx_offscreenThrottle` | Steps once every `offscreenInterval` steps, with the time it held back, so it catches up in larger steps. |
| `mvfx_offscreenPause` | Does not step; the time passes it by. |

Throttled instances spread their steps by their seeds, so many unseen instances do not all step in the same frame.

## Inputs between steps

Between steps the program may:

- spawn particles itself (`mvfxInstance_Spawn`): they are born at the start of the next step with the places, velocities and values given, up to the emitter's `hostSpawnCapacity`;
- set instance parameters (`mvfxInstance_SetParameter`), read by births from the next step;
- give an emitter another mesh to spawn from (`mvfxInstance_SetEmitterMesh`), as for a skinned character;
- set the colliders, the scene answers and the depth image the instance's emitters collide with ([collision](collision.md));
- hold units to its own frames (`mvfxInstance_SetAttachFrames`), wake sleeping rigid bodies, and push water surfaces ([interaction](interaction.md), [fluids](fluids.md)).

## What an emitter did

`mvfxInstance_GetEmitterStats` reads an emitter's live count and what it has done since the instance began: the births it owed, those its capacity dropped, those its kill rules killed, those killed for a value a host module left past its limit, and every count of work a capacity dropped (events, scene queries, neighbors, contacts, links, torn constraints). Nothing is dropped silently: a non-zero count says which capacity to raise.
