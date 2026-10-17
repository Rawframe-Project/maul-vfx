// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The numbers of an emitter's step on the GPU. The kernels' floats are
// compared with the CPU path's within a tolerance, so these need not be
// its bits: the exponential is the part's own, in binary64.

#include "numbers.h"

#include <string.h>

// e^-x for x >= 0: x halved until small, a series, squared back.
static double ExpMinus(double x)
{
    int32_t halvings = 0;
    while (x > 0x1p-10 && halvings < 64)
    {
        x *= 0.5;
        ++halvings;
    }
    double e = 1.0 - x * (1.0 - x * (0.5 - x * (1.0 / 6.0 - x / 24.0)));
    for (int32_t i = 0; i < halvings; ++i)
    {
        e *= e;
    }
    return e;
}

// The drag factors over seconds: decay, carry and push (the integrator's
// exact form).
static void DragFactors(double drag, double seconds, float out[3])
{
    double x = drag * seconds;
    double decay = ExpMinus(x);
    double phi1 = x < 1e-6 ? 1.0 - 0.5 * x : (1.0 - decay) / x;
    double phi2 = x < 1e-6 ? 0.5 - x / 6.0 : (1.0 - phi1) / x;
    out[0] = (float)decay;
    out[1] = (float)(seconds * phi1);
    out[2] = (float)(seconds * seconds * phi2);
}

