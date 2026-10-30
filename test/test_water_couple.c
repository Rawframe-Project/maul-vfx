// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles meeting water: the refusals; a particle at rest floating at g
// / buoyancy below a still surface, and one of a radius below binary32's
// least normal at it, its instance still or moving; drag
// slowing one in still water and carrying one with a flow; crossings met
// once each where and when they cross, their events, and the pushes they
// leave with the volume kept; once a step through substeps; through water
// into the floor, not over dry ground; pours adding exactly their
// particles' volume and killing them, and one past binary32's range held
// at MVFX_MAX_WATER_DEPTH. Particles are read from their
// records, surfaces through the output, and a shallow flow is held
// through a test hook.

#include "particles.h"
#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/water.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// A coupling to surface 0 of particles of radius 0.05.
static mvfxWaterCoupling Coupling(void)
{
    return (mvfxWaterCoupling){.enabled = true, .water = 0, .radius = 0.05f};
}

// An emitter of up to 16 particles the host places, living 100 s, writing
// crossings and deaths, meeting the water by coupling.
static mvfxEmitterDef Coupled(const mvfxWaterCoupling* coupling)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 16;
    emitter.hostSpawnCapacity = 16;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.water = *coupling;
    emitter.events = mvfx_eventMaskWater | mvfx_eventMaskDeath;
    emitter.eventCapacity = 64;
    return emitter;
}

// A surface of 16 by 16 cells of 0.1 of a model, shallow water filled to
// 0.2.
static mvfxWaterDef Surface(mvfxWaterModel model)
{
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.model = model;
    water.size[0] = 16;
    water.size[1] = 16;
    water.cellSize = 0.1f;
    water.fillLevel = 0.2f;
    return water;
}

// Opens a context and an effect of the emitters and the surface, its
// instance at the origin; false when the effect is refused.
static bool Open(const mvfxEmitterDef* emitters, int32_t count, const mvfxWaterDef* water)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = count;
    def.waters = water;
    def.waterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect) != mvfx_success)
    {
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    return true;
}

static void Close(void)
{
    mvfxDestroyContext(s_context);
    s_context = NULL;
}

static void Place(double x, double y, double z, float vx, float vy, float vz)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, vy, vz};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "placed");
}

// The emitter's live particles as the output writes them.
static int32_t Particles(TestParticle out[16])
{
    int32_t count = ReadParticles(s_context, s_instance, 0, out, 16);
    CHECK(count >= 0, "records");
    return count;
}

// Particle i's record.
static TestParticle Particle(int32_t i)
{
    TestParticle particles[16];
    int32_t count = Particles(particles);
    CHECK(i < count, "a live particle");
    return i < count ? particles[i] : (TestParticle){0};
}

// The last step's events of a kind, at most 16.
static int32_t Events(mvfxEventKind kind, const mvfxEvent* found[16])
{
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    CHECK(mvfxInstance_GetEvents(s_context, s_instance, 0, &events, &count) == mvfx_success,
          "events");
    int32_t kept = 0;
    for (int32_t e = 0; e < count && kept < 16; ++e)
    {
        if (events[e].kind == kind)
        {
            found[kept++] = &events[e];
        }
    }
    return kept;
}

// Spoils a valid coupling of a waves surface by case c (none for 10).
static void Spoil(int32_t c, mvfxEmitterDef* emitter)
{
    mvfxWaterCoupling* coupling = &emitter->water;
    switch (c)
    {
    case 0:
        coupling->water = 1;
        break;
    case 1:
        coupling->water = -1;
        break;
    case 2:
        coupling->radius = 0.0f;
        break;
    case 3:
        coupling->radius = 2e6f;
        break;
    case 4:
        coupling->push = -1.0f;
        break;
    case 5:
        coupling->pour = true;
        break;
    case 6:
        coupling->buoyancy = -1.0f;
        break;
    case 7:
        coupling->drag = 1001.0f;
        break;
    case 8:
        coupling->enabled = false;
        break;
    case 9:
        emitter->events |= 1u << (mvfx_eventWater + 1);
        break;
    default:
        break;
    }
}

