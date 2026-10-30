// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles and a gas: particles following a gas's velocity, pulled
// toward it as a velocity field or sped by it as an acceleration, along
// the world's turn of a turned local gas, and not outside its box;
// particles pouring density, temperature and fuel into a gas, all of it
// kept, and pulling its faces toward their velocity, nothing from outside
// its box; a splat's exact trilinear weights and pull; a splat landing
// where its particle is, an anchor moved; a particle reading the gas where
// it is through a moved, scaled, offset box; the refusals of forces and
// splats naming no gas.

#include "peek.h"
#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// Opens an instance, turned by rotation, of an effect of one emitter and
// one 2D gas of 32 by 32 cells with no pressure.
static void Open(const mvfxEmitterDef* emitter, const mvfxGasDef* gas, const float rotation[4])
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEmitterDef peeking = Peeking(emitter);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &peeking;
    def.emitterCount = 1;
    def.gases = gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.rotation, rotation, sizeof instanceDef.rotation);
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
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
    static float us[33 * 32];
    static float vs[32 * 33];
    for (int32_t f = 0; f < 33 * 32; ++f)
    {
        us[f] = u;
        vs[f] = v;
    }
    CHECK(mvfxTestWriteGasFaces(s_context, s_instance, 0, 0, us, 33 * 32) &&
              mvfxTestWriteGasFaces(s_context, s_instance, 0, 1, vs, 32 * 33),
          "blown");
}

// Sets the gas's X faces to their index along X, in its box's lengths a
// second.
static void Ramp(void)
{
    static float us[33 * 32];
    for (int32_t f = 0; f < 33 * 32; ++f)
    {
        us[f] = (float)(f % 33);
    }
    CHECK(mvfxTestWriteGasFaces(s_context, s_instance, 0, 0, us, 33 * 32), "ramped");
}

// Steps the context by n steps of 1/64 s.
static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    }
}

// A particle's velocity as the last step left it.
static void VelocityOf(int32_t i, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = s_peeked.velocity[k][i];
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
        int32_t count = s_peeked.count;
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
    bool right = s_peeked.count > 0;
    bool moved = false;
    for (int32_t i = 0; i < s_peeked.count; ++i)
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
    bool right = s_peeked.count > 0;
    for (int32_t i = 0; i < s_peeked.count; ++i)
    {
        float v[3];
        VelocityOf(i, v);
        right = right && v[0] == 0.0f && v[1] == 0.0f;
    }
    CHECK(right, "no gas outside its box");
    mvfxDestroyContext(s_context);
}

static float s_cells[32 * 32];

