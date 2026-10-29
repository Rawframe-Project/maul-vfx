// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The limits of what a record shows, each at its bound and just past it: a
// life curve's scale (MVFX_MAX_LIFE_SCALE) wherever one is held; a size's
// source over its size over life (MVFX_MAX_EXTENT), a colour's over its
// colour over life (MVFX_MAX_COLOR), a spin's and an angular velocity's
// over their rotation speed (MVFX_MAX_SPIN), a custom attribute's over its
// scale (MVFX_MAX_VALUE); a gradient's keys; a host's spawned sizes and
// colours refused past them, an event's inherited ones held to them; each
// moved particle's speed held within MVFX_MAX_SPEED along its heading; and
// what a host module leaves past a limit killed and counted apart, a place
// past MVFX_MAX_PLACE among them, so the records stay finite under the
// largest instance scale.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

// Whether an effect of the emitters and a parameter's defaults (none
// for NULL) is taken.
static bool TakenWith(const mvfxEmitterDef* emitters, int32_t count, const float* parameter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = count;
    effectDef.parameters = parameter;
    effectDef.parameterCount = parameter != NULL ? 1 : 0;
    mvfxEffectId effect = {0};
    bool taken = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    mvfxDestroyContext(context);
    return taken;
}

static bool Taken(const mvfxEmitterDef* emitters, int32_t count)
{
    return TakenWith(emitters, count, NULL);
}

static Scene NewScene(const mvfxEmitterDef* emitters, int32_t count, float scale)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.scale = scale;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

static void Step(const Scene* scene, int64_t ticks)
{
    CHECK(mvfxContext_StepTicks(scene->context, ticks) == mvfx_success, "step");
}

static TestParticle s_particles[64];

static int32_t Read(const Scene* scene, int32_t emitter)
{
    return ReadParticles(scene->context, scene->instance, emitter, s_particles, 64);
}

static void Stats(const Scene* scene, int32_t emitter, mvfxEmitterStats* stats)
{
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, emitter, stats) ==
              mvfx_success,
          "stats");
}

// An emitter of particles the host spawns, living a minute.
static mvfxEmitterDef Hosted(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 16;
    emitter.hostSpawnCapacity = 16;
    emitter.lifetime = mvfxRangeSource(60.0f, 60.0f);
    return emitter;
}

static void Spawn(const Scene* scene, int32_t count)
{
    static const double places[3 * 4] = {0};
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "spawned");
}

static mvfxVectorSource Range3(float x, float y, float z)
{
    return (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {x, y, z, 0}, .max = {x, y, z, 0}};
}

static mvfxVectorSource Range4(float r, float g, float b, float a)
{
    return (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {r, g, b, a}, .max = {r, g, b, a}};
}

// The next float above x.
static float Past(float x)
{
    return nextafterf(x, INFINITY);
}

// An emitter holding a life curve in one of its places.
static mvfxEmitterDef HoldingLife(int32_t place, mvfxLifeCurve life)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    static mvfxCustomAttribute custom;
    switch (place)
    {
    case 0:
        emitter.overLife.size = (mvfxSizeOverLife){.enabled = true, .size = {life}};
        break;
    case 1:
        emitter.overLife.color =
            (mvfxColorOverLife){.enabled = true, .scaleAlpha = true, .alpha = life};
        break;
    case 2:
        emitter.overLife.rotation = (mvfxRotationOverLife){.enabled = true, .scale = life};
        break;
    case 3:
        custom = (mvfxCustomAttribute){.components = 1, .scaledOverLife = true, .overLife = life};
        emitter.custom = &custom;
        emitter.customCount = 1;
        break;
    case 4:
        emitter.velocityModules.addedSpeed = life;
        emitter.velocityModules.addedDirection[0] = 1.0f;
        break;
    case 5:
        emitter.velocityModules.orbitRate = life;
        emitter.velocityModules.orbitAxis[1] = 1.0f;
        break;
    default:
        emitter.velocityModules.motionScale = life;
        break;
    }
    return emitter;
}