static void TestRefusals(void)
{
    // Each spoiled coupling refused (a pour on waves; crossings' events
    // with no coupling); a valid one opened, a pour on shallow water too,
    // and an emitter spawning from crossings.
    bool right = true;
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    for (int32_t c = 0; c <= 10; ++c)
    {
        mvfxWaterCoupling coupling = Coupling();
        coupling.push = 0.01f;
        coupling.buoyancy = 20.0f;
        coupling.drag = 1.0f;
        mvfxEmitterDef emitter = Coupled(&coupling);
        Spoil(c, &emitter);
        right = right && Open(&emitter, 1, &waves) == (c == 10);
        Close();
    }
    CHECK(right, "a spoiled coupling refused, a valid one opened");
    mvfxWaterDef shallow = Surface(mvfx_waterShallow);
    mvfxWaterCoupling pour = Coupling();
    pour.pour = true;
    mvfxEmitterDef emitters[2] = {Coupled(&pour), mvfxDefaultEmitterDef()};
    emitters[1].capacity = 16;
    emitters[1].rate = 0.0f;
    emitters[1].lifetime = mvfxRangeSource(1.0f, 1.0f);
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventWater,
                                              .minCount = 1,
                                              .maxCount = 1,
                                              .capacity = 16};
    CHECK(Open(emitters, 2, &shallow), "a pour on shallow water, splashes from crossings");
    Close();
    emitters[1].eventSpawn.kind = mvfx_eventWater + 1;
    CHECK(!Open(emitters, 2, &shallow), "an unknown event kind refused");
    Close();
}

static void TestRoom(void)
{
    // Room for a crossing a particle: all 16 of an emitter's particles,
    // meeting surface 1 of two, falling at 1 from 0.5 above it, each
    // crossing it in the same step, are each met. That the surface it
    // does not meet has no room is in test_water_couple_internal.c.
    mvfxWaterCoupling coupling = Coupling();
    coupling.water = 1;
    mvfxEmitterDef emitter = Coupled(&coupling);
    const mvfxWaterDef waters[2] = {Surface(mvfx_waterWaves), Surface(mvfx_waterWaves)};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    def.waters = waters;
    def.waterCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    for (int32_t p = 0; p < 16; ++p)
    {
        Place(-0.6 + 0.4 * (double)(p % 4), 0.5, -0.6 + 0.4 * (double)(p / 4), 0.0f, -1.0f, 0.0f);
    }
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "born");
    CHECK(mvfxContext_Step(s_context, 1.0) == mvfx_success, "stepped");
    const mvfxEvent* events[16];
    CHECK(Events(mvfx_eventWater, events) == 16, "room for every particle's crossing");
    Close();
}

