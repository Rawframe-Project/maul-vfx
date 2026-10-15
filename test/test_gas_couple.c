// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles and a gas: particles following a gas's velocity, pulled
// toward it as a velocity field or sped by it as an acceleration, along
// the world's turn of a turned local gas, and not outside its box;
// particles pouring density, temperature and fuel into a gas, all of it
// kept, and pulling its faces toward their velocity, nothing from outside
// its box; the refusals of forces and splats naming no gas.

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstance* s_live;

// Opens an instance, turned by rotation, of an effect of one emitter and
// one 2D gas of 32 by 32 cells with no pressure.
static void Open(const mvfxEmitterDef* emitter, const mvfxGasDef* gas, const float rotation[4])
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    def.gases = gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.rotation, rotation, sizeof instanceDef.rotation);
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &instance) == mvfx_success, "instance");
    s_live = mvfxContextInstance(s_context, instance);
}

static const float s_unturned[4] = {0, 0, 0, 1};

// A 2D gas of 32 by 32 cells of 0.1, no pressure.
static mvfxGasDef Still(void)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 32;
    gas.size[1] = 32;
    gas.pressureCycles = 0;
    return gas;
}

// Sets every face of the gas to a velocity in its box's frame.
static void Blow(float u, float v)
{
    mvfxGasState* state = &s_live->gases[0];
    for (int32_t f = 0; f < 33 * 32; ++f)
    {
        state->velocity[0][state->current][f] = u;
        state->velocity[1][state->current][f] = v;
    }
}

// Steps the context by n steps of 1/64 s.
static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    }
}

// A particle's velocity in the emitter's arrays.
static void VelocityOf(int32_t i, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* v =
            mvfxAttributeArray(s_live, 0, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        out[k] = v[i];
    }
}

