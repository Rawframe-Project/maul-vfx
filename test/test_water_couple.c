// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles meeting water: the refusals; a particle at rest floating at g
// / buoyancy below a still surface, its instance still or moving; drag
// slowing one in still water and carrying one with a flow; crossings met
// once each where and when they cross, their events, and the pushes they
// leave with the volume kept; once a step through substeps; through water
// into the floor, not over dry ground; pours adding exactly their
// particles' volume and killing them.

#include "context.h"
#include "direction.h"
#include "instance.h"
#include "instance_api.h"
#include "step.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;
static mvfxInstance* s_live;

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
    s_live = mvfxContextInstance(s_context, s_instance);
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

static float At(mvfxAttribute attribute, int32_t i)
{
    return ((const float*)mvfxAttributeArray(s_live, 0, attribute))[i];
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
    // Room for crossings only on the surface an emitter meets: one a
    // particle of its capacity.
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
    s_live = mvfxContextInstance(s_context, s_instance);
    CHECK(s_live->waters[0].crossingCapacity == 0 && s_live->waters[1].crossingCapacity == 16,
          "room for crossings where they happen");
    Close();
}

static void TestFloat(void)
{
    // Under gravity and buoyancy 20, a particle let go at rest 0.1 below a
    // still surface settles at g / 20 below it (drag 2 damps it).
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 20.0f;
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
    float y = At(mvfx_attributePositionY, 0);
    CHECK(fabsf(y + 9.81f / 20.0f) <= 1e-3f && fabsf(At(mvfx_attributeVelocityY, 0)) <= 1e-3f,
          "a particle floats at g / buoyancy");
    Close();
}

static void TestMoving(void)
{
    // The float as TestFloat's, its instance moving 2 a step along X: the
    // particle reads the surface as it last stepped, where each step
    // starts, so it floats as still.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 20.0f;
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
    CHECK(fabsf(At(mvfx_attributePositionY, 0) + 9.81f / 20.0f) <= 1e-3f,
          "a moving instance's particle floats");
    Close();
}

static void TestScaled(void)
{
    // The float as TestFloat's of a world particle, its instance scaled 2:
    // g / buoyancy deep in the world, the surface's frame scaled away.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 20.0f;
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
    CHECK(fabsf(At(mvfx_attributePositionY, 0) + 9.81f / 20.0f) <= 1e-3f,
          "a scaled instance's particle floats as deep");
    Close();
}

