// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "water_couple.h"

#include "box.h"
#include "event.h"
#include "water.h"
#include "water_api.h"

#include <math.h>

void mvfxResolveWaterCoupling(mvfxInstance* instance, int32_t emitter,
                              const mvfxSpaceMap* fromWorld, const float gravity[3])
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
    static const float y[3] = {0.0f, 1.0f, 0.0f};
    mvfxMapVector(&out->map, y, 1.0f, out->up);
    float g[3];
    mvfxUnmapVector(&out->map, gravity, g);
    out->strength = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
}

// A vector of the particles' space along the surface's Y, in its lengths.
static float Along(const mvfxResolvedWater* water, const float v[3])
{
    return (water->up[0] * v[0] + water->up[1] * v[1] + water->up[2] * v[2]) / water->map.sigma;
}

// A particle's place x (in the particles' space) in the surface's frame,
// and the surface's height and ground over it; false off the grid.
static bool Over(const mvfxResolvedWater* water, const float x[3], float local[3], float* height,
                 float* ground)
{
    double place[3];
    mvfxUnmapPoint(&water->map, x, place);
    for (int32_t k = 0; k < 3; ++k)
    {
        local[k] = (float)place[k];
    }
    return mvfxWaterHeight(water->water, water->state, local[0], local[2], height, ground);
}

mvfxWaterTouch mvfxWaterDrag(const mvfxResolvedWater* water, const float x[3], float a[3],
                             float* drag)
{
    mvfxWaterTouch touch = {0};
    double place[3];
    mvfxUnmapPoint(&water->map, x, place);
    for (int32_t k = 0; k < 3; ++k)
    {
        touch.at[k] = (float)place[k];
    }
    // The four cells about it, read once for its height and its flow.
    int32_t lo[2];
    float t[2];
    float ground = 0.0f;
    if (!mvfxWaterCorners(water->water, touch.at[0], touch.at[2], lo, t))
    {
        return touch;
    }
    mvfxWaterHeightOf(water->water, water->state, lo, t, &touch.height, &ground);
    if (!(touch.at[1] > ground))
    {
        return touch;
    }
    touch.over = true;
    float r = water->radius;
    float share = fminf(fmaxf((touch.height - touch.at[1] + r) / (2.0f * r), 0.0f), 1.0f);
    float pull = water->coupling->drag * share;
    if (!(pull > 0.0f))
    {
        return touch;
    }
    // In the surface's frame toward the water's velocity, then into the
    // particles' space.
    float flow[3];
    mvfxWaterVelocityOf(water->water, water->state, lo, t, flow);
    float toward[3] = {pull * flow[0], pull * flow[1], pull * flow[2]};
    float out[3];
    mvfxMapVector(&water->map, toward, water->map.sigma, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += out[k];
    }
    *drag += pull;
    return touch;
}

void mvfxWaterLift(const mvfxResolvedWater* water, const mvfxWaterTouch* touch, const float v[3],
                   float carry, float push, float a[3])
{
    if (!touch->over)
    {
        return;
    }
    // Along the surface's Y: where the step ends without the lift, c, and
    // the lift's pieces, full below h - r, none above h + r, falling
    // linearly between (a spring of stiffness k); the end y1 = c + push
    // L(y1) is on one of them, y - push L(y) rising in y.
    float r = water->radius;
    float full = water->coupling->buoyancy * water->strength;
    float c = touch->at[1] + Along(water, v) * carry + Along(water, a) * push;
    float top = touch->height + r;
    float lift = 0.0f;
    if (c + push * full <= touch->height - r)
    {
        lift = full;
    }
    else if (c < top)
    {
        float k = full / (2.0f * r);
        float end = (c + push * k * top) / (1.0f + push * k);
        lift = k * (top - end);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += water->up[k] * lift * water->map.sigma;
    }
}

void mvfxMeetWater(mvfxInstance* instance, int32_t emitter, int32_t i, const mvfxWaterTouch* touch,
                   const float start[3], const float end[3], int64_t tick, double seconds)
{
    const mvfxResolvedWater* water = &instance->emitters[emitter].water;
    mvfxWaterState* state = water->state;
    // From above the surface (the start's, as touched) to below it: met
    // where the height over the surface, linear along the move, is 0, over
    // water there (shallow water's ground below its surface).
    const float* from = touch->at;
    float above = from[1] - touch->height;
    float to[3];
    float height = 0.0f;
    float ground = 0.0f;
    if (instance->crossScratch[i] != 0 || !touch->over || !(above >= 0.0f) ||
        !Over(water, end, to, &height, &ground))
    {
        return;
    }
    float below = to[1] - height;
    if (!(below < 0.0f) || state->crossingCount >= state->crossingCapacity)
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
    (void)mvfxWaterHeight(water->water, state, at[0], at[2], &met[0], &met[1]);
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