static void TestFollow(void)
{
    // Particles born at rest in the middle of a gas blowing (1, 0.5): as
    // a velocity field of tightness 8, each moves along the gas (the one
    // born at the step's end not yet), never faster, the oldest after 10
    // steps at least 0.6 of its speed; as an
    // acceleration of strength 2, each at 2 (1, 0.5) times about its age,
    // the oldest's speed past that of the field's.
    bool right = true;
    float oldest[2] = {0.0f, 0.0f};
    for (int32_t mode = 0; mode < 2; ++mode)
    {
        mvfxForce follow = {.kind = mvfx_forceGas,
                            .strength = mode == 0 ? 1.0f : 2.0f,
                            .fieldMode = mode == 0 ? mvfx_fieldVelocity : mvfx_fieldAcceleration,
                            .tightness = 8.0f};
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.forces = &follow;
        emitter.forceCount = 1;
        mvfxGasDef gas = Still();
        Open(&emitter, &gas, s_unturned);
        Blow(1.0f, 0.5f);
        Steps(10);
        int32_t count = s_live->emitters[0].count;
        right = right && count >= 9;
        for (int32_t i = 0; i < count; ++i)
        {
            float v[3];
            VelocityOf(i, v);
            float speed = sqrtf(v[0] * v[0] + v[1] * v[1]);
            right = right && fabsf(v[0] * 0.5f - v[1]) <= 1e-5f && v[0] >= 0.0f && v[2] == 0.0f;
            right = right && (mode == 1 || speed <= sqrtf(1.25f));
            oldest[mode] = fmaxf(oldest[mode], speed);
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right && oldest[0] >= 0.6f * sqrtf(1.25f) && oldest[1] > sqrtf(1.25f) * 0.25f,
          "particles follow a gas");
}

static void TestTurned(void)
{
    // A local gas, its instance turned a quarter about Z, blowing (1, 0)
    // along its own X, which is the world's +Y: world-space particles
    // following it move up the world's Y.
    const float quarter[4] = {0, 0, 0.70710678f, 0.70710678f};
    const mvfxForce follow = {.kind = mvfx_forceGas,
                              .strength = 1.0f,
                              .fieldMode = mvfx_fieldVelocity,
                              .tightness = 8.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.forces = &follow;
    emitter.forceCount = 1;
    mvfxGasDef gas = Still();
    Open(&emitter, &gas, quarter);
    Blow(1.0f, 0.0f);
    Steps(6);
    bool right = s_live->emitters[0].count > 0;
    bool moved = false;
    for (int32_t i = 0; i < s_live->emitters[0].count; ++i)
    {
        float v[3];
        VelocityOf(i, v);
        right = right && v[1] >= 0.0f && fabsf(v[0]) <= 1e-5f * v[1];
        moved = moved || v[1] > 0.0f;
    }
    CHECK(right && moved, "particles follow a turned gas");
    mvfxDestroyContext(s_context);
}

static void TestOutside(void)
{
    // The gas's box moved 5 away along X: particles born at the origin,
    // outside it, are not moved.
    const mvfxForce follow = {.kind = mvfx_forceGas,
                              .strength = 1.0f,
                              .fieldMode = mvfx_fieldVelocity,
                              .tightness = 8.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.forces = &follow;
    emitter.forceCount = 1;
    mvfxGasDef gas = Still();
    gas.center[0] = 5.0f;
    Open(&emitter, &gas, s_unturned);
    Blow(1.0f, 0.5f);
    Steps(4);
    bool right = s_live->emitters[0].count > 0;
    for (int32_t i = 0; i < s_live->emitters[0].count; ++i)
    {
        float v[3];
        VelocityOf(i, v);
        right = right && v[0] == 0.0f && v[1] == 0.0f;
    }
    CHECK(right, "no gas outside its box");
    mvfxDestroyContext(s_context);
}

// The sum of a gas's scalar f.
static double Sum(int32_t f)
{
    const mvfxGasState* state = &s_live->gases[0];
    double sum = 0.0;
    for (int64_t i = 0; i < s_live->effect->gases[0].cells; ++i)
    {
        sum += (double)state->scalars[f][state->current][i];
    }
    return sum;
}

static void TestSplat(void)
{
    // Particles born at rest a little off the gas's middle, each pouring 2
    // of density, 3 of temperature and 0.5 of fuel a second: with no
    // velocity to carry it, after each step the gas holds each live
    // particle's share of every step it was alive, all of it, to float's
    // rounding.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.shape.offset[0] = 0.13f;
    emitter.shape.offset[1] = -0.07f;
    emitter.gasSplat =
        (mvfxGasSplat){.enabled = true, .density = 2.0f, .temperature = 3.0f, .fuel = 0.5f};
    mvfxGasDef gas = Still();
    Open(&emitter, &gas, s_unturned);
    double poured = 0.0;
    bool right = true;
    for (int32_t i = 0; i < 6; ++i)
    {
        Steps(1);
        poured += (double)s_live->emitters[0].count / 64.0;
        right = right && fabs(Sum(0) - 2.0 * poured) <= 1e-5 &&
                fabs(Sum(1) - 3.0 * poured) <= 1e-5 && fabs(Sum(2) - 0.5 * poured) <= 1e-5;
    }
    CHECK(right && poured > 0.0, "particles pour into a gas");
    mvfxDestroyContext(s_context);
}

static void TestSplatDrag(void)
{
    // Particles born moving at (2, 0) pull the gas's faces near them
    // toward that by a drag of 20 a second: some X face moves, none past
    // 2; the Y faces stay still.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.velocity.mode = mvfx_directionFixed;
    emitter.velocity.direction[0] = 1.0f;
    emitter.velocity.direction[2] = 0.0f;
    emitter.velocity.speed = (mvfxRange){2.0f, 2.0f};
    emitter.gasSplat = (mvfxGasSplat){.enabled = true, .drag = 20.0f};
    mvfxGasDef gas = Still();
    Open(&emitter, &gas, s_unturned);
    Steps(3);
    const mvfxGasState* state = &s_live->gases[0];
    float most = 0.0f;
    bool right = true;
    for (int32_t f = 0; f < 33 * 32; ++f)
    {
        most = fmaxf(most, state->velocity[0][state->current][f]);
        right = right && state->velocity[1][state->current][f] == 0.0f;
    }
    CHECK(right && most > 0.1f && most <= 2.0f, "particles pull a gas's faces");
    mvfxDestroyContext(s_context);
}

static void TestSplatOutside(void)
{
    // Particles outside the gas's box pour nothing.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.gasSplat = (mvfxGasSplat){.enabled = true, .density = 2.0f, .drag = 5.0f};
    mvfxGasDef gas = Still();
    gas.center[1] = -4.0f;
    Open(&emitter, &gas, s_unturned);
    Steps(4);
    CHECK(s_live->emitters[0].count > 0 && Sum(0) == 0.0, "nothing poured from outside");
    mvfxDestroyContext(s_context);
}

static void TestRefusals(void)
{
    // A force following gas 1 of an effect of one gas, a gas's force
    // following a gas, a splat into gas 1, a splat of negative drag and one
    // of a rate not finite are refused; a splat into gas 0, taken.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    bool right = true;
    for (int32_t c = 0; c < 6; ++c)
    {
        mvfxForce follow = {.kind = mvfx_forceGas, .gas = c == 0 ? 1 : 0};
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.forces = &follow;
        emitter.forceCount = c == 0 ? 1 : 0;
        emitter.gasSplat = (mvfxGasSplat){.enabled = c >= 2,
                                          .gas = c == 2 ? 1 : 0,
                                          .drag = c == 3 ? -1.0f : 0.0f,
                                          .fuel = c == 4 ? NAN : 0.0f};
        mvfxGasDef gas = Still();
        gas.forces = &follow;
        gas.forceCount = c == 1 ? 1 : 0;
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.emitters = &emitter;
        def.emitterCount = 1;
        def.gases = &gas;
        def.gasCount = 1;
        mvfxEffectId effect = {0};
        mvfxResult made = mvfxCreateEffect(s_context, &def, &effect);
        right = right && made == (c == 5 ? mvfx_success : mvfx_errorInvalid);
    }
    CHECK(right, "the couplings' refusals");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestFollow();
    TestTurned();
    TestOutside();
    TestSplat();
    TestSplatDrag();
    TestSplatOutside();
    TestRefusals();
    return s_failures;
}