static void TestFloat(void)
{
    // Under gravity at buoyancy 1.25, a particle of radius 0.05 let go at
    // rest 0.1 below a still surface settles with its center r (2 / 1.25 -
    // 1) = 0.03 below it (drag 2 damps it).
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 1.25f;
    coupling.drag = 2.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    emitter.motion.gravity[1] = -9.81f;
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(0.0, -0.1, 0.0, 0.0f, 0.0f, 0.0f);
    for (int32_t s = 0; s < 600; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    float y = Particle(0).position[1];
    CHECK(fabsf(y + 0.03f) <= 1e-4f && fabsf(Particle(0).velocity[1]) <= 1e-3f,
          "a particle floats at g / buoyancy");
    Close();
    // A radius of 1e-38, below binary32's least normal: its lift's spring,
    // buoyancy times g over twice the radius, passes binary32's range. It
    // lifts by the spring's limit and rests at the surface, its place held
    // there each step against gravity.
    coupling.radius = 1e-38f;
    emitter = Coupled(&coupling);
    emitter.motion.gravity[1] = -9.81f;
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(0.0, -0.1, 0.0, 0.0f, 0.0f, 0.0f);
    for (int32_t s = 0; s < 600; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    y = Particle(0).position[1];
    CHECK(fabsf(y) <= 1e-4f && isfinite(Particle(0).velocity[1]),
          "a particle of a radius below binary32's least normal floats at the surface");
    Close();
}

static void TestMoving(void)
{
    // The float as TestFloat's, its instance moving 2 a step along X: the
    // particle reads the surface as it last stepped, where each step
    // starts, so it floats as still.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 1.25f;
    coupling.drag = 2.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    emitter.space = mvfx_spaceLocal;
    emitter.motion.gravity[1] = -9.81f;
    emitter.motion.gravitySpace = mvfx_spaceLocal;
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(0.0, -0.1, 0.0, 0.0f, 0.0f, 0.0f);
    static const float unturned[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int32_t s = 0; s < 600; ++s)
    {
        const double origin[3] = {2.0 * (s + 1), 0.0, 0.0};
        CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, unturned, 1.0f, false) ==
                  mvfx_success,
              "moved");
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    CHECK(fabsf(Particle(0).position[1] + 0.03f) <= 1e-4f, "a moving instance's particle floats");
    Close();
}

static void TestScaled(void)
{
    // The float as TestFloat's of a world particle, its instance scaled 2:
    // 0.03 deep in the world, the surface's frame scaled away.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 1.25f;
    coupling.drag = 2.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    emitter.motion.gravity[1] = -9.81f;
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    static const double origin[3] = {0.0, 0.0, 0.0};
    static const float unturned[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, unturned, 2.0f, true) ==
              mvfx_success,
          "scaled");
    Place(0.0, -0.1, 0.0, 0.0f, 0.0f, 0.0f);
    for (int32_t s = 0; s < 600; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    CHECK(fabsf(Particle(0).position[1] + 0.03f) <= 1e-4f,
          "a scaled instance's particle floats as deep");
    Close();
}

// A particle of radius r at buoyancy and drag under gravity, let go at
// rest at y below a still surface: where it is after 10 s, and the
// highest it rose.
static float Settle(float r, float buoyancy, float drag, double y, float* top)
{
    mvfxWaterCoupling coupling = Coupling();
    coupling.radius = r;
    coupling.buoyancy = buoyancy;
    coupling.drag = drag;
    mvfxEmitterDef emitter = Coupled(&coupling);
    emitter.motion.gravity[1] = -9.81f;
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(0.0, y, 0.0, 0.0f, 0.0f, 0.0f);
    *top = -INFINITY;
    for (int32_t s = 0; s < 600; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
        *top = fmaxf(*top, Particle(0).position[1]);
    }
    float at = Particle(0).position[1];
    Close();
    return at;
}

static void TestDeep(void)
{
    // At the largest buoyancy and drag, let go 5 deep, a particle rises
    // and rests with its center r (1 - 2 / 1000) above the surface, never
    // past it by r / 25 (1.4 mm, measured; it leapt 5 m when the lift was
    // taken where the step starts); a small one (radius 0.001, its band's stiffness 9,810 times
    // dt^2 = 2.7) at buoyancy 2 rests half under, at the surface, rather
    // than ringing; at 0.5 one sinks.
    float top = 0.0f;
    float high = Settle(0.05f, MVFX_MAX_BUOYANCY, MVFX_MAX_DRAG, -5.0, &top);
    float rest = 0.05f * (1.0f - 2.0f / MVFX_MAX_BUOYANCY);
    CHECK(fabsf(high - rest) <= 1e-4f && top <= rest + 0.05f / 25.0f, "the stiffest float rests");
    float small = Settle(0.001f, 2.0f, 2.0f, -0.1, &top);
    CHECK(fabsf(small) <= 1e-5f, "a stiff band settles");
    CHECK(Settle(0.05f, 0.5f, 2.0f, -0.1, &top) < -1.0f, "a particle too dense to float sinks");
}

static void TestUnderground(void)
{
    // Below shallow water's floor a particle is not in the water: let go
    // at rest there, no buoyancy lifts it and it falls freely.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 20.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    emitter.motion.gravity[1] = -9.81f;
    mvfxWaterDef shallow = Surface(mvfx_waterShallow);
    CHECK(Open(&emitter, 1, &shallow), "opened");
    CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "filled");
    Place(0.0, -0.3, 0.0, 0.0f, 0.0f, 0.0f);
    for (int32_t s = 0; s < 30; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    CHECK(fabsf(Particle(0).position[1] + 0.3f + 0.5f * 9.81f * 0.25f) <= 1e-3f,
          "nothing lifts one below the floor");
    Close();
}

static void TestDrag(void)
{
    // In still water a particle's speed falls as exp(-drag t): drag 2 for
    // half a second leaves exp(-1) of it.
    mvfxWaterCoupling coupling = Coupling();
    coupling.drag = 2.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(-0.4, -0.2, 0.0, 1.0f, 0.0f, 0.0f);
    for (int32_t s = 0; s < 30; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    CHECK(fabsf(Particle(0).velocity[0] - expf(-1.0f)) <= 1e-5f, "drag slows it");
    Close();
}

static void TestFlow(void)
{
    // Shallow water 0.2 deep flowing at 0.5 along X (its faces held so
    // each step): a particle at rest in it, drag 10, takes the flow's speed
    // within 1e-3 in a second.
    mvfxWaterCoupling coupling = Coupling();
    coupling.drag = 10.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    mvfxWaterDef shallow = Surface(mvfx_waterShallow);
    CHECK(Open(&emitter, 1, &shallow), "opened");
    CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "filled");
    Place(-0.5, 0.1, 0.0, 0.0f, 0.0f, 0.0f);
    static float u[17 * 16];
    for (int32_t i = 0; i < 17 * 16; ++i)
    {
        u[i] = 0.5f;
    }
    for (int32_t s = 0; s < 60; ++s)
    {
        CHECK(mvfxTestWriteWaterFaces(s_context, s_instance, 0, 0, u, 17 * 16), "held");
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    CHECK(fabsf(Particle(0).velocity[0] - 0.5f) <= 1e-3f, "drag carries it with the flow");
    Close();
}

// Surface 0's field over its 16 by 16 cells.
static const float* Field(mvfxWaterField field)
{
    static float values[256];
    const mvfxWaterRead read = {.field = field, .format = mvfx_formatFloat32, .size = {16, 16}};
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &read, values, sizeof values) ==
              mvfx_success,
          "read");
    return values;
}