static void TestLifeCurves(void)
{
    // value times the curve's largest baked sample within 1e6, wherever a
    // life curve is held: by its value alone, by a key, and by a cubic's
    // rise between keys of 0 (to 2.5e5 halfway).
    static const mvfxCurveKey key[1] = {{0.0f, MVFX_MAX_LIFE_SCALE, 0, 0, 0}};
    static const mvfxCurveKey rising[2] = {{0.0f, 0.0f, 0.0f, 1e6f, mvfx_interpolationCubic},
                                           {1.0f, 0.0f, -1e6f, 0.0f, mvfx_interpolationCubic}};
    const mvfxCurve keyed = {key, 1, mvfx_tangentsGiven};
    const mvfxCurve rise = {rising, 2, mvfx_tangentsGiven};
    const mvfxLifeCurve atBound[3] = {{.value = MVFX_MAX_LIFE_SCALE},
                                      {.value = -1.0f, .curve = keyed},
                                      {.value = 4.0f, .curve = rise}};
    const mvfxLifeCurve pastBound[3] = {{.value = Past(MVFX_MAX_LIFE_SCALE)},
                                        {.value = 1.0001f, .curve = keyed},
                                        {.value = 5.0f, .curve = rise}};
    bool within = true;
    bool past = true;
    for (int32_t place = 0; place < 7; ++place)
    {
        // Each taken as it is made: the custom attribute's place is shared.
        for (int32_t k = 0; k < 3; ++k)
        {
            mvfxEmitterDef emitter = HoldingLife(place, atBound[k]);
            within = within && Taken(&emitter, 1);
            emitter = HoldingLife(place, pastBound[k]);
            past = past && !Taken(&emitter, 1);
        }
    }
    CHECK(within, "a life curve at its bound, in every place");
    CHECK(past, "a life curve past it, in every place");
}

static void TestSizes(void)
{
    // A size's ends over its size over life: 1e3 times a scale of 1e3.
    mvfxEmitterDef emitter = Hosted();
    emitter.rate = 4.0f;
    emitter.size = Range3(1e3f, -1e3f, 1e6f);
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .perAxis = true, .size = {{.value = 1e3f}, {.value = 1e3f}, {.value = 1}}};
    CHECK(Taken(&emitter, 1), "a size at its cap on every axis");
    Scene scene = NewScene(&emitter, 1, 1.0f);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    int32_t count = Read(&scene, 0);
    CHECK(count > 0 && s_particles[0].size[0] == 1e6f && s_particles[0].size[1] == -1e6f &&
              s_particles[0].size[2] == 1e6f,
          "shown at the limit");
    mvfxDestroyContext(scene.context);
    emitter.size = Range3(1e3f, Past(1e3f), 1e6f);
    CHECK(!Taken(&emitter, 1), "past it on one axis");
    emitter.size = Range3(1e3f, 1e3f, Past(1e6f));
    CHECK(!Taken(&emitter, 1), "past it where no scale holds the axis");
}

static void TestColors(void)
{
    // A colour's channels over the gradient's largest colour key (1e3),
    // its alpha over the largest alpha key (2) times the alpha's scale (2).
    static const mvfxColorKey colors[2] = {{0.0f, 1, 1, 1}, {1.0f, 1e3f, 0, 0}};
    static const mvfxAlphaKey alphas[1] = {{0.0f, 2.0f}};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.color = Range4(1e3f, 1e3f, -1e3f, 2.5e5f);
    emitter.overLife.color = (mvfxColorOverLife){.enabled = true,
                                                 .gradient = {colors, 2, alphas, 1},
                                                 .scaleAlpha = true,
                                                 .alpha = {.value = 2.0f}};
    CHECK(Taken(&emitter, 1), "a colour at its cap");
    emitter.color = Range4(1e3f, 1e3f, -1e3f, Past(2.5e5f));
    CHECK(!Taken(&emitter, 1), "its alpha past");
    emitter.color = Range4(1e3f, Past(1e3f), 1e3f, 1.0f);
    CHECK(!Taken(&emitter, 1), "a channel past");
    // A gradient's keys within MVFX_MAX_COLOR, as a ribbon's colour too.
    static const mvfxColorKey bright[1] = {{0.0f, MVFX_MAX_COLOR, 0, 0}};
    const mvfxColorKey brighter[1] = {{0.0f, Past(MVFX_MAX_COLOR), 0, 0}};
    static const mvfxAlphaKey opaque[1] = {{0.0f, 1.0f}};
    emitter = mvfxDefaultEmitterDef();
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.ribbon.color = (mvfxGradient){bright, 1, opaque, 1};
    CHECK(Taken(&emitter, 1), "a ribbon's key at the limit");
    emitter.ribbon.color = (mvfxGradient){brighter, 1, opaque, 1};
    CHECK(!Taken(&emitter, 1), "a ribbon's key past");
}

