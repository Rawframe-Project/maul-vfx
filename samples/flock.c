// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A flock round obstacles: sixty birds set off from a ball in every
// direction, gather by separation, alignment and cohesion, and follow a
// closed path round a square of 10 while two pillars stand on it, which
// they bounce off as colliders. A line a second gives the flock's order
// (the length of its summed velocity over the sum of its speeds), the
// birds' distances from the path and their contacts with the pillars (read
// as collision events). It returns 0 when the flock moves together along
// the path, has met the pillars and has no bird inside one.

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BIRDS 60
#define HALF  5.0f

// The pillars: spheres of radius 1 on the path's near and far sides.
static const mvfxCollider s_pillars[2] = {
    {.kind = mvfx_colliderSphere, .center = {0.0f, 0.0f, HALF}, .radius = 1.0f},
    {.kind = mvfx_colliderSphere, .center = {0.0f, 0.0f, -HALF}, .radius = 1.0f},
};

static float s_birds[BIRDS][6];

static mvfxEffectId MakeEffect(mvfxContext* context)
{
    static const mvfxBurst flock = {
        .minCount = BIRDS, .maxCount = BIRDS, .cycles = 1, .probability = 1.0f};
    // The path: the square's corners, joined.
    static const float corners[12] = {HALF,  0.0f, HALF,  -HALF, 0.0f, HALF,
                                      -HALF, 0.0f, -HALF, HALF,  0.0f, -HALF};

    mvfxEmitterDef birds = mvfxDefaultEmitterDef();
    birds.capacity = BIRDS;
    birds.rate = 0.0f;
    birds.bursts = &flock;
    birds.burstCount = 1;
    birds.lifetime = mvfxRangeSource(100.0f, 100.0f);
    birds.shape.kind = mvfx_shapeSphere;
    birds.shape.offset[0] = HALF;
    birds.shape.radius = 1.5f;
    birds.shape.thickness = 1.0f;
    birds.velocity = (mvfxVelocity){.mode = mvfx_directionFromCenter, .speed = {1.0f, 2.0f}};
    birds.neighbors =
        (mvfxNeighborDef){.enabled = true, .source = -1, .radius = 5.0f, .maxNeighbors = 32};
    birds.steering = (mvfxSteeringDef){
        .enabled = true,
        .separation = 4.0f,
        .separationRadius = 0.6f,
        .alignment = 1.5f,
        .alignmentRadius = 5.0f,
        .cohesion = 1.0f,
        .cohesionRadius = 5.0f,
        .viewAngle = 2.5f,
        .cruiseSpeed = 3.0f,
        .maxForce = 8.0f,
        .path = {.weight = 2.0f,
                 .points = corners,
                 .pointCount = 4,
                 .closed = true,
                 .radius = 0.5f,
                 .lookahead = 0.5f},
    };
    // Held to 4 a second.
    birds.velocityModules.limitSpeed = true;
    birds.velocityModules.speedLimit = (mvfxLifeCurve){.value = 4.0f};
    birds.velocityModules.limitDampen = 5.0f;
    birds.colliders = s_pillars;
    birds.colliderCount = 2;
    birds.collision = (mvfxCollision){.enabled = true, .restitution = 0.5f, .radius = 0.1f};
    birds.events = mvfx_eventMaskCollision;
    birds.eventCapacity = BIRDS;

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &birds;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

// The distance from a point in the XZ plane to the square's edges.
static double FromPath(double x, double z)
{
    double h = (double)HALF;
    double dx = fabs(fabs(x) - h);
    double dz = fabs(fabs(z) - h);
    // Within the square's span along an axis, the nearer edge across it;
    // past a corner, the corner.
    double alongX = fabs(x) <= h ? dz : hypot(fabs(x) - h, fabs(z) - h);
    double alongZ = fabs(z) <= h ? dx : hypot(fabs(x) - h, fabs(z) - h);
    return fmin(alongX, alongZ);
}

// What a second's report finds.
typedef struct Flock
{
    double order;
    double meanOff;
    double mostOff;
    bool clear;
} Flock;

// Writes the birds' places and velocities and reports the flock.
static bool Report(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId layout,
                   int32_t second, Flock* flock)
{
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t count = 0;
    if (mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, s_birds, BIRDS,
                                    &count) != mvfx_success ||
        count != BIRDS)
    {
        printf("the birds were not written\n");
        return false;
    }
    double sum[3] = {0.0, 0.0, 0.0};
    double speeds = 0.0;
    *flock = (Flock){.clear = true};
    for (int32_t i = 0; i < BIRDS; ++i)
    {
        const float* b = s_birds[i];
        sum[0] += (double)b[3];
        sum[1] += (double)b[4];
        sum[2] += (double)b[5];
        speeds += sqrt((double)(b[3] * b[3] + b[4] * b[4] + b[5] * b[5]));
        double off = hypot(FromPath((double)b[0], (double)b[2]), (double)b[1]);
        flock->meanOff += off / BIRDS;
        flock->mostOff = fmax(flock->mostOff, off);
        for (int32_t p = 0; p < 2; ++p)
        {
            const float* c = s_pillars[p].center;
            float dx = b[0] - c[0];
            float dy = b[1] - c[1];
            float dz = b[2] - c[2];
            flock->clear = flock->clear && dx * dx + dy * dy + dz * dz >= 0.99f;
        }
    }
    flock->order = hypot(hypot(sum[0], sum[1]), sum[2]) / speeds;
    printf("%2d s: order %.2f; the birds %.2f from the path on average, %.2f at most\n", second,
           flock->order, flock->meanOff, flock->mostOff);
    return flock->clear;
}

int main(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        printf("no context\n");
        return 1;
    }
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 24;
    mvfxLayoutId layout = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeEffect(context);
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
              mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    // Twenty seconds, past two laps of the square; the last five judged.
    Flock flock = {0};
    double order = 0.0;
    double mostOff = 0.0;
    int64_t contacts = 0;
    for (int32_t step = 1; ok && step <= 20 * 60; ++step)
    {
        const mvfxEvent* events = NULL;
        int32_t count = 0;
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
             mvfxInstance_GetEvents(context, instance, 0, &events, &count) == mvfx_success;
        contacts += count;
        ok = ok && (step % 60 != 0 || Report(context, instance, layout, step / 60, &flock));
        if (ok && step % 60 == 0)
        {
            printf("      %lld contacts with the pillars so far\n", (long long)contacts);
        }
        if (ok && step % 60 == 0 && step > 15 * 60)
        {
            order += flock.order / 5.0;
            mostOff = fmax(mostOff, flock.mostOff);
        }
    }
    mvfxDestroyContext(context);
    // Moving together, none far from the path, none in a pillar.
    ok = ok && order > 0.7 && mostOff < 2.0 && contacts > 0;
    printf(ok ? "the flock keeps its way round the pillars\n" : "the flock failed\n");
    return ok ? 0 : 1;
}