// The sum of a waves surface's heights, and its lowest.
static double Heights(float* lowest)
{
    const float* u = Field(mvfx_waterHeight);
    double sum = 0.0;
    *lowest = 0.0f;
    for (int32_t i = 0; i < 256; ++i)
    {
        sum += (double)u[i];
        *lowest = fminf(*lowest, u[i]);
    }
    return sum;
}

static void TestCrossings(void)
{
    // Four particles falling at 1 from 0.5 above a still surface cross it
    // at 0.5 s, once each: their events there and then (to a tick), its
    // normal up; their pushes sink the surface, the volume kept.
    mvfxWaterCoupling coupling = Coupling();
    coupling.push = 0.02f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    for (int32_t p = 0; p < 4; ++p)
    {
        Place(-0.3 + 0.2 * p, 0.5, 0.1, 0.0f, -1.0f, 0.0f);
    }
    int32_t crossings = 0;
    int32_t wrong = 0;
    float lowest = 0.0f;
    double sum = 0.0;
    for (int32_t s = 0; s < 60; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
        const mvfxEvent* events[16];
        int32_t found = Events(mvfx_eventWater, events);
        crossings += found;
        for (int32_t e = 0; e < found; ++e)
        {
            const mvfxEvent* event = events[e];
            wrong += fabs(event->place[1]) > 1e-4 || fabsf(event->normal[1] - 1.0f) > 1e-6f;
            wrong += llabs(event->tick - MVFX_TICKS_PER_SECOND / 2) > 1;
        }
        float low = 0.0f;
        sum = Heights(&low);
        lowest = fminf(lowest, low);
    }
    CHECK(crossings == 4 && wrong == 0, "each crossing met once, where and when");
    CHECK(lowest < -1e-4f && fabs(sum) <= 1e-4, "crossings push the surface, its volume kept");
    Close();
}

static void TestOnce(void)
{
    // A liquid's particle let go 0.3 above a still surface at buoyancy 4
    // and no drag falls through it, is thrown back out (fully under it
    // rises at 3 g) and falls through again within a second's step of 32
    // substeps: its crossing is met once that step.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 4.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    emitter.motion.gravity[1] = -9.81f;
    emitter.liquid.enabled = true;
    emitter.liquid.substeps = 32;
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(0.0, 0.3, 0.0, 0.0f, 0.0f, 0.0f);
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "born");
    CHECK(mvfxContext_Step(s_context, 1.0) == mvfx_success, "stepped");
    const mvfxEvent* events[16];
    CHECK(Events(mvfx_eventWater, events) == 1, "one crossing a step");
    Close();
}

