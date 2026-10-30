// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A pond: a water surface of waves, 6.4 units a side, with thirty leaves
// dropped onto it that float half under and drift with it, pushing it
// where they land. A second in, the host throws a stone in (a push), and
// each frame it samples the surface under a boat to bob it, as a game
// floats its own objects. A line a second gives the boat's height and how
// far the leaves are from the surface under them. It returns 0 when every
// leaf floats on the surface and the stone's waves have reached the boat.

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"
#include "maul-vfx/water.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LEAVES 30

static float s_leaves[LEAVES][3];
static double s_points[LEAVES][3];
static mvfxWaterSample s_under[LEAVES];

static mvfxEffectId MakeEffect(mvfxContext* context)
{
    mvfxWaterDef pond = mvfxDefaultWaterDef();
    pond.waveSpeed = 1.5f;
    pond.damping = 0.2f;

    // The leaves: thirty at once on a disc of radius 2, half a unit up,
    // falling onto the water, floating with their centers at its surface
    // (buoyancy 2) and drawn along with it.
    static const mvfxBurst drop = {
        .minCount = LEAVES, .maxCount = LEAVES, .cycles = 1, .probability = 1.0f};
    mvfxEmitterDef leaves = mvfxDefaultEmitterDef();
    leaves.capacity = LEAVES;
    leaves.rate = 0.0f;
    leaves.bursts = &drop;
    leaves.burstCount = 1;
    leaves.lifetime = mvfxRangeSource(100.0f, 100.0f);
    leaves.shape.kind = mvfx_shapeCircle;
    leaves.shape.offset[1] = 0.2f;
    leaves.shape.radius = 2.0f;
    leaves.shape.thickness = 1.0f;
    leaves.shape.rotation[0] = 0.70710678f; // the disc turned into the XZ plane
    leaves.shape.rotation[3] = 0.70710678f;
    leaves.motion.gravity[1] = -9.8f;
    leaves.water = (mvfxWaterCoupling){
        .enable = true, .water = 0, .radius = 0.05f, .push = 0.1f, .buoyancy = 2.0f, .drag = 10.0f};

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &leaves;
    effectDef.emitterCount = 1;
    effectDef.waters = &pond;
    effectDef.waterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect, NULL) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

// The farthest any leaf's center is from the surface under it.
static bool Leaves(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId layout,
                   double* farthestOut)
{
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t count = 0;
    if (mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, s_leaves, LEAVES,
                                    &count) != mvfx_success ||
        count != LEAVES)
    {
        return false;
    }
    for (int32_t i = 0; i < LEAVES; ++i)
    {
        s_points[i][0] = (double)s_leaves[i][0];
        s_points[i][1] = (double)s_leaves[i][1];
        s_points[i][2] = (double)s_leaves[i][2];
    }
    if (mvfxInstance_SampleWater(context, instance, 0, &s_points[0][0], LEAVES, s_under) !=
        mvfx_success)
    {
        return false;
    }
    *farthestOut = 0.0;
    for (int32_t i = 0; i < LEAVES; ++i)
    {
        *farthestOut = fmax(*farthestOut, fabs(s_points[i][1] - s_under[i].surface[1]));
    }
    return true;
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
    static const mvfxLayoutField place = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &place;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeEffect(context);
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
              mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    // The boat, near a corner, 4.6 from where the stone falls.
    static const double boat[3] = {-2.5, 0.0, -2.5};
    const mvfxWaterPush stone = {.point = {1.0, 0.0, 0.5}, .radius = 0.5f, .depth = 0.3f};
    double lowest = 0.0;
    double highest = 0.0;
    double farthest = 0.0;
    // Five seconds at 60 frames a second.
    for (int32_t step = 1; ok && step <= 5 * 60; ++step)
    {
        mvfxWaterSample under;
        ok = (step != 60 ||
              mvfxInstance_PushWater(context, instance, 0, &stone, 1) == mvfx_success) &&
             mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
             mvfxInstance_SampleWater(context, instance, 0, boat, 1, &under) == mvfx_success;
        // The boat rides the surface: its height after the stone fell.
        if (ok && step > 60)
        {
            lowest = fmin(lowest, under.surface[1]);
            highest = fmax(highest, under.surface[1]);
        }
        if (ok && step % 60 == 0)
        {
            ok = Leaves(context, instance, layout, &farthest);
            printf("%d s: the boat at %+.4f, leaning (%.3f %.3f %.3f); the leaves within %.3f "
                   "of the surface\n",
                   step / 60, under.surface[1], (double)under.normal[0], (double)under.normal[1],
                   (double)under.normal[2], farthest);
        }
    }
    mvfxWaterInfo info;
    ok = ok && mvfxInstance_GetWaterInfo(context, instance, 0, &info) == mvfx_success &&
         info.stepsSlowed == 0;
    mvfxDestroyContext(context);
    printf("the boat bobbed from %+.4f to %+.4f\n", lowest, highest);
    // Each leaf floats with its center at the surface, within its radius;
    // the stone's waves rocked the boat.
    ok = ok && farthest < 0.05 && highest - lowest > 0.02;
    printf(ok ? "the pond holds its leaves\n" : "the pond failed\n");
    return ok ? 0 : 1;
}
