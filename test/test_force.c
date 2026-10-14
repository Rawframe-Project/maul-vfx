// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Forces: each kind's acceleration where the host places a particle at
// rest, wind exact as drag is, the falloffs' weights, forces given in the
// emitter's space or the world's under a turned, scaled instance, a
// strength from a parameter and its hold, the order of several forces,
// and the refusals.

#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxInstance* live;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter, int32_t parameterCount)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    static const float parameters[4] = {1, 1, 1, 1};
    effectDef.parameters = parameters;
    effectDef.parameterCount = parameterCount;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

static const float* Array(const Scene* scene, mvfxAttribute attribute)
{
    return mvfxAttributeArray(scene->live, 0, attribute);
}

// An emitter that spawns only what the host gives, living long.
static mvfxEmitterDef Held(const mvfxForce* forces, int32_t count)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.forces = forces;
    emitter.forceCount = count;
    return emitter;
}

// The acceleration on a particle the host places at rest at place: its
// velocity after a step of 1/1024 s, times 1,024.
static void AccelerationIn(Scene* scene, const double place[3], float out[3])
{
    static const float rest[3] = {0.0f, 0.0f, 0.0f};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = rest};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
    CHECK(mvfxContext_StepTicks(scene->context, MVFX_TICKS_PER_SECOND / 1024) == mvfx_success,
          "step");
    int32_t last = scene->live->emitters[0].count - 1;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = 1024.0f * Array(scene, (mvfxAttribute)(mvfx_attributeVelocityX + k))[last];
    }
}

static void AccelerationAt(const mvfxEmitterDef* emitter, double x, double y, double z,
                           float out[3])
{
    Scene scene = NewScene(emitter, 0);
    const double place[3] = {x, y, z};
    AccelerationIn(&scene, place, out);
    mvfxDestroyContext(scene.context);
}

static bool Is(const float a[3], float x, float y, float z)
{
    float tolerance = 1e-4f * (1.0f + fabsf(x) + fabsf(y) + fabsf(z));
    return fabsf(a[0] - x) <= tolerance && fabsf(a[1] - y) <= tolerance &&
           fabsf(a[2] - z) <= tolerance;
}

static void TestKinds(void)
{
    float a[3];
    mvfxForce force = {.kind = mvfx_forceConstant, .vector = {1, -2, 3}, .strength = 2.0f};
    mvfxEmitterDef emitter = Held(&force, 1);
    AccelerationAt(&emitter, 5, 6, 7, a);
    CHECK(Is(a, 2, -4, 6), "a constant: vector times strength");

    // Attractor at (0, 0, 0), strength 8, softening 2: 8 toward it at the
    // softening radius, 2 at twice it, 4 at half it, none at the point.
    force = (mvfxForce){.kind = mvfx_forceAttractor, .strength = 8.0f, .softening = 2.0f};
    emitter = Held(&force, 1);
    AccelerationAt(&emitter, 2, 0, 0, a);
    CHECK(Is(a, -8, 0, 0), "at the softening radius");
    AccelerationAt(&emitter, 0, 4, 0, a);
    CHECK(Is(a, 0, -2, 0), "beyond it, by the square");
    AccelerationAt(&emitter, 0, 0, -1, a);
    CHECK(Is(a, 0, 0, 4), "within it, linearly");
    AccelerationAt(&emitter, 0, 0, 0, a);
    CHECK(Is(a, 0, 0, 0), "none at the point");
    force.strength = -8.0f;
    force.point[0] = 1.0f;
    emitter = Held(&force, 1);
    AccelerationAt(&emitter, 3, 0, 0, a);
    CHECK(Is(a, 8, 0, 0), "a negative strength repels");

    // Vortex about Z through (1, 1, 0), strength 3, pull 2: at (2, 1, 5),
    // 3 along +Y and 2 toward the axis; none on the axis.
    force = (mvfxForce){.kind = mvfx_forceVortex,
                        .vector = {0, 0, 4},
                        .point = {1, 1, 0},
                        .strength = 3.0f,
                        .pull = 2.0f};
    emitter = Held(&force, 1);
    AccelerationAt(&emitter, 2, 1, 5, a);
    CHECK(Is(a, -2, 3, 0), "about the axis and toward it");
    AccelerationAt(&emitter, 1, 1, -3, a);
    CHECK(Is(a, 0, 0, 0), "none on the axis");
}