// A vector turned by a unit quaternion (x, y, z, w): v + 2 w (q x v) +
// 2 q x (q x v).
static void Rotate(const float q[4], const float v[3], float out[3])
{
    float t[3] = {2.0f * (q[1] * v[2] - q[2] * v[1]), 2.0f * (q[2] * v[0] - q[0] * v[2]),
                  2.0f * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

// A world particle falling at 1 from 0.5 above a surface of a model at
// (0.24, 0.1) in its frame (with a twin, none), the instance scaled 2 and
// turned by rotation, filled to 0 (shallow water to 0.1 of the frame's
// lengths), stepped a second (the twin pushed by the host at that point
// first, by radius and depth in the world's lengths): its heights after,
// its depths' sum, the step's crossing event and the volume poured.
static const float* Crossed(mvfxWaterModel model, const float rotation[4], bool twin,
                            const mvfxWaterPush* push, double* depths, mvfxEvent* event,
                            double* poured)
{
    mvfxWaterCoupling coupling = Coupling();
    coupling.push = 0.02f;
    coupling.pour = model == mvfx_waterShallow;
    mvfxEmitterDef emitter = Coupled(&coupling);
    mvfxWaterDef water = Surface(model);
    water.fillLevel = 0.0f;
    CHECK(Open(&emitter, 1, &water), "opened");
    static const double origin[3] = {0.0, 0.0, 0.0};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, rotation, 2.0f, true) ==
              mvfx_success,
          "placed");
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "filled");
    static float filled[256];
    for (int32_t i = 0; model == mvfx_waterShallow && i < 256; ++i)
    {
        filled[i] = 0.1f;
    }
    CHECK(model != mvfx_waterShallow ||
              mvfxTestWriteWaterDepth(s_context, s_instance, 0, filled, 256),
          "shallow water 0.1 deep");
    // Falling along the frame's -Y from 0.5 above its point (0.12, 0.05)
    // in the world's lengths, between two cells' centers.
    const float local[3] = {0.24f, 0.5f, 0.1f};
    const float down[3] = {0.0f, -1.0f, 0.0f};
    float place[3];
    float velocity[3];
    Rotate(rotation, local, place);
    Rotate(rotation, down, velocity);
    if (!twin)
    {
        Place((double)place[0], (double)place[1], (double)place[2], velocity[0], velocity[1],
              velocity[2]);
    }
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "born");
    CHECK(!twin || mvfxInstance_PushWater(s_context, s_instance, 0, push, 1) == mvfx_success,
          "pushed");
    CHECK(mvfxContext_Step(s_context, 1.0) == mvfx_success, "stepped");
    static float heights[256];
    memcpy(heights, Field(mvfx_waterHeight), sizeof heights);
    const float* d = Field(mvfx_waterDepth);
    *depths = 0.0;
    for (int32_t i = 0; i < 256; ++i)
    {
        *depths += (double)d[i];
    }
    const mvfxEvent* events[16];
    *event = Events(mvfx_eventWater, events) == 1 ? *events[0] : (mvfxEvent){0};
    mvfxWaterInfo info;
    CHECK(mvfxInstance_GetWaterInfo(s_context, s_instance, 0, &info) == mvfx_success, "info");
    *poured = info.volumePoured;
    Close();
    return heights;
}