static void TestTurnsAndCustom(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.spin = mvfxRangeSource(-1e3f, 1e3f);
    emitter.overLife.rotation = (mvfxRotationOverLife){.enabled = true, .scale = {.value = 1e3f}};
    CHECK(Taken(&emitter, 1), "a spin at its cap");
    emitter.spin = mvfxRangeSource(-1e3f, Past(1e3f));
    CHECK(!Taken(&emitter, 1), "a spin past");
    // A spin read from a parameter: ends of 1e3 take a value of 1.
    emitter.spin =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {1e3f, 1e3f}, .parameter = 0};
    const float one[4] = {1.0f, 0, 0, 0};
    const float more[4] = {Past(1.0f), 0, 0, 0};
    CHECK(TakenWith(&emitter, 1, one), "a spin's parameter at its cap");
    CHECK(!TakenWith(&emitter, 1, more), "past it");
    emitter = mvfxDefaultEmitterDef();
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.angularVelocity = Range3(0, MVFX_MAX_SPIN, 0);
    CHECK(Taken(&emitter, 1), "an angular velocity at its cap");
    emitter.angularVelocity = Range3(0, 0, Past(MVFX_MAX_SPIN));
    CHECK(!Taken(&emitter, 1), "past it");
    mvfxCustomAttribute custom = {
        .components = 2,
        .initial = {.kind = mvfx_sourceRange, .min = {1e24f, -1e24f}, .max = {1e24f, -1e24f}},
        .scaledOverLife = true,
        .overLife = {.value = 1e6f}};
    emitter = mvfxDefaultEmitterDef();
    emitter.custom = &custom;
    emitter.customCount = 1;
    CHECK(Taken(&emitter, 1), "a custom attribute at its cap");
    custom.initial.max[1] = -Past(1e24f);
    CHECK(!Taken(&emitter, 1), "past it");
}

static void TestHostSpawns(void)
{
    // What the host spawns is held to the same caps: refused past them as
    // misuse, none of the request queued.
    static const mvfxAlphaKey alphas[1] = {{0.0f, 2.0f}};
    static const mvfxColorKey colors[1] = {{0.0f, 1, 1, 1}};
    mvfxEmitterDef emitter = Hosted();
    emitter.size = Range3(1, 1, 1);
    emitter.color = Range4(1, 1, 1, 1);
    emitter.overLife.size = (mvfxSizeOverLife){.enabled = true, .size = {{.value = 1e3f}}};
    emitter.overLife.color =
        (mvfxColorOverLife){.enabled = true, .gradient = {colors, 1, alphas, 1}};
    Scene scene = NewScene(&emitter, 1, 1.0f);
    static const double places[6] = {0};
    float sizes[6] = {1e3f, -1e3f, 1e3f, 1, 1, 1};
    float tints[8] = {1e6f, 1, 1, 5e5f, 1, 1, 1, 1};
    mvfxSpawnRequest request = {.count = 2, .positions = places, .colors = tints, .sizes = sizes};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success,
          "at the caps, taken");
    uint64_t misuse = mvfxGetContextMisuse(scene.context);
    sizes[4] = Past(1e3f);
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_errorLimit &&
              mvfxGetContextMisuse(scene.context) == misuse + 1,
          "a size past, refused as misuse");
    sizes[4] = 1;
    tints[7] = Past(5e5f);
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_errorLimit,
          "an alpha past");
    Step(&scene, 1);
    CHECK(Read(&scene, 0) == 2 && s_particles[0].size[1] == -1e6f &&
              s_particles[0].color[3] == 1e6f,
          "the two taken, shown at the limits");
    mvfxDestroyContext(scene.context);
}

