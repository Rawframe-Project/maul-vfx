// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "water_couple.h"

#include "box.h"
#include "event.h"
#include "water.h"
#include "water_api.h"

#include <math.h>

void mvfxResolveWaterCoupling(mvfxInstance* instance, int32_t emitter,
                              const mvfxSpaceMap* fromWorld)
{
    const mvfxWaterCoupling* coupling = &instance->effect->emitters[emitter].water;
    mvfxResolvedWater* out = &instance->emitters[emitter].water;
    if (!coupling->enabled)
    {
        *out = (mvfxResolvedWater){0};
        return;
    }
    // Where the surface last stepped: with the instance where the step
    // starts, as the map from the world is.
    mvfxBoxPlace place = mvfxWaterPlaceAt(instance, coupling->water, &instance->from);
    *out = (mvfxResolvedWater){.coupling = coupling,
                               .water = &instance->effect->waters[coupling->water],
                               .state = &instance->waters[coupling->water],
                               .map = mvfxBoxToSpace(&place, fromWorld)};
    out->radius = coupling->radius / out->map.sigma;
}

// A particle's place x (in the particles' space) in the surface's frame,
// and the surface over it; false off the grid.
static bool Over(const mvfxResolvedWater* water, const float x[3], float local[3], float* height,
                 float* ground, float velocity[3])
{
    double place[3];
    mvfxUnmapPoint(&water->map, x, place);
    for (int32_t k = 0; k < 3; ++k)
    {
        local[k] = (float)place[k];
    }
    return mvfxWaterUnder(water->water, water->state, local[0], local[2], height, ground, velocity);
}

void mvfxWaterPull(const mvfxResolvedWater* water, const float x[3], float a[3], float* drag)
{
    float local[3];
    float height = 0.0f;
    float ground = 0.0f;
    float velocity[3];
    if (!Over(water, x, local, &height, &ground, velocity) || !(local[1] < height) ||
        !(local[1] > ground))
    {
        return;
    }
    const mvfxWaterCoupling* coupling = water->coupling;
    // In the surface's frame: the lift and the pull toward the water's
    // velocity, then into the particles' space.
    float lift[3] = {coupling->drag * velocity[0],
                     coupling->drag * velocity[1] + coupling->buoyancy * (height - local[1]),
                     coupling->drag * velocity[2]};
    float out[3];
    mvfxMapVector(&water->map, lift, water->map.sigma, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += out[k];
    }
    *drag += coupling->drag;
}

void mvfxMeetWater(mvfxInstance* instance, int32_t emitter, int32_t i, const float start[3],
                   const float end[3], int64_t tick, double seconds)
{
    const mvfxResolvedWater* water = &instance->emitters[emitter].water;
    mvfxWaterState* state = water->state;
    float from[3];
    float to[3];
    float height[2];
    float ground[2];
    float velocity[3];
    if (instance->crossScratch[i] != 0 ||
        !Over(water, start, from, &height[0], &ground[0], velocity) ||
        !Over(water, end, to, &height[1], &ground[1], velocity))
    {
        return;
    }
    // From above the surface to below it: met where the height over the
    // surface, linear along the move, is 0, over water there (shallow
    // water's ground below its surface).
    float above = from[1] - height[0];
    float below = to[1] - height[1];
    if (!(above >= 0.0f && below < 0.0f) || state->crossingCount >= state->crossingCapacity)
    {
        return;
    }
    float t = above / (above - below);
    float point[3];
    float at[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        point[k] = start[k] + t * (end[k] - start[k]);
        at[k] = from[k] + t * (to[k] - from[k]);
    }
    float met[2];
    (void)mvfxWaterUnder(water->water, state, at[0], at[2], &met[0], &met[1], velocity);
    if (!(met[0] > met[1]))
    {
        return;
    }
    instance->crossScratch[i] = 1;
    const mvfxWaterCoupling* coupling = water->coupling;
    float r = water->radius;
    float reach = fmaxf(r, water->water->def.cellSize);
    float push = coupling->push / water->map.sigma;
    state->crossings[state->crossingCount++] = (mvfxWaterCrossing){
        .at = {at[0], at[2]},
        .radius = reach,
        .depth = push * (r / reach) * (r / reach),
        .volume = coupling->pour ? (float)(4.0 / 3.0 * 3.14159265358979323846) * r * r * r : 0.0f,
    };
    instance->killScratch[i] |= coupling->pour ? 1 : 0;
    // The surface's normal turned into the particles' space, its length
    // kept.
    float normal[3];
    float turned[3];
    mvfxWaterNormal(water->water, state, at[0], at[2], normal);
    mvfxMapVector(&water->map, normal, 1.0f, turned);
    int64_t when = tick + (int64_t)floor((double)t * seconds * (double)MVFX_TICKS_PER_SECOND + 0.5);
    mvfxRecordEvent(instance, emitter,
                    &(mvfxEventFact){.kind = mvfx_eventWater,
                                     .particle = i,
                                     .tick = when,
                                     .placeTick = when,
                                     .point = point,
                                     .normal = turned});
}