static void TestRecorded(void)
{
    // In the surface's frame, half the world's lengths: the point (0.12,
    // 0.05), the particle's radius 0.025, so the push's radius a cell (0.1)
    // and its depth 0.01 (0.025 / 0.1)^2. A waves surface after the step
    // is the twin's the host pushed there by those (0.2 and 0.00125 in the
    // world's), to 1e-3 of the depth. A pour's volume is a sphere of 0.025
    // (shallow water 0.1 deep there, its crossing 0.1 lower): the depths
    // rise by it, and it counts in the world as a sphere of 0.05. Turned
    // a quarter about Z, the event's normal is the frame's Y in the world.
    static const float unturned[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    const mvfxWaterPush push = {.point = {0.24, 0.0, 0.1}, .radius = 0.2f, .depth = 0.00125f};
    mvfxEvent event;
    double poured = 0.0;
    double depths = 0.0;
    static float crossed[256];
    memcpy(crossed, Crossed(mvfx_waterWaves, unturned, false, NULL, &depths, &event, &poured),
           sizeof crossed);
    int32_t wrong = llabs(event.tick - MVFX_TICKS_PER_SECOND / 2) > 1;
    wrong += poured != 0.0;
    const float* pushed = Crossed(mvfx_waterWaves, unturned, true, &push, &depths, &event, &poured);
    float lowest = 0.0f;
    for (int32_t i = 0; i < 256; ++i)
    {
        lowest = fminf(lowest, pushed[i]);
        wrong += fabsf(crossed[i] - pushed[i]) > 1e-3f * 0.000625f;
    }
    wrong += !(lowest < -1e-5f);
    double before = 0.0;
    (void)Crossed(mvfx_waterShallow, unturned, true, &push, &before, &event, &poured);
    (void)Crossed(mvfx_waterShallow, unturned, false, NULL, &depths, &event, &poured);
    double sphere = 4.0 / 3.0 * 3.14159265358979323846 * 0.025 * 0.025 * 0.025;
    wrong += fabs((depths - before) * 0.01 / sphere - 1.0) > 1e-3;
    wrong += fabs(poured / (4.0 / 3.0 * 3.14159265358979323846 * 0.05 * 0.05 * 0.05) - 1.0) > 1e-6;
    wrong += llabs(event.tick - 3 * MVFX_TICKS_PER_SECOND / 10) > 1;
    (void)Crossed(mvfx_waterWaves, quarter, false, NULL, &depths, &event, &poured);
    wrong += fabsf(event.normal[0] + 1.0f) > 1e-5f || fabsf(event.normal[1]) > 1e-5f;
    CHECK(wrong == 0, "a crossing recorded in the surface's frame");
}

static void TestOnSurface(void)
{
    // A particle let go on the surface itself, moving down, crosses it.
    mvfxEmitterDef emitter = Coupled(&(mvfxWaterCoupling){.enabled = true, .radius = 0.05f});
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(0.0, 0.0, 0.0, 0.0f, -1.0f, 0.0f);
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "born");
    CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    const mvfxEvent* events[16];
    CHECK(Events(mvfx_eventWater, events) == 1, "a crossing from the surface");
    Close();
}

// A particle falling at 10 from 0.5 for 0.1 s, through shallow water
// filled to level onto its floor and past it: its crossings.
static int32_t Through(float level)
{
    mvfxEmitterDef emitter = Coupled(&(mvfxWaterCoupling){.enabled = true, .radius = 0.05f});
    mvfxWaterDef shallow = Surface(mvfx_waterShallow);
    shallow.fillLevel = level;
    CHECK(Open(&emitter, 1, &shallow), "opened");
    CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "filled");
    Place(0.0, 0.5, 0.0, 0.0f, -10.0f, 0.0f);
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "born");
    CHECK(mvfxContext_Step(s_context, 0.1) == mvfx_success, "stepped");
    const mvfxEvent* events[16];
    int32_t found = Events(mvfx_eventWater, events);
    Close();
    return found;
}

static void TestThrough(void)
{
    // Through water into the floor within a step it crosses; over dry
    // ground, none.
    CHECK(Through(0.2f) == 1 && Through(0.0f) == 0, "crossings over water, not dry ground");
}

// A shallow surface's volume.
static double Volume(void)
{
    const float* d = Field(mvfx_waterDepth);
    double sum = 0.0;
    for (int32_t i = 0; i < 256; ++i)
    {
        sum += (double)d[i];
    }
    return sum * 0.01;
}