static void TestInherited(void)
{
    // A source's particle at the limits dies; its spark inherits its
    // colour and size, held to what the spark may store (1e3 under its
    // scales of 1e3), so it shows them at the limits again.
    static const mvfxColorKey colors[1] = {{0.0f, 1e3f, 1e3f, 1e3f}};
    static const mvfxAlphaKey alphas[1] = {{0.0f, 1e3f}};
    mvfxEmitterDef emitters[2] = {Hosted(), Hosted()};
    emitters[0].lifetime = mvfxRangeSource(0.5f, 0.5f);
    emitters[0].size = Range3(1e6f, -1e6f, 1e6f);
    emitters[0].color = Range4(1e6f, -1e6f, 1e6f, 1e6f);
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 8;
    emitters[1].size = Range3(1, 1, 1);
    emitters[1].color = Range4(1, 1, 1, 1);
    emitters[1].overLife.size = (mvfxSizeOverLife){.enabled = true, .size = {{.value = 1e3f}}};
    emitters[1].overLife.color =
        (mvfxColorOverLife){.enabled = true, .gradient = {colors, 1, alphas, 1}};
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 1,
                                              .maxCount = 1,
                                              .inheritColor = true,
                                              .inheritSize = true,
                                              .capacity = 8};
    Scene scene = NewScene(emitters, 2, 1.0f);
    // Born in a step of a tick, so its death in the next writes an event.
    Spawn(&scene, 1);
    Step(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    TestParticle* spark = &s_particles[0];
    CHECK(Read(&scene, 1) == 1 && spark->size[0] == 1e6f && spark->size[1] == -1e6f &&
              spark->color[0] == 1e6f && spark->color[1] == -1e6f && spark->color[3] == 1e6f,
          "an inherited colour and size held to the spark's caps");
    mvfxDestroyContext(scene.context);
}

// Steps a scene a second at a time, n times.
static void Seconds(const Scene* scene, int32_t n)
{
    for (int32_t s = 0; s < n; ++s)
    {
        Step(scene, MVFX_TICKS_PER_SECOND);
    }
}

// A velocity's length, in binary64.
static double SpeedOf(const float v[3])
{
    return sqrt((double)v[0] * (double)v[0] + (double)v[1] * (double)v[1] +
                (double)v[2] * (double)v[2]);
}