static void TestWindExact(void)
{
    // Wind (5, 0, 0) taking 2 of the difference a second, with drag 0.5:
    // v(t) = 5 c / k + (v0 - 5 c / k) e^(-k t) along X with k = 2.5, from
    // v0 = -3, the same over one step or many.
    mvfxForce force = {.kind = mvfx_forceWind, .vector = {5, 0, 0}, .strength = 2.0f};
    mvfxEmitterDef emitter = Held(&force, 1);
    emitter.motion.drag = 0.5f;
    for (int32_t steps = 1; steps <= 16; steps *= 16)
    {
        Scene scene = NewScene(&emitter, 0);
        static const double place[3] = {0, 0, 0};
        static const float thrown[3] = {-3, 0, 0};
        mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = thrown};
        CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success,
              "placed");
        for (int32_t i = 0; i < steps; ++i)
        {
            CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / steps) ==
                      mvfx_success,
                  "step");
        }
        double k = 2.5;
        double terminal = 10.0 / k;
        double e = exp(-k);
        double v = terminal + (-3.0 - terminal) * e;
        double x = terminal + (-3.0 - terminal) * (1.0 - e) / k;
        float vx = Array(&scene, mvfx_attributeVelocityX)[0];
        float px = Array(&scene, mvfx_attributePositionX)[0];
        CHECK(fabs((double)vx - v) <= 2e-5 && fabs((double)px - x) <= 2e-5,
              "toward the wind's velocity, exactly");
        mvfxDestroyContext(scene.context);
    }
}

static void TestFalloffs(void)
{
    float a[3];
    // A constant (0, 1, 0) in a sphere of radius 4 about (1, 0, 0),
    // exponent 2: (1 - 2 / 4)^2 at distance 2, none at 4 or beyond.
    mvfxForce force = {
        .kind = mvfx_forceConstant,
        .vector = {0, 1, 0},
        .strength = 1.0f,
        .falloff = {
            .kind = mvfx_falloffSphere, .center = {1, 0, 0}, .radius = 4.0f, .exponent = 2.0f}};
    mvfxEmitterDef emitter = Held(&force, 1);
    AccelerationAt(&emitter, 1, 2, 0, a);
    CHECK(Is(a, 0, 0.25f, 0), "a sphere's weight");
    AccelerationAt(&emitter, 5, 0, 0, a);
    CHECK(Is(a, 0, 0, 0), "nothing at its edge");
    force.falloff.exponent = 0.0f;
    emitter = Held(&force, 1);
    AccelerationAt(&emitter, 1, 3.9, 0, a);
    CHECK(Is(a, 0, 1, 0), "a hard edge");

    // A tube of radius 2 along X through the origin, exponent 1: by the
    // distance from the line, whatever the place along it.
    force.falloff = (mvfxFalloff){
        .kind = mvfx_falloffTube, .axis = {3, 0, 0}, .radius = 2.0f, .exponent = 1.0f};
    emitter = Held(&force, 1);
    AccelerationAt(&emitter, 50, 0, 1.5, a);
    CHECK(Is(a, 0, 0.25f, 0), "a tube's weight");

    // A cone from the origin along +Z, radius 10, half angle 0.6,
    // exponent 1: on the axis at 5, half; at the half angle's cosine
    // share, a further half; outside the angle or behind, none; at its
    // apex, all.
    force.falloff = (mvfxFalloff){.kind = mvfx_falloffCone,
                                  .axis = {0, 0, 1},
                                  .radius = 10.0f,
                                  .angle = 0.6f,
                                  .exponent = 1.0f};
    emitter = Held(&force, 1);
    AccelerationAt(&emitter, 0, 0, 5, a);
    CHECK(Is(a, 0, 0.5f, 0), "on a cone's axis");
    double c = cos(0.6);
    double half = acos(0.5 + 0.5 * c);
    AccelerationAt(&emitter, 5 * sin(half), 0, 5 * cos(half), a);
    CHECK(Is(a, 0, 0.25f, 0), "half way out in angle");
    AccelerationAt(&emitter, 5 * sin(0.7), 0, 5 * cos(0.7), a);
    CHECK(Is(a, 0, 0, 0), "outside its angle");
    AccelerationAt(&emitter, 0, 0, -1, a);
    CHECK(Is(a, 0, 0, 0), "behind it");
    AccelerationAt(&emitter, 0, 0, 0, a);
    CHECK(Is(a, 0, 1, 0), "all at its apex");
}

