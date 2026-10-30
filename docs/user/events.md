# Events

An emitter writes the events its def asks for (`events`, a mask): births, deaths, collisions, tears of units, and crossings into a water surface. Each event (`mvfxEvent`) carries its kind, the particle's serial, the tick it happened at, its place in the world, its velocity, a surface normal where one applies, and the particle's colour and size at that moment. A death says whether a kill caused it rather than age.

Events are written in a fixed order each step, up to the emitter's `eventCapacity` for the whole context step; the rest are counted (`mvfxEmitterStats.eventsDropped`), never lost silently. The program reads them after the step with `mvfxInstance_GetEvents`, to play sounds, start decals, or spawn effects of its own. They stay until the context's next step or a restart. A context step that took several steps of an instance (a liquid's substeps, or the time a throttled instance held off screen) gives every one of them, in order: deaths by age first, then what happened as particles moved and were born, then deaths by kills.

## Sub-emitters

Another emitter of the same effect spawns from an emitter's events (`mvfxEventSpawn`): from each event of a kind, a number of births drawn between a minimum and a maximum, at the event's place and tick, taking a share of its velocity and, if asked, its colour and size, held to what the spawning emitter's particles may store ([limits](effects.md#limits-of-what-particles-show)). The source must write that kind of event. Sources step before the emitters that spawn from them, so a step's events spawn in the same step. A step's births from events are bounded by the spawning emitter's `capacity` for them; past it the latest by tick are refused and counted.

Fireworks are rockets whose deaths spawn sparks, and markers the program places itself:

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
emitters[1].eventSpawn = (mvfxEventSpawn){.enable = true,
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

## The program's own births

`mvfxInstance_Spawn` queues particles for an emitter between steps, with the places, velocities and values the program gives: bullet impacts, footsteps, a spell's hit point. A colour or size past what the emitter's particles may store is refused ([limits](effects.md#limits-of-what-particles-show)). They are born at the start of the next step, before the emitter's other births, and take serials and room like any birth. An emitter reserves `hostSpawnCapacity` for them in every instance.

## Scene queries

A step can also end by asking the program about its scene. That is collision rather than events; see [collision](collision.md#the-programs-scene).