static void TestSpeedHold(void)
{
    // Falling under 1e6 a second squared from rest, a second a step: below
    // the limit of 1e9 for 999 seconds, held at it from the thousandth on,
    // in each way a particle moves.
    for (int32_t way = 0; way < 3; ++way)
    {
        mvfxEmitterDef emitter = Hosted();
        emitter.lifetime = mvfxRangeSource(2000.0f, 2000.0f);
        emitter.motion.gravity[1] = -1e6f;
        static const mvfxCollider far = {
            .kind = mvfx_colliderPlane, .center = {0, 1e6f, 0}, .normal = {0, -1, 0}};
        if (way == 1)
        {
            // Colliding with a plane above it, which it falls away from.
            emitter.colliders = &far;
            emitter.colliderCount = 1;
            emitter.collision.enabled = true;
        }
        else if (way == 2)
        {
            // Its own drag scale, so each particle moves alone.
            emitter.motion.dragScale = mvfxRangeSource(1.0f, 1.0f);
        }
        Scene scene = NewScene(&emitter, 1, 1.0f);
        Spawn(&scene, 1);
        Step(&scene, 1);
        Seconds(&scene, 999);
        CHECK(Read(&scene, 0) == 1 && s_particles[0].velocity[1] < -9.98e8f &&
                  s_particles[0].velocity[1] > -MVFX_MAX_SPEED,
              "below the limit");
        Seconds(&scene, 2);
        CHECK(Read(&scene, 0) == 1 && s_particles[0].velocity[1] == -MVFX_MAX_SPEED, "held at it");
        mvfxDestroyContext(scene.context);
    }
    // A world-space emitter's limit is its instance's scale times it, in
    // the world: twice 1e9 at a scale of 2.
    mvfxEmitterDef emitter = Hosted();
    emitter.lifetime = mvfxRangeSource(3000.0f, 3000.0f);
    emitter.space = mvfx_spaceWorld;
    emitter.motion.gravity[1] = -1e6f;
    Scene scene = NewScene(&emitter, 1, 2.0f);
    Spawn(&scene, 1);
    Step(&scene, 1);
    Seconds(&scene, 1999);
    CHECK(Read(&scene, 0) == 1 && s_particles[0].velocity[1] > -2e9f, "below the scale's");
    Seconds(&scene, 2);
    CHECK(Read(&scene, 0) == 1 && s_particles[0].velocity[1] == -2e9f, "held at the scale's");
    mvfxDestroyContext(scene.context);
    // A birth set off at the fastest valid speed under the strongest
    // constant force, 1e12 a second squared, is held as it moves its first
    // step.
    static const mvfxForce strongest = {.kind = mvfx_forceConstant,
                                        .vector = {0, -MVFX_MAX_EXTENT, 0},
                                        .strength = MVFX_MAX_EXTENT};
    emitter = Hosted();
    emitter.forces = &strongest;
    emitter.forceCount = 1;
    scene = NewScene(&emitter, 1, 1.0f);
    static const double place[3] = {0};
    static const float down[3] = {0, -MVFX_MAX_EXTENT, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = down};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success,
          "set off");
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Read(&scene, 0) == 1 && s_particles[0].velocity[1] == -MVFX_MAX_SPEED, "a birth held");
    mvfxDestroyContext(scene.context);
    // A diagonal velocity is held to the bound's speed along its own
    // heading: under a force of 1e12 along X and 5e11 along Y, held from
    // about (1e12, 5e11) to 1e9 at half as much Y as X, not to (1e9, 1e9).
    static const mvfxForce diagonal = {
        .kind = mvfx_forceConstant, .vector = {1e6f, 5e5f, 0}, .strength = 1e6f};
    emitter = Hosted();
    emitter.forces = &diagonal;
    emitter.forceCount = 1;
    scene = NewScene(&emitter, 1, 1.0f);
    Spawn(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    bool read = Read(&scene, 0) == 1;
    const float* v = s_particles[0].velocity;
    CHECK(read && fabs(SpeedOf(v) / (double)MVFX_MAX_SPEED - 1.0) < 0x1p-22 &&
              fabs((double)v[1] / (double)v[0] - 0.5) < 0x1p-22 && v[2] == 0.0f,
          "a diagonal velocity held along its heading");
    mvfxDestroyContext(scene.context);
    // Within the bound on each axis, past it in speed: built to about
    // (8e8, 8e8) in a second, held to 1e9 at 45 degrees.
    static const mvfxForce even = {
        .kind = mvfx_forceConstant, .vector = {8e5f, 8e5f, 0}, .strength = 1e3f};
    emitter = Hosted();
    emitter.forces = &even;
    emitter.forceCount = 1;
    scene = NewScene(&emitter, 1, 1.0f);
    Spawn(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    read = Read(&scene, 0) == 1;
    CHECK(read && v[0] < 8e8f && v[0] == v[1] &&
              fabs(SpeedOf(v) / (double)MVFX_MAX_SPEED - 1.0) < 0x1p-22,
          "a speed past the bound, each axis within it, held");
    mvfxDestroyContext(scene.context);
}

// What a module leaves, once s_acting is set: s_leave picks it, on the
// first particle.
static int32_t s_leave;
static bool s_acting;
static void Leave(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* a = &call->particles;
    if (!s_acting || a->count == 0)
    {
        return;
    }
    switch (s_leave)
    {
    case 0:
        a->position[0][0] = NAN;
        break;
    case 1:
        // Within the bound on each axis, past it in speed.
        a->velocity[0][0] = 8e8f;
        a->velocity[1][0] = 0.0f;
        a->velocity[2][0] = -8e8f;
        break;
    case 2:
        a->size[1][0] = Past(1e3f);
        break;
    case 3:
        a->color[3][0] = Past(MVFX_MAX_COLOR);
        break;
    case 4:
        a->angle[0] = NAN;
        break;
    case 5:
        a->spin[0] = Past(MVFX_MAX_SPIN);
        break;
    case 6:
        a->custom[0][1][0] = -Past(MVFX_MAX_VALUE);
        break;
    case 7:
        a->orientation[3][0] = NAN;
        break;
    case 8:
        a->angularVelocity[0][0] = Past(MVFX_MAX_SPIN);
        break;
    case 9:
        // At the limits: nothing killed.
        a->velocity[0][0] = MVFX_MAX_SPEED;
        a->size[0][0] = -1e3f;
        a->spin[0] = -MVFX_MAX_SPIN;
        a->angularVelocity[2][0] = MVFX_MAX_SPIN;
        a->custom[0][0][0] = MVFX_MAX_VALUE;
        break;
    default:
        // Its own kill, of a particle it also leaves past a limit: killed.
        a->size[0][0] = INFINITY;
        if (a->kill != NULL)
        {
            a->kill[0] = 1;
        }
        break;
    }
}

static void TestModules(void)
{
    mvfxCustomAttribute custom = {.components = 2};
    for (int32_t stage = 0; stage < 2; ++stage)
    {
        for (s_leave = 0; s_leave < 11; ++s_leave)
        {
            if (s_leave == 10 && stage == mvfx_stageBeforeMotion)
            {
                continue;
            }
            const mvfxHostModule module = {
                .stage = (mvfxModuleStage)stage, .function = Leave, .user = NULL};
            mvfxEmitterDef emitter = Hosted();
            emitter.size = Range3(1, 1, 1);
            emitter.color = Range4(1, 1, 1, 1);
            emitter.angle = mvfxRangeSource(0.0f, 0.0f);
            emitter.spin = mvfxRangeSource(0.0f, 0.0f);
            emitter.orientationMode = mvfx_orientationRandom;
            emitter.angularVelocity = Range3(0, 0, 0);
            emitter.overLife.size = (mvfxSizeOverLife){.enabled = true, .size = {{.value = 1e3f}}};
            emitter.custom = &custom;
            emitter.customCount = 1;
            emitter.hostModules = &module;
            emitter.hostModuleCount = 1;
            emitter.events = mvfx_eventMaskDeath;
            emitter.eventCapacity = 8;
            Scene scene = NewScene(&emitter, 1, 1.0f);
            Spawn(&scene, 2);
            s_acting = false;
            Step(&scene, 1);
            s_acting = true;
            Step(&scene, 1);
            mvfxEmitterStats stats;
            Stats(&scene, 0, &stats);
            int32_t live = Read(&scene, 0);
            bool kept = s_leave == 9;
            bool own = s_leave == 10;
            CHECK(live == (kept ? 2 : 1) && stats.valuesKilled == (kept || own ? 0u : 1u) &&
                      stats.killed == (own ? 1u : 0u),
                  stage == 0 ? "before motion: killed and counted apart"
                             : "after motion: killed and counted apart");
            if (stage == mvfx_stageBeforeMotion && s_leave == 1)
            {
                // Taken out before the step moves it: its death is where
                // the step found it, not a tick along its fast velocity.
                const mvfxEvent* events = NULL;
                int32_t count = 0;
                CHECK(mvfxInstance_GetEvents(scene.context, scene.instance, 0, &events, &count) ==
                              mvfx_success &&
                          count == 1 && fabs(events[0].place[2]) < 0.01,
                      "before motion: taken out unmoved");
            }
            mvfxDestroyContext(scene.context);
        }
    }
}

// After motion, once s_placing is set: a place past MVFX_MAX_PLACE (yet
// finite) on the first particle, one at it on the second.
static bool s_placing;
static void LeavePlaces(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* a = &call->particles;
    if (s_placing && a->count == 2)
    {
        a->position[0][0] = 1e38f;
        a->position[0][1] = MVFX_MAX_PLACE;
    }
}

static void TestModulePlaces(void)
{
    // A local emitter under the largest scale: the place past the limit
    // would be infinite in the world; the one at it is 1e36 there.
    const mvfxHostModule module = {
        .stage = mvfx_stageAfterMotion, .function = LeavePlaces, .user = NULL};
    mvfxEmitterDef emitter = Hosted();
    emitter.space = mvfx_spaceLocal;
    emitter.hostModules = &module;
    emitter.hostModuleCount = 1;
    Scene scene = NewScene(&emitter, 1, MVFX_MAX_SCALE);
    Spawn(&scene, 2);
    s_placing = false;
    Step(&scene, 1);
    s_placing = true;
    Step(&scene, 1);
    mvfxEmitterStats stats;
    Stats(&scene, 0, &stats);
    static const mvfxLayoutField fields[1] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3}};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &layout) == mvfx_success, "layout");
    float records[4][3] = {{0}};
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      records, 4, &count) == mvfx_success,
          "records written");
    bool finite = true;
    for (int64_t i = 0; i < count; ++i)
    {
        finite =
            finite && isfinite(records[i][0]) && isfinite(records[i][1]) && isfinite(records[i][2]);
    }
    CHECK(stats.valuesKilled == 1u && count == 1 && finite &&
              records[0][0] == MVFX_MAX_PLACE * MVFX_MAX_SCALE,
          "a module's place past it killed, the records finite");
    mvfxDestroyContext(scene.context);
}

int main(void)
{
    TestLifeCurves();
    TestSizes();
    TestColors();
    TestTurnsAndCustom();
    TestHostSpawns();
    TestInherited();
    TestSpeedHold();
    TestModules();
    TestModulePlaces();
    return s_failures == 0 ? 0 : 1;
}