// The sum of a gas's scalar field f (density, temperature or fuel).
static double Sum(int32_t f)
{
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, (mvfxGasField)f, s_cells, 32 * 32) ==
              mvfx_success,
          "read");
    double sum = 0.0;
    for (int64_t i = 0; i < 32 * 32; ++i)
    {
        sum += (double)s_cells[i];
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
        (mvfxGasSplat){.enable = true, .density = 2.0f, .temperature = 3.0f, .fuel = 0.5f};
    mvfxGasDef gas = Still();
    Open(&emitter, &gas, s_unturned);
    double poured = 0.0;
    bool right = true;
    for (int32_t i = 0; i < 6; ++i)
    {
        Steps(1);
        poured += (double)s_peeked.count / 64.0;
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
    emitter.gasSplat = (mvfxGasSplat){.enable = true, .drag = 20.0f};
    mvfxGasDef gas = Still();
    Open(&emitter, &gas, s_unturned);
    Steps(3);
    static float us[33 * 32];
    static float vs[32 * 33];
    CHECK(mvfxTestReadGasFaces(s_context, s_instance, 0, 0, us, 33 * 32) == 33 * 32 &&
              mvfxTestReadGasFaces(s_context, s_instance, 0, 1, vs, 32 * 33) == 32 * 33,
          "faces read");
    float most = 0.0f;
    bool right = true;
    for (int32_t f = 0; f < 33 * 32; ++f)
    {
        most = fmaxf(most, us[f]);
        right = right && vs[f] == 0.0f;
    }
    CHECK(right && most > 0.1f && most <= 2.0f, "particles pull a gas's faces");
    mvfxDestroyContext(s_context);
}

static void TestSplatOutside(void)
{
    // Particles outside the gas's box pour nothing.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.gasSplat = (mvfxGasSplat){.enable = true, .density = 2.0f, .drag = 5.0f};
    mvfxGasDef gas = Still();
    gas.center[1] = -4.0f;
    Open(&emitter, &gas, s_unturned);
    Steps(4);
    CHECK(s_peeked.count > 0 && Sum(0) == 0.0, "nothing poured from outside");
    mvfxDestroyContext(s_context);
}

static void TestSplatExact(void)
{
    // Particles the host puts in a still 2D gas of 32 by 32 cells of 0.1,
    // over a step of 1/64 s, each splatting where the step leaves it, at
    // (0.13, -0.07) from the box's middle, (17.3, 15.3) in cells. Pouring
    // 2 of density a second at rest, with a second half a cell outside the
    // box pouring nothing: the 4 cells about the first take 2/64 times
    // their weights (0.04, 0.16, 0.16, 0.64), all that is poured. Moving
    // at v along X through faces blowing at -v, pulling by a drag of 10:
    // the 4 X faces about it go toward v by (v - -v) (1 - e^(-10/64))
    // times theirs (0.14, 0.06, 0.56, 0.24), all the pull, as the pull is
    // relative to the face's own velocity; the Y faces stay still.
    bool right = true;
    for (int32_t pass = 0; pass < 2; ++pass)
    {
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.rate = 0.0f;
        emitter.hostSpawnCapacity = 2;
        emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
        emitter.gasSplat = pass == 0 ? (mvfxGasSplat){.enable = true, .density = 2.0f}
                                     : (mvfxGasSplat){.enable = true, .drag = 10.0f};
        mvfxGasDef gas = Still();
        Open(&emitter, &gas, s_unturned);
        // Slow, so carrying the faces by what the splat gives them, later
        // in the step, changes them by about 2e-5 of their values.
        float speed = pass == 0 ? 0.0f : 1e-3f;
        Blow(-speed, 0.0f);
        const double places[6] = {0.13 - (double)speed / 64.0, -0.07, 0, -1.65, 0, 0};
        const float velocities[6] = {speed, 0, 0, 0, 0, 0};
        mvfxSpawnRequest request = {
            .count = pass == 0 ? 2 : 1, .positions = places, .velocities = velocities};
        CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "placed");
        Steps(1);
        if (pass == 0)
        {
            double total = Sum(0);
            const int32_t at[4] = {14 * 32 + 16, 14 * 32 + 17, 15 * 32 + 16, 15 * 32 + 17};
            const float weight[4] = {0.04f, 0.16f, 0.16f, 0.64f};
            right = right && s_peeked.count == 2 && fabs(total - 2.0 / 64.0) <= 1e-7;
            for (int32_t c = 0; c < 4; ++c)
            {
                right = right && fabsf(s_cells[at[c]] - weight[c] * 2.0f / 64.0f) <= 1e-7f;
            }
        }
        else
        {
            static float us[33 * 32];
            static float vs[32 * 33];
            CHECK(mvfxTestReadGasFaces(s_context, s_instance, 0, 0, us, 33 * 32) == 33 * 32 &&
                      mvfxTestReadGasFaces(s_context, s_instance, 0, 1, vs, 32 * 33) == 32 * 33,
                  "faces read");
            const int32_t faceAt[4] = {14 * 33 + 17, 14 * 33 + 18, 15 * 33 + 17, 15 * 33 + 18};
            const float faceWeight[4] = {0.14f, 0.06f, 0.56f, 0.24f};
            float pull = 2.0f * speed * (1.0f - expf(-10.0f / 64.0f));
            float faceTotal = 0.0f;
            for (int32_t f = 0; f < 33 * 32; ++f)
            {
                faceTotal += us[f] + speed;
            }
            for (int32_t c = 0; c < 4; ++c)
            {
                right =
                    right && fabsf(us[faceAt[c]] + speed - faceWeight[c] * pull) <= 1e-4f * pull;
            }
            right = right && fabsf(faceTotal - pull) <= 1e-4f * pull;
            for (int32_t f = 0; f < 32 * 33; ++f)
            {
                right = right && vs[f] == 0.0f;
            }
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "a splat's weights exact");
}

// One particle born at the effect's start, at rest at offset (x, y) from
// the emitter, in the world.
static mvfxEmitterDef One(float x, float y)
{
    static const mvfxBurst once = {
        .time = 0.0f, .minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.bursts = &once;
    emitter.burstCount = 1;
    emitter.shape.offset[0] = x;
    emitter.shape.offset[1] = y;
    return emitter;
}

// Opens as Open, the instance at origin and of scale.
static void OpenAt(const mvfxEmitterDef* emitter, const mvfxGasDef* gas, const double origin[3],
                   float scale)
{
    Open(emitter, gas, s_unturned);
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, s_unturned, scale, true) ==
              mvfx_success,
          "placed");
}

