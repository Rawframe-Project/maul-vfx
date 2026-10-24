// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The water coupling's own room: crossings are kept only for the surfaces
// an emitter meets, none on the rest. The room for every particle on the
// surface met is in test_water_couple.c.

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

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

static void TestRoom(void)
{
    // Room for crossings only on the surface an emitter meets: none on
    // the other (one a particle of its capacity on the one it meets).
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
    const mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    CHECK(live->waters[0].crossingCapacity == 0 && live->waters[1].crossingCapacity == 16,
          "room for crossings where they happen");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestRoom();
    return s_failures;
}