static void TestSpaces(void)
{
    // The instance at (100, 0, 0), a quarter turn about Z, scale 2.
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    float a[3];
    // An attractor at (1, 0, 0) in the emitter's space, strength 3,
    // softening 1, on a world emitter: at (100, 2, 0) in the world, scaled
    // to strength 6 at softening 2, a particle at its softening radius.
    mvfxForce force = {.kind = mvfx_forceAttractor,
                       .space = mvfx_spaceLocal,
                       .point = {1, 0, 0},
                       .strength = 3.0f,
                       .softening = 1.0f};
    mvfxEmitterDef emitter = Held(&force, 1);
    Scene scene = NewScene(&emitter, 0);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    static const double near[3] = {100.0, 4.0, 0.0};
    AccelerationIn(&scene, near, a);
    CHECK(Is(a, 0, -6, 0), "the emitter's space turned and scaled into the world");
    mvfxDestroyContext(scene.context);

    // A world wind (0, 6, 0) and a world constant (4, 0, 0) on a local
    // emitter: turned back and unscaled, the constant (0, -2, 0) and the
    // wind toward (3, 0, 0).
    mvfxForce forces[2] = {
        {.kind = mvfx_forceConstant, .vector = {4, 0, 0}, .strength = 1.0f},
        {.kind = mvfx_forceWind, .vector = {0, 6, 0}, .strength = 1.0f},
    };
    emitter = Held(forces, 2);
    emitter.space = mvfx_spaceLocal;
    scene = NewScene(&emitter, 0);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    static const double home[3] = {0.0, 0.0, 0.0};
    AccelerationIn(&scene, home, a);
    // The wind's drag of 1 carries every acceleration over the short step
    // by (1 - e^-h) / h, near 1.
    float carry = (float)((1.0 - exp(-1.0 / 1024.0)) * 1024.0);
    CHECK(Is(a, 3.0f * carry, -2.0f * carry, 0), "the world's turned back and unscaled");
    mvfxDestroyContext(scene.context);

    // A world attractor and falloff at (100, 3, 0) on a local emitter:
    // at the instance's (1.5, 0, 0).
    force = (mvfxForce){
        .kind = mvfx_forceAttractor,
        .point = {100, 3, 0},
        .strength = 2.0f,
        .softening = 1.0f,
        .falloff = {
            .kind = mvfx_falloffSphere, .center = {100, 3, 0}, .radius = 100.0f, .exponent = 0.0f}};
    emitter = Held(&force, 1);
    emitter.space = mvfx_spaceLocal;
    scene = NewScene(&emitter, 0);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    static const double beside[3] = {1.5, 0.5, 0.0};
    AccelerationIn(&scene, beside, a);
    // Softening 0.5 in the instance's space, strength 1, at distance 0.5.
    CHECK(Is(a, 0, -1, 0), "a world point in the instance's space");
    mvfxDestroyContext(scene.context);
}