static void TestSplatPlace(void)
{
    // One particle poured from a world-space emitter of an instance moved
    // to (2000, 0, 0), past the distance that moves the emitter's anchor
    // (its places offsets from it), into its local gas: at (0.13, -0.07) from the box's
    // middle, cells (16.8, 14.8) less a half, its density after a step is
    // 2/64 times the weights 0.04, 0.16, 0.16, 0.64 about cell (16, 14).
    mvfxEmitterDef emitter = One(0.13f, -0.07f);
    emitter.gasSplat = (mvfxGasSplat){.enable = true, .density = 2.0f};
    mvfxGasDef gas = Still();
    const double origin[3] = {2000, 0, 0};
    OpenAt(&emitter, &gas, origin, 1.0f);
    Steps(1);
    (void)Sum(0);
    const float* density = s_cells;
    const int32_t at[4] = {14 * 32 + 16, 14 * 32 + 17, 15 * 32 + 16, 15 * 32 + 17};
    const float weight[4] = {0.04f, 0.16f, 0.16f, 0.64f};
    bool right = s_peeked.count == 1 && s_peeked.anchor[0] != 0.0;
    for (int32_t c = 0; c < 4; ++c)
    {
        right = right && fabsf(density[at[c]] - weight[c] * 2.0f / 64.0f) <= 1e-6f;
    }
    CHECK(right, "a splat lands where the particle is");
    mvfxDestroyContext(s_context);
}

static void TestFollowPlace(void)
{
    // A local gas centered at (0.5, 0, 0) in an instance at (1, 0, 0) of
    // scale 2, its X faces' velocity their index along X (in its box's
    // lengths a second): a world particle born 0.8 along X from the
    // emitter in the instance's frame, at (2.6, 0, 0), 0.3 from
    // the box's middle in its lengths, at 19 cells, reads 19 there, 38 in
    // the world. As an acceleration of strength 1 it is moving at 38/64
    // after a step; as a velocity field of tightness 8, at 38 (1 -
    // e^(-8/64)).
    bool right = true;
    for (int32_t mode = 0; mode < 2; ++mode)
    {
        const mvfxForce follow = {.kind = mvfx_forceGas,
                                  .strength = 1.0f,
                                  .fieldMode =
                                      mode == 0 ? mvfx_fieldAcceleration : mvfx_fieldVelocity,
                                  .tightness = 8.0f};
        mvfxEmitterDef emitter = One(0.8f, 0.0f);
        emitter.forces = &follow;
        emitter.forceCount = 1;
        mvfxGasDef gas = Still();
        gas.center[0] = 0.5f;
        const double origin[3] = {1, 0, 0};
        OpenAt(&emitter, &gas, origin, 2.0f);
        Ramp();
        Steps(1);
        float v[3];
        VelocityOf(0, v);
        float expected = mode == 0 ? 38.0f / 64.0f : 38.0f * (1.0f - expf(-8.0f / 64.0f));
        right = right && s_peeked.count == 1 && fabsf(v[0] - expected) <= 1e-3f && v[1] == 0.0f;
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "a particle follows the gas where it is");
}

static void TestFollowMoving(void)
{
    // A local particle 0.8 along X in a local gas whose X faces' velocity
    // is their index, its instance moving from the origin to (1, 0, 0) over
    // the step: it reads the gas as it last stepped, where the step starts,
    // face 24 under it (not face 14, where the box will be), so as an
    // acceleration of strength 1 it moves at 24/64 after the step.
    const mvfxForce follow = {
        .kind = mvfx_forceGas, .strength = 1.0f, .fieldMode = mvfx_fieldAcceleration};
    mvfxEmitterDef emitter = One(0.8f, 0.0f);
    emitter.space = mvfx_spaceLocal;
    emitter.forces = &follow;
    emitter.forceCount = 1;
    mvfxGasDef gas = Still();
    Open(&emitter, &gas, s_unturned);
    Steps(1);
    Ramp();
    const double moved[3] = {1, 0, 0};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, moved, s_unturned, 1.0f, false) ==
              mvfx_success,
          "moved");
    Steps(1);
    float v[3];
    VelocityOf(0, v);
    CHECK(fabsf(v[0] - 24.0f / 64.0f) <= 1e-4f, "a moving instance's gas read where it stepped");
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
        emitter.gasSplat = (mvfxGasSplat){.enable = c >= 2,
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
        mvfxResult made = mvfxCreateEffect(s_context, &def, &effect, NULL);
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
    TestSplatExact();
    TestSplatPlace();
    TestFollowPlace();
    TestFollowMoving();
    TestRefusals();
    return s_failures;
}