static void TestPour(void)
{
    // Eight particles of radius 0.05 falling into shallow water 0.2 deep
    // pour: each dies (a kill) as its sphere's volume joins the water.
    mvfxWaterCoupling coupling = Coupling();
    coupling.pour = true;
    mvfxEmitterDef emitter = Coupled(&coupling);
    mvfxWaterDef shallow = Surface(mvfx_waterShallow);
    CHECK(Open(&emitter, 1, &shallow), "opened");
    CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "filled");
    double start = Volume();
    for (int32_t p = 0; p < 8; ++p)
    {
        Place(-0.35 + 0.1 * p, 0.5, -0.2 + 0.05 * p, 0.0f, -1.0f, 0.0f);
    }
    int32_t deaths = 0;
    int32_t kills = 0;
    for (int32_t s = 0; s < 60; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
        const mvfxEvent* events[16];
        int32_t found = Events(mvfx_eventDeath, events);
        deaths += found;
        for (int32_t e = 0; e < found; ++e)
        {
            kills += events[e]->killed ? 1 : 0;
        }
    }
    double poured = 8.0 * 4.0 / 3.0 * 3.14159265358979323846 * 0.05 * 0.05 * 0.05;
    TestParticle left[16];
    CHECK(Particles(left) == 0 && deaths == 8 && kills == 8, "poured particles die");
    CHECK(fabs((Volume() - start) / poured - 1.0) <= 1e-4, "their volume joins the water");
    mvfxWaterInfo info;
    CHECK(mvfxInstance_GetWaterInfo(s_context, s_instance, 0, &info) == mvfx_success &&
              fabs(info.volumePoured / poured - 1.0) <= 1e-6,
          "the volume poured counted");
    CHECK(mvfxInstance_Restart(s_context, s_instance) == mvfx_success &&
              mvfxInstance_GetWaterInfo(s_context, s_instance, 0, &info) == mvfx_success &&
              info.volumePoured == 0.0,
          "a restart clears it");
    Close();
}

static void TestPourHeld(void)
{
    // A particle of radius MVFX_MAX_EXTENT in the world falls into shallow
    // water of cells of 2^-19, the instance at a scale of 2^-19: in the
    // surface's lengths its sphere is 5.2e11 across, and the 6e35 of its
    // volume over a cell's 3.6e-12 passes binary32's largest. The cell it
    // pours into is held at MVFX_MAX_WATER_DEPTH, which the step's flow
    // then spreads with the volume kept, every depth finite; the volume
    // poured counts in the world.
    mvfxWaterCoupling coupling = Coupling();
    coupling.radius = MVFX_MAX_EXTENT;
    coupling.pour = true;
    mvfxEmitterDef emitter = Coupled(&coupling);
    mvfxWaterDef shallow = Surface(mvfx_waterShallow);
    shallow.cellSize = 0x1p-19f;
    shallow.fillLevel = 0x1p-17f;
    CHECK(Open(&emitter, 1, &shallow), "opened");
    static const double origin[3] = {0.0, 0.0, 0.0};
    static const float upright[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, upright, 0x1p-19f, true) ==
              mvfx_success,
          "scaled");
    CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "filled");
    // Over cell (8, 8)'s center, falling from 16 cells above the ground to
    // 2, through the surface at 4.
    Place(0x1p-39, 0x1p-34, 0x1p-39, 0.0f, -(0x1p-34f - 0x1p-37f), 0.0f);
    CHECK(mvfxContext_Step(s_context, 1.0) == mvfx_success, "poured");
    const float* d = Field(mvfx_waterDepth);
    int32_t wrong = 0;
    double sum = 0.0;
    for (int32_t i = 0; i < 256; ++i)
    {
        wrong += !(d[i] >= 0.0f && d[i] <= MVFX_MAX_WATER_DEPTH);
        sum += (double)d[i];
    }
    CHECK(wrong == 0 && fabs(sum / (double)MVFX_MAX_WATER_DEPTH - 1.0) <= 1e-4,
          "the cell poured into held at MVFX_MAX_WATER_DEPTH, the step's flow keeping it");
    mvfxWaterInfo info;
    double poured = 4.0 / 3.0 * 3.14159265358979323846 * 1e18;
    CHECK(mvfxInstance_GetWaterInfo(s_context, s_instance, 0, &info) == mvfx_success &&
              fabs(info.volumePoured / poured - 1.0) <= 1e-6,
          "the volume poured counted in the world");
    Close();
}

int main(void)
{
    TestRefusals();
    TestRoom();
    TestFloat();
    TestMoving();
    TestScaled();
    TestDeep();
    TestUnderground();
    TestDrag();
    TestFlow();
    TestCrossings();
    TestOnce();
    TestRecorded();
    TestOnSurface();
    TestThrough();
    TestPour();
    TestPourHeld();
    return s_failures;
}