// Turns v by the unit quaternion q.
static void Rotate(const float q[4], const float v[3], float out[3])
{
    float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
    float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
    float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

// Gravity in the particles' space, by the instance at the plan's start.
static void Gravity(const mvfxEmitterView* view, const mvfxPlanPlace* from, float out[3])
{
    if (view->gravitySpace == view->space)
    {
        memcpy(out, view->gravity, 3 * sizeof(float));
        return;
    }
    const float* q = from->rotation;
    if (view->gravitySpace == mvfx_spaceLocal)
    {
        float scaled[3] = {view->gravity[0] * from->scale, view->gravity[1] * from->scale,
                           view->gravity[2] * from->scale};
        Rotate(q, scaled, out);
        return;
    }
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float turned[3];
    Rotate(inverse, view->gravity, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = turned[k] / from->scale;
    }
}

// Writes four floats' bits at words.
static void Floats(uint32_t* words, float a, float b, float c, float d)
{
    const float f[4] = {a, b, c, d};
    memcpy(words, f, sizeof f);
}

// A place's origin from the anchor, and its scale, then its rotation.
static void Place(uint32_t* words, const mvfxPlanPlace* place, const double anchor[3])
{
    Floats(words, (float)(place->origin[0] - anchor[0]), (float)(place->origin[1] - anchor[1]),
           (float)(place->origin[2] - anchor[2]), place->scale);
    memcpy(words + 4, place->rotation, 4 * sizeof(float));
}

uint32_t mvfxUpdateGroups(int32_t capacity)
{
    uint32_t groups = ((uint32_t)capacity + 63u) / 64u;
    return groups < 65535u ? groups : 65535u;
}

uint32_t mvfxScanBlocks(int32_t capacity)
{
    return (3u * (uint32_t)capacity + 1023u) / 1024u;
}

// The drag every particle has: the emitter's and that of its winds and
// velocity fields without a falloff, summed in the order the core sums a
// particle's, so the two are equal where they should be.
static float SharedDrag(const mvfxEmitterView* view, const mvfxEmitterPlan* plan)
{
    float wind = 0.0f;
    for (int32_t f = 0; f < plan->forceCount; ++f)
    {
        const mvfxPlannedForce* force = &plan->forces[f];
        bool field = force->kind == mvfx_forceTurbulence || force->kind == mvfx_forceVectorField;
        float drag = force->kind == mvfx_forceWind                     ? force->strength
                     : field && force->fieldMode == mvfx_fieldVelocity ? force->tightness
                                                                       : 0.0f;
        wind += force->falloff == mvfx_falloffNone ? 1.0f * drag : 0.0f;
    }
    return view->drag + wind;
}

// A field's 108 words: its map's turn; its octaves and seed; its scale
// and the octaves' gain; its scroll; the octaves' frequencies (8), cells
// (8 of 4 words) and fractions (8 of 4); a vector field's box (origin,
// size, turn), its grid (first word, size) and the grid's format and
// border.
static void Field(const mvfxPlannedForce* force, const uint32_t grid[5], uint32_t* words)
{
    memcpy(words, force->rotation, 4 * sizeof(float));
    words[4] = (uint32_t)force->octaves;
    words[5] = force->seed;
    Floats(words + 8, force->sigma, force->gain, 0.0f, 0.0f);
    Floats(words + 12, force->scroll[0], force->scroll[1], force->scroll[2], 0.0f);
    memcpy(words + 16, force->frequency, MVFX_MAX_OCTAVES * sizeof(float));
    for (int32_t o = 0; o < MVFX_MAX_OCTAVES; ++o)
    {
        memcpy(words + 24 + 4 * o, force->cell[o], 3 * sizeof(uint32_t));
        memcpy(words + 56 + 4 * o, force->fraction[o], 3 * sizeof(float));
    }
    Floats(words + 88, force->boxOrigin[0], force->boxOrigin[1], force->boxOrigin[2], 0.0f);
    Floats(words + 92, force->boxSize[0], force->boxSize[1], force->boxSize[2], 0.0f);
    memcpy(words + 96, force->boxRotation, 4 * sizeof(float));
    memcpy(words + 100, grid, 4 * sizeof(uint32_t));
    words[104] = grid[4];
    words[105] = force->border;
}

// The forces: a head (their count, whether any kills, the shared drag),
// 24 words each, then each one's field.
static void Forces(const mvfxEmitterPlan* plan, float sharedDrag, const uint32_t (*gridWords)[5],
                   uint32_t* words)
{
    bool kills = false;
    for (int32_t f = 0; f < plan->forceCount; ++f)
    {
        const mvfxPlannedForce* force = &plan->forces[f];
        uint32_t* at = words + 4 + 24 * f;
        at[0] = force->kind;
        at[1] = force->falloff;
        at[2] = force->fieldMode;
        Floats(at + 4, force->vector[0], force->vector[1], force->vector[2], force->strength);
        Floats(at + 8, force->point[0], force->point[1], force->point[2], force->softening);
        Floats(at + 12, force->center[0], force->center[1], force->center[2], force->radius);
        Floats(at + 16, force->axis[0], force->axis[1], force->axis[2], force->cosine);
        Floats(at + 20, force->killRadius, force->pull, force->exponent, force->tightness);
        kills = kills || (force->kind == mvfx_forceAttractor && force->killRadius > 0.0f);
        Field(force, gridWords[f], words + 4 + 24 * MVFX_MAX_FORCES + 108 * f);
    }
    words[0] = (uint32_t)plan->forceCount;
    words[1] = kills ? 1u : 0u;
    memcpy(&words[2], &sharedDrag, sizeof sharedDrag);
}

// The collision: a head (the colliders' count, whether a contact kills),
// the response (restitution, friction, life loss, rest speed), then 20
// words a collider: its kind and inside; center and radius; normal; half
// extents; rotation.
static void Colliders(const mvfxEmitterPlan* plan, uint32_t* words)
{
    words[0] = (uint32_t)plan->colliderCount;
    words[1] = plan->killOnCollision || plan->lifeLoss >= 1.0f ? 1u : 0u;
    Floats(words + 4, plan->restitution, plan->friction, plan->lifeLoss, plan->restSpeed);
    for (int32_t c = 0; c < plan->colliderCount; ++c)
    {
        const mvfxPlannedCollider* collider = &plan->colliders[c];
        uint32_t* at = words + 8 + 20 * c;
        at[0] = collider->kind;
        at[1] = collider->inside ? 1u : 0u;
        Floats(at + 4, collider->center[0], collider->center[1], collider->center[2],
               collider->radius);
        Floats(at + 8, collider->normal[0], collider->normal[1], collider->normal[2], 0.0f);
        Floats(at + 12, collider->halfExtents[0], collider->halfExtents[1],
               collider->halfExtents[2], 0.0f);
        memcpy(at + 16, collider->rotation, 4 * sizeof(float));
    }
}

void mvfxStepNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, int32_t emitter,
                     const uint32_t (*gridWords)[5], uint32_t words[MVFX_STEP_WORDS])
{
    memset(words, 0, MVFX_STEP_WORDS * sizeof(uint32_t));
    uint64_t start = (uint64_t)plan->start;
    const uint32_t counts[12] = {(uint32_t)plan->capacity,
                                 (uint32_t)plan->birthCount,
                                 (uint32_t)plan->ticks,
                                 plan->seed,
                                 (uint32_t)start,
                                 (uint32_t)(start >> 32u),
                                 (uint32_t)emitter,
                                 view->space,
                                 view->velocity.mode,
                                 view->shape.kind,
                                 mvfxUpdateGroups(plan->capacity),
                                 0};
    memcpy(words, counts, sizeof counts);
    // The anchor's shift (none for an emitter in the instance's space,
    // whose anchor stays).
    const double* before = plan->anchorBefore;
    const double* after = plan->anchorAfter;
    Floats(words + 12, (float)(before[0] - after[0]), (float)(before[1] - after[1]),
           (float)(before[2] - after[2]), view->inheritVelocity);
    float sharedDrag = SharedDrag(view, plan);
    float factors[3];
    DragFactors((double)sharedDrag, (double)plan->ticks / (double)MVFX_TICKS_PER_SECOND, factors);
    Floats(words + 16, factors[0], factors[1], factors[2], view->drag);
    float gravity[3];
    Gravity(view, &plan->from, gravity);
    Floats(words + 20, gravity[0], gravity[1], gravity[2], 0.0f);
    const float* m = view->shape.matrix;
    for (int32_t r = 0; r < 3; ++r)
    {
        Floats(words + 24 + 4 * r, m[3 * r], m[3 * r + 1], m[3 * r + 2], 0.0f);
    }
    const mvfxShapeView* s = &view->shape;
    Floats(words + 36, s->offset[0], s->offset[1], s->offset[2], s->radius);
    Floats(words + 40, s->thickness, 0.0f, 0.0f, 0.0f);
    const mvfxVelocityView* v = &view->velocity;
    Floats(words + 44, v->direction[0], v->direction[1], v->direction[2], v->coneCosine);
    Floats(words + 48, v->tangentA[0], v->tangentA[1], v->tangentA[2], v->randomize);
    Floats(words + 52, v->tangentB[0], v->tangentB[1], v->tangentB[2], v->speedMin);
    Floats(words + 56, v->speedMax, 0.0f, 0.0f, 0.0f);
    Place(words + 60, &plan->from, after);
    Place(words + 68, &plan->to, after);
    Floats(words + 76, plan->velocity[0], plan->velocity[1], plan->velocity[2], 0.0f);
    Forces(plan, sharedDrag, gridWords, words + 80);
    Colliders(plan, words + 84 + 132 * MVFX_MAX_FORCES);
}