static void TestUnderground(void)
{
    // Below shallow water's floor a particle is not in the water: at rest
    // there, no buoyancy lifts it.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 20.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    mvfxWaterDef shallow = Surface(mvfx_waterShallow);
    CHECK(Open(&emitter, 1, &shallow), "opened");
    CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "filled");
    Place(0.0, -0.3, 0.0, 0.0f, 0.0f, 0.0f);
    for (int32_t s = 0; s < 30; ++s)
    {
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    CHECK(At(mvfx_attributePositionY, 0) == -0.3f, "nothing lifts one below the floor");
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
    CHECK(fabsf(At(mvfx_attributeVelocityX, 0) - expf(-1.0f)) <= 1e-5f, "drag slows it");
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
    mvfxWaterState* state = &s_live->waters[0];
    for (int32_t s = 0; s < 60; ++s)
    {
        float* u = state->faces[0][state->faceCurrent];
        for (int32_t i = 0; i < 17 * 16; ++i)
        {
            u[i] = 0.5f;
        }
        CHECK(mvfxContext_Step(s_context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    CHECK(fabsf(At(mvfx_attributeVelocityX, 0) - 0.5f) <= 1e-3f, "drag carries it with the flow");
    Close();
}

// The sum of a waves surface's heights, and its lowest.
static double Heights(float* lowest)
{
    const mvfxWaterState* state = &s_live->waters[0];
    const float* u = state->heights[state->current];
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
    // A liquid's particle let go just above a still surface with a stiff
    // buoyancy bobs through it several times in a step of 32 substeps:
    // its crossing is met once that step.
    mvfxWaterCoupling coupling = Coupling();
    coupling.buoyancy = 4000.0f;
    mvfxEmitterDef emitter = Coupled(&coupling);
    emitter.motion.gravity[1] = -9.81f;
    emitter.liquid.enabled = true;
    emitter.liquid.substeps = 32;
    mvfxWaterDef waves = Surface(mvfx_waterWaves);
    CHECK(Open(&emitter, 1, &waves), "opened");
    Place(0.0, 0.02, 0.0, 0.0f, 0.0f, 0.0f);
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "born");
    CHECK(mvfxContext_Step(s_context, 0.5) == mvfx_success, "stepped");
    const mvfxEvent* events[16];
    CHECK(Events(mvfx_eventWater, events) == 1, "one crossing a step");
    Close();
}

// The crossing a world particle falling at 1 from 0.5 above a surface of
// a model at (0.3, 0.1) records as the step it crosses in ends (the
// instance's emitters stepped alone, the surface not yet), its instance
// scaled 2 and turned by rotation; the step's crossing event.
static mvfxWaterCrossing Recorded(mvfxWaterModel model, const float rotation[4], mvfxEvent* event)
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
    // Shallow water filled to 0.1 of the frame's lengths.
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "filled");
    for (int32_t i = 0; model == mvfx_waterShallow && i < 256; ++i)
    {
        s_live->waters[0].heights[s_live->waters[0].current][i] = 0.1f;
    }
    // Falling along the frame's -Y from 0.5 above its point (0.15, 0.05)
    // in the world's lengths.
    const float local[3] = {0.3f, 0.5f, 0.1f};
    const float down[3] = {0.0f, -1.0f, 0.0f};
    float place[3];
    float velocity[3];
    mvfxRotate(rotation, local, place);
    mvfxRotate(rotation, down, velocity);
    Place((double)place[0], (double)place[1], (double)place[2], velocity[0], velocity[1],
          velocity[2]);
    CHECK(mvfxContext_Step(s_context, 0.0) == mvfx_success, "born");
    mvfxStepInstance(s_live, MVFX_TICKS_PER_SECOND);
    const mvfxWaterState* state = &s_live->waters[0];
    mvfxWaterCrossing crossing =
        state->crossingCount == 1 ? state->crossings[0] : (mvfxWaterCrossing){0};
    const mvfxEvent* events[16];
    *event = Events(mvfx_eventWater, events) == 1 ? *events[0] : (mvfxEvent){0};
    Close();
    return crossing;
}

static void TestRecorded(void)
{
    // In the surface's frame, half the world's lengths: the point (0.15,
    // 0.05), the particle's radius 0.025, so the push's radius a cell (0.1)
    // and its depth 0.01 (0.025 / 0.1)^2; a pour's volume a sphere of
    // 0.025 (shallow water 0.1 deep there, its crossing 0.1 lower). Turned
    // a quarter about Z, the event's normal is the frame's Y in the world.
    static const float unturned[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    mvfxEvent event;
    mvfxWaterCrossing waves = Recorded(mvfx_waterWaves, unturned, &event);
    int32_t wrong = fabsf(waves.at[0] - 0.15f) > 1e-6f || fabsf(waves.at[1] - 0.05f) > 1e-6f;
    wrong += fabsf(waves.radius - 0.1f) > 1e-7f || fabsf(waves.depth - 0.000625f) > 1e-9f;
    wrong += waves.volume != 0.0f;
    wrong += llabs(event.tick - MVFX_TICKS_PER_SECOND / 2) > 1;
    mvfxWaterCrossing shallow = Recorded(mvfx_waterShallow, unturned, &event);
    float sphere = (float)(4.0 / 3.0 * 3.14159265358979323846 * 0.025 * 0.025 * 0.025);
    wrong += fabsf(shallow.volume / sphere - 1.0f) > 1e-6f;
    wrong += llabs(event.tick - 3 * MVFX_TICKS_PER_SECOND / 10) > 1;
    (void)Recorded(mvfx_waterWaves, quarter, &event);
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
    const mvfxWaterState* state = &s_live->waters[0];
    const float* d = state->heights[state->current];
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
    CHECK(s_live->emitters[0].count == 0 && deaths == 8 && kills == 8, "poured particles die");
    CHECK(fabs((Volume() - start) / poured - 1.0) <= 1e-4, "their volume joins the water");
    Close();
}

int main(void)
{
    TestRefusals();
    TestRoom();
    TestFloat();
    TestMoving();
    TestScaled();
    TestUnderground();
    TestDrag();
    TestFlow();
    TestCrossings();
    TestOnce();
    TestRecorded();
    TestOnSurface();
    TestThrough();
    TestPour();
    return s_failures;
}