static void TestParameters(void)
{
    // A constant's strength from parameter 0, then held; a wind's to 0 and
    // the most drag.
    mvfxForce forces[2] = {
        {.kind = mvfx_forceConstant,
         .vector = {1, 0, 0},
         .strength = 2.0f,
         .strengthFromParameter = true,
         .parameter = 0},
        {.kind = mvfx_forceWind,
         .vector = {0, 1, 0},
         .strength = 1.0f,
         .strengthFromParameter = true,
         .parameter = 0},
    };
    mvfxEmitterDef emitter = Held(forces, 2);
    Scene scene = NewScene(&emitter, 1);
    static const double home[3] = {0, 0, 0};
    float a[3];
    const float three[4] = {3, 0, 0, 0};
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, three) == mvfx_success,
          "set");
    AccelerationIn(&scene, home, a);
    // The wind's drag k carries every acceleration by (1 - e^-(k h)) / (k h).
    float carry = (float)((1.0 - exp(-3.0 / 1024.0)) * 1024.0 / 3.0);
    CHECK(Is(a, 6.0f * carry, 3.0f * carry, 0), "strengths times the parameter");
    const float huge[4] = {1e30f, 0, 0, 0};
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, huge) == mvfx_success, "set");
    AccelerationIn(&scene, home, a);
    carry = (float)((1.0 - exp(-(double)MVFX_MAX_DRAG / 1024.0)) * 1024.0 / (double)MVFX_MAX_DRAG);
    CHECK(Is(a, MVFX_MAX_EXTENT * carry, MVFX_MAX_DRAG * carry, 0), "held to the most");
    const float negative[4] = {-5, 0, 0, 0};
    CHECK(mvfxInstance_SetParameter(scene.context, scene.instance, 0, negative) == mvfx_success,
          "set");
    AccelerationIn(&scene, home, a);
    CHECK(Is(a, -10, 0, 0), "a wind holds to 0");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 16; ++field)
    {
        mvfxForce force = {.kind = mvfx_forceAttractor, .softening = 1.0f};
        mvfxEmitterDef bad = Held(&force, 1);
        switch (field)
        {
        case 0:
            force.kind = 4;
            break;
        case 1:
            force.space = 2;
            break;
        case 2:
            force.vector[1] = (float)NAN;
            break;
        case 3:
            force.point[2] = 2.0f * MVFX_MAX_EXTENT;
            break;
        case 4:
            force.strength = -2.0f * MVFX_MAX_EXTENT;
            break;
        case 5:
            force.softening = 0.0f;
            break;
        case 6:
            force = (mvfxForce){.kind = mvfx_forceVortex};
            break;
        case 7:
            force = (mvfxForce){.kind = mvfx_forceVortex, .vector = {0, 0, 1}, .pull = (float)NAN};
            break;
        case 8:
            force = (mvfxForce){.kind = mvfx_forceWind, .strength = -1.0f};
            break;
        case 9:
            force.strengthFromParameter = true;
            break;
        case 10:
            force.falloff = (mvfxFalloff){.kind = 4, .radius = 1.0f};
            break;
        case 11:
            force.falloff = (mvfxFalloff){.kind = mvfx_falloffSphere};
            break;
        case 12:
            force.falloff =
                (mvfxFalloff){.kind = mvfx_falloffSphere, .radius = 1.0f, .exponent = 9.0f};
            break;
        case 13:
            force.falloff = (mvfxFalloff){.kind = mvfx_falloffTube, .radius = 1.0f};
            break;
        case 14:
            force.falloff = (mvfxFalloff){
                .kind = mvfx_falloffCone, .axis = {0, 0, 1}, .radius = 1.0f, .angle = 1.6f};
            break;
        default:
            bad.forceCount = MVFX_MAX_FORCES + 1;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &bad;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "refused");
    }
    mvfxEmitterDef none = Held(NULL, 1);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &none;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "no forces given");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestKinds();
    TestWindExact();
    TestFalloffs();
    TestSpaces();
    TestParameters();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
