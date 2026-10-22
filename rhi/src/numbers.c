// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The numbers of an emitter's step on the GPU. The kernels' floats are
// compared with the CPU path's within a tolerance, so these need not be
// its bits: the exponential is the part's own, in binary64.

#include "numbers.h"

#include <math.h>
#include <string.h>

double mvfxExpMinus(double x)
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
    double decay = mvfxExpMinus(x);
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

// A place's origin from the anchor, and its scale, then its rotation.
static void Place(uint32_t* words, const mvfxPlanPlace* place, const double anchor[3])
{
    mvfxFloatWords(words, (float)(place->origin[0] - anchor[0]),
                   (float)(place->origin[1] - anchor[1]), (float)(place->origin[2] - anchor[2]),
                   place->scale);
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
        bool field = force->kind == mvfx_forceTurbulence || force->kind == mvfx_forceVectorField ||
                     force->kind == mvfx_forceGas;
        float drag = force->kind == mvfx_forceWind                     ? force->strength
                     : field && force->fieldMode == mvfx_fieldVelocity ? force->tightness
                                                                       : 0.0f;
        wind += force->falloff == mvfx_falloffNone ? 1.0f * drag : 0.0f;
    }
    return view->drag + wind;
}

// A field's 108 words: its map's turn; its octaves and seed; its scale
// and the octaves' gain; its scroll; the octaves' frequencies (8), cells
// (8 of 4 words) and fractions (8 of 4); a vector field's or a gas's box
// (origin, size, turn), its grid (first word, size: a gas's faces and
// cells) and the grid's format and border.
static void Field(const mvfxPlannedForce* force, const uint32_t grid[5], uint32_t* words)
{
    memcpy(words, force->rotation, 4 * sizeof(float));
    words[4] = (uint32_t)force->octaves;
    words[5] = force->seed;
    mvfxFloatWords(words + 8, force->sigma, force->gain, 0.0f, 0.0f);
    mvfxFloatWords(words + 12, force->scroll[0], force->scroll[1], force->scroll[2], 0.0f);
    memcpy(words + 16, force->frequency, MVFX_MAX_OCTAVES * sizeof(float));
    for (int32_t o = 0; o < MVFX_MAX_OCTAVES; ++o)
    {
        memcpy(words + 24 + 4 * o, force->cell[o], 3 * sizeof(uint32_t));
        memcpy(words + 56 + 4 * o, force->fraction[o], 3 * sizeof(float));
    }
    mvfxFloatWords(words + 88, force->boxOrigin[0], force->boxOrigin[1], force->boxOrigin[2], 0.0f);
    mvfxFloatWords(words + 92, force->boxSize[0], force->boxSize[1], force->boxSize[2], 0.0f);
    memcpy(words + 96, force->boxRotation, 4 * sizeof(float));
    memcpy(words + 100, grid, 4 * sizeof(uint32_t));
    words[104] = grid[4];
    words[105] = force->border;
}

void mvfxForceWords(const mvfxPlannedForce* forces, int32_t count, float sharedDrag,
                    const uint32_t (*gridWords)[5], uint32_t* words)
{
    bool kills = false;
    for (int32_t f = 0; f < count; ++f)
    {
        const mvfxPlannedForce* force = &forces[f];
        uint32_t* at = words + 4 + 24 * f;
        at[0] = force->kind;
        at[1] = force->falloff;
        at[2] = force->fieldMode;
        mvfxFloatWords(at + 4, force->vector[0], force->vector[1], force->vector[2],
                       force->strength);
        mvfxFloatWords(at + 8, force->point[0], force->point[1], force->point[2], force->softening);
        mvfxFloatWords(at + 12, force->center[0], force->center[1], force->center[2],
                       force->radius);
        mvfxFloatWords(at + 16, force->axis[0], force->axis[1], force->axis[2], force->cosine);
        mvfxFloatWords(at + 20, force->killRadius, force->pull, force->exponent, force->tightness);
        kills = kills || (force->kind == mvfx_forceAttractor && force->killRadius > 0.0f);
        Field(force, gridWords[f], words + 4 + 24 * MVFX_MAX_FORCES + 108 * f);
    }
    words[0] = (uint32_t)count;
    words[1] = kills ? 1u : 0u;
    memcpy(&words[2], &sharedDrag, sizeof sharedDrag);
}

// The collision: a head (the colliders' count, whether a contact kills),
// the response (restitution, friction, life loss, rest speed), then 32
// words a collider: its kind and inside; center and radius; normal; half
// extents; rotation; its grid (first word, size), the grid's format; its
// scale and grow.
static void Colliders(const mvfxEmitterPlan* plan, const uint32_t (*grids)[5], bool normals,
                      uint32_t* words)
{
    const mvfxPlannedDepth* depth = &plan->depth;
    words[0] = (uint32_t)plan->colliderCount;
    words[1] = plan->killOnCollision || plan->lifeLoss >= 1.0f ? 1u : 0u;
    words[2] = plan->colliderCount > 0 || depth->enabled ? 1u : 0u;
    if (depth->enabled)
    {
        const uint32_t head[8] = {1u,
                                  (uint32_t)depth->width,
                                  (uint32_t)depth->height,
                                  (uint32_t)depth->samples,
                                  depth->reversed ? 1u : 0u,
                                  depth->killOutsideView ? 1u : 0u,
                                  normals ? 1u : 0u,
                                  0u};
        memcpy(words + 8, head, sizeof head);
        mvfxFloatWords(words + 16, depth->radius, depth->thickness, 0.0f, 0.0f);
        memcpy(words + 20, depth->normalTurn, 4 * sizeof(float));
        memcpy(words + 24, depth->toClip, 16 * sizeof(float));
        memcpy(words + 40, depth->fromClip, 16 * sizeof(float));
    }
    mvfxFloatWords(words + 4, plan->restitution, plan->friction, plan->lifeLoss, plan->restSpeed);
    for (int32_t c = 0; c < plan->colliderCount; ++c)
    {
        mvfxColliderWords(&plan->colliders[c], grids[c], words + 56 + 32 * c);
    }
}

void mvfxColliderWords(const mvfxPlannedCollider* collider, const uint32_t grid[5],
                       uint32_t words[MVFX_COLLIDER_WORDS])
{
    memset(words, 0, MVFX_COLLIDER_WORDS * sizeof(uint32_t));
    words[0] = collider->kind;
    words[1] = collider->inside ? 1u : 0u;
    mvfxFloatWords(words + 4, collider->center[0], collider->center[1], collider->center[2],
                   collider->radius);
    mvfxFloatWords(words + 8, collider->normal[0], collider->normal[1], collider->normal[2], 0.0f);
    mvfxFloatWords(words + 12, collider->halfExtents[0], collider->halfExtents[1],
                   collider->halfExtents[2], 0.0f);
    memcpy(words + 16, collider->rotation, 4 * sizeof(float));
    memcpy(words + 20, grid, 4 * sizeof(uint32_t));
    words[24] = grid[4];
    mvfxFloatWords(words + 28, collider->scale, collider->grow, 0.0f, 0.0f);
}

mvfxStepSpans mvfxStepSpansOf(const mvfxEmitterPlan* plan, bool beam)
{
    return (mvfxStepSpans){
        .first = {0, 84 + 132 * MVFX_MAX_FORCES, MVFX_VALUES_FIRST, MVFX_KILLS_FIRST},
        .count = {84 + 24 * MVFX_MAX_FORCES + 108 * (uint32_t)plan->forceCount,
                  56 + 32 * (uint32_t)plan->colliderCount,
                  MVFX_VALUES_WORDS - (beam ? 0u : MVFX_BEAM_WORDS),
                  24 + 20 * (uint32_t)plan->killRuleCount},
    };
}

void mvfxSourcesOf(const mvfxEmitterView* view, mvfxSourceView sources[MVFX_GPU_SOURCES])
{
    memset(sources, 0, MVFX_GPU_SOURCES * sizeof sources[0]);
    sources[MVFX_SOURCE_SIZE] = view->size;
    sources[MVFX_SOURCE_COLOR] = view->color;
    sources[MVFX_SOURCE_ANGLE] = view->angle;
    sources[MVFX_SOURCE_SPIN] = view->spin;
    sources[MVFX_SOURCE_ORIENTATION] = view->orientation;
    sources[MVFX_SOURCE_ANGULAR] = view->angularVelocity;
    sources[MVFX_SOURCE_DRAG] = view->dragScale;
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        sources[MVFX_SOURCE_CUSTOM + i] = view->custom[i];
    }
    sources[MVFX_SOURCE_LIFE] = view->lifetime;
}

uint32_t mvfxSpawnFirst(int32_t capacity, int32_t eventCapacity)
{
    return mvfxEventFirst(capacity) + MVFX_EVENT_WORDS * (uint32_t)eventCapacity;
}

uint64_t mvfxSpawnWords(int32_t sourceEventCapacity)
{
    return MVFX_SPAWN_EVENTS + (uint64_t)(MVFX_EVENT_WORDS + 7u) * (uint64_t)sourceEventCapacity;
}

// A plan's ticks from its start as a word, held to the plan.
static uint32_t PlanTick(int64_t tick, int64_t ticks)
{
    return (uint32_t)(tick < 0 ? 0 : (tick > ticks ? ticks : tick));
}

void mvfxSpawnNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                      const mvfxEmitterView* source, const mvfxEmitterPlan* sourcePlan,
                      int32_t hostRecords, uint32_t words[MVFX_STEP_WORDS])
{
    const mvfxEventSpawn* spawn = &view->eventSpawn;
    if (!spawn->enabled)
    {
        return;
    }
    uint32_t* head = words + MVFX_SPAWN_FIRST;
    bool spawns = plan->spawnFirst <= plan->spawnLast && plan->spawnLast >= 0 &&
                  plan->spawnFirst <= plan->ticks;
    head[0] = spawns ? PlanTick(plan->spawnFirst, plan->ticks) : 1u;
    head[1] = spawns ? PlanTick(plan->spawnLast, plan->ticks) : 0u;
    head[2] = (uint32_t)plan->hostBirthCount;
    head[3] = mvfxSpawnFirst(plan->capacity, view->eventCapacity);
    head[4] = (uint32_t)plan->owed;
    head[5] = (uint32_t)(plan->owed >> 32u);
    head[6] = 6u * (uint32_t)plan->capacity + 4u * (uint32_t)hostRecords;
    // The births' room, twice the capacity, in units for an emitter of
    // units (a birth a unit).
    head[7] =
        2u * (uint32_t)plan->capacity / (uint32_t)(view->unitPoints > 1 ? view->unitPoints : 1);
    const double* anchor = sourcePlan->anchorAfter;
    const double* own = plan->anchorAfter;
    const double* start = plan->from.origin;
    const double* end = plan->to.origin;
    mvfxFloatWords(head + 8, (float)(anchor[0] - own[0]), (float)(anchor[1] - own[1]),
                   (float)(anchor[2] - own[2]), 0.0f);
    mvfxFloatWords(head + 12, (float)(anchor[0] - start[0]), (float)(anchor[1] - start[1]),
                   (float)(anchor[2] - start[2]), 0.0f);
    mvfxFloatWords(head + 16, (float)(end[0] - start[0]), (float)(end[1] - start[1]),
                   (float)(end[2] - start[2]), 0.0f);
    uint32_t* rule = words + MVFX_SPAWN_RULE_FIRST;
    uint64_t delay = (uint64_t)view->delayTicks;
    uint64_t loop = (uint64_t)view->loopTicks;
    const uint32_t ruleWords[12] = {1u,
                                    (uint32_t)spawn->kind,
                                    (uint32_t)spawn->minCount,
                                    (uint32_t)(spawn->maxCount - spawn->minCount) + 1u,
                                    (uint32_t)spawn->capacity,
                                    (spawn->inheritColor ? 1u : 0u) |
                                        (spawn->inheritSize ? 2u : 0u) |
                                        (source->space == mvfx_spaceLocal ? 4u : 0u),
                                    0u,
                                    (uint32_t)source->eventCapacity,
                                    (uint32_t)delay,
                                    (uint32_t)(delay >> 32u),
                                    (uint32_t)loop,
                                    (uint32_t)(loop >> 32u)};
    memcpy(rule, ruleWords, sizeof ruleWords);
    memcpy(&rule[6], &spawn->inheritVelocity, sizeof(float));
}

void mvfxHostBirthWords(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, uint32_t* words)
{
    bool world = view->space == mvfx_spaceWorld;
    for (int32_t h = 0; h < plan->hostBirthCount; ++h)
    {
        const mvfxPlannedHostBirth* host = &plan->hostBirths[h];
        uint32_t* at = words + MVFX_HOST_BIRTH_WORDS * h;
        float place[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            place[k] =
                (float)(world ? host->position[k] - plan->anchorAfter[k] : host->position[k]);
        }
        mvfxFloatWords(at, place[0], place[1], place[2], 0.0f);
        at[3] = host->given;
        const float* v = host->velocity;
        mvfxFloatWords(at + 4, v[0], v[1], v[2], 0.0f);
        // For units held to the host's frames, the unit's frame.
        at[7] = (uint32_t)host->frame;
        memcpy(at + 8, host->color, 4 * sizeof(float));
        mvfxFloatWords(at + 12, host->size[0], host->size[1], host->size[2], 0.0f);
    }
}

void mvfxRecordLivesOf(const mvfxEmitterView* view, mvfxLifeView lives[MVFX_RECORD_LIVES])
{
    const mvfxOverLifeView* o = &view->overLife;
    lives[0] = o->alpha;
    for (int32_t k = 0; k < 3; ++k)
    {
        lives[1 + k] = o->sizes[k];
    }
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        lives[4 + i] = o->custom[i];
    }
}

bool mvfxIsRecordLayout(const mvfxLayoutView* layout)
{
    bool written = layout->stride % 4 == 0;
    for (int32_t f = 0; written && f < layout->fieldCount; ++f)
    {
        written = layout->fields[f].field <= mvfx_recordMesh;
    }
    return written;
}

bool mvfxIsRibbonLayout(const mvfxLayoutView* layout)
{
    bool written = layout->stride % 4 == 0;
    for (int32_t f = 0; written && f < layout->fieldCount; ++f)
    {
        written = layout->fields[f].field <= mvfx_recordCorner;
    }
    return written;
}

// The pinned head's place in the particles' space, from the plan's in the
// world in binary64 (records.wgsl's map undone): from the anchor in the
// world's space, else from the instance's origin turned back and scaled.
static void HeadInParticles(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, float out[3])
{
    bool world = view->space == mvfx_spaceWorld;
    const double* from = world ? plan->anchorAfter : plan->to.origin;
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = plan->ribbonHeadPlace[k] - from[k];
    }
    if (world)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = (float)d[k];
        }
        return;
    }
    // The turn's inverse: v + w t + u x t, t = 2 u x v, u the axis negated.
    const float* q = plan->to.rotation;
    const double u[3] = {-(double)q[0], -(double)q[1], -(double)q[2]};
    const double t[3] = {2.0 * (u[1] * d[2] - u[2] * d[1]), 2.0 * (u[2] * d[0] - u[0] * d[2]),
                         2.0 * (u[0] * d[1] - u[1] * d[0])};
    const double turned[3] = {d[0] + (double)q[3] * t[0] + (u[1] * t[2] - u[2] * t[1]),
                              d[1] + (double)q[3] * t[1] + (u[2] * t[0] - u[0] * t[2]),
                              d[2] + (double)q[3] * t[2] + (u[0] * t[1] - u[1] * t[0])};
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (float)(turned[k] / (double)plan->to.scale);
    }
}

void mvfxRibbonNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                       const mvfxStepTables* tables, const mvfxRibbonSink* sink,
                       uint32_t words[MVFX_RIBBON_WORDS])
{
    const mvfxRibbonView* r = &view->ribbon;
    const uint32_t arrays[4] = {mvfxWordOf(view->arrays.ribbon),
                                mvfxWordOf(view->arrays.ribbonDistance), r->texture, r->facing};
    memcpy(words, arrays, sizeof arrays);
    // The core's offset, a binary64 product at the clock rounded once.
    double seconds = (double)(plan->start + plan->ticks) / (double)MVFX_TICKS_PER_SECOND;
    mvfxFloatWords(words + 4, r->tileLength, (float)((double)r->scroll * seconds), r->width.value,
                   0.0f);
    mvfxFloatWords(words + 8, r->normal[0], r->normal[1], r->normal[2], 0.0f);
    words[12] = tables->ribbonWidth;
    words[13] = tables->ribbonColor;
    words[14] =
        (uint32_t)(sink->capacity < (int64_t)UINT32_MAX ? sink->capacity : (int64_t)UINT32_MAX);
    words[15] = sink->countWord;
    const uint32_t cut[4] = {r->pieces > 1 ? (uint32_t)r->pieces : 1u,
                             r->bendCosines[0] >= -1.5f ? 1u : 0u, plan->ribbonHead ? 1u : 0u,
                             plan->ribbonCounter};
    memcpy(words + 16, cut, sizeof cut);
    float head[4] = {0};
    if (plan->ribbonHead)
    {
        HeadInParticles(view, plan, head);
    }
    memcpy(words + 20, head, sizeof head);
    const uint32_t sinks[4] = {sink->kind, sink->sides, sink->caps ? 1u : 0u, 0u};
    memcpy(words + 24, sinks, sizeof sinks);
    memcpy(words + 28, r->bendCosines, MVFX_MAX_RIBBON_PIECES * sizeof(float));
}

void mvfxSpriteNumbers(const mvfxEmitterView* view, const mvfxView* frame,
                       const mvfxRibbonSink* sink, uint32_t words[MVFX_RIBBON_WORDS])
{
    memset(words, 0, MVFX_RIBBON_WORDS * sizeof(uint32_t));
    mvfxFloatWords(words + 8, frame->up[0], frame->up[1], frame->up[2], view->sprite.stretch);
    words[12] = 0xFFFFFFFFu;
    words[13] = 0xFFFFFFFFu;
    words[14] =
        (uint32_t)(sink->capacity < (int64_t)UINT32_MAX ? sink->capacity : (int64_t)UINT32_MAX);
    words[15] = sink->countWord;
    words[24] = MVFX_SINK_SPRITES;
    words[25] = view->sprite.billboard;
}

void mvfxSheetNumbers(const mvfxEmitterView* view, const mvfxRibbonSink* sink,
                      uint32_t words[MVFX_RIBBON_WORDS])
{
    memset(words, 0, MVFX_RIBBON_WORDS * sizeof(uint32_t));
    words[0] = mvfxWordOf(view->arrays.torn);
    words[12] = 0xFFFFFFFFu;
    words[13] = 0xFFFFFFFFu;
    words[14] =
        (uint32_t)(sink->capacity < (int64_t)UINT32_MAX ? sink->capacity : (int64_t)UINT32_MAX);
    words[15] = sink->countWord;
    words[24] = MVFX_SINK_SHEETS;
    words[25] = (uint32_t)view->unitWidth;
    words[26] = (uint32_t)view->unitPoints;
}

// Where a particle's values are for the record pass (records.wgsl's
// Words).
static void RecordWords(const mvfxAttributeArrays* a, uint32_t* words)
{
    const uint32_t head[12] = {
        mvfxWordOf(a->position), mvfxWordOf(a->velocity),    mvfxWordOf(a->serial),
        mvfxWordOf(a->birth),    mvfxWordOf(a->inverseLife), mvfxWordOf(a->size),
        mvfxWordOf(a->color),    mvfxWordOf(a->angle),       mvfxWordOf(a->orientation),
        mvfxWordOf(a->frame),    mvfxWordOf(a->mesh),        0xFFFFFFFFu};
    memcpy(words, head, sizeof head);
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        words[12 + i] = mvfxWordOf(a->custom[i]);
    }
}

// The map into the view's space (records.wgsl's turn, offset and
// scales), as the CPU path unmaps: a world-space emitter's places are
// offsets from the anchor; an instance-space emitter's are turned by the
// instance and divided by one over its scale.
static void RecordMap(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                      const double origin[3], uint32_t* words)
{
    bool world = view->space == mvfx_spaceWorld;
    const mvfxPlanPlace* to = &plan->to;
    const double* from = world ? plan->anchorAfter : to->origin;
    if (world)
    {
        mvfxFloatWords(words, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        memcpy(words, to->rotation, 4 * sizeof(float));
    }
    mvfxFloatWords(words + 4, (float)(from[0] - origin[0]), (float)(from[1] - origin[1]),
                   (float)(from[2] - origin[2]), 0.0f);
    mvfxFloatWords(words + 8, world ? 1.0f : 1.0f / to->scale, world ? to->scale : 1.0f, 0.0f,
                   0.0f);
}

// The values over life records show (records.wgsl's color to lives).
static void RecordOverLife(const mvfxEmitterView* view, const mvfxStepTables* tables,
                           uint32_t* words)
{
    const mvfxOverLifeView* o = &view->overLife;
    const uint32_t color[4] = {o->color ? 1u : 0u, o->colorInput, tables->gradient,
                               o->scaleAlpha ? 1u : 0u};
    memcpy(words, color, sizeof color);
    mvfxFloatWords(words + 4, o->colorRange[0], o->colorRange[1], 0.0f, 0.0f);
    const uint32_t size[4] = {o->size ? 1u : 0u, o->sizeInput, o->perAxis ? 1u : 0u, 0u};
    memcpy(words + 8, size, sizeof size);
    mvfxFloatWords(words + 12, o->sizeRange[0], o->sizeRange[1], 0.0f, 0.0f);
    uint32_t components = 0;
    uint32_t scaled = 0;
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        components |= (uint32_t)view->custom[i].components << (4 * i);
        scaled |= o->customScaled[i] ? 1u << i : 0u;
    }
    words[16] = components;
    words[17] = scaled;
    const mvfxFrameOverLife* frame = &o->frame;
    float factor = frame->animation == mvfx_frameAtRate ? frame->frameRate : frame->cycles;
    words[20] = frame->animation;
    words[21] = (uint32_t)frame->frameCount;
    words[22] = frame->loop ? 1u : 0u;
    memcpy(&words[23], &factor, sizeof factor);
    mvfxLifeView lives[MVFX_RECORD_LIVES];
    mvfxRecordLivesOf(view, lives);
    for (int32_t k = 0; k < MVFX_RECORD_LIVES; ++k)
    {
        memcpy(&words[24 + 2 * k], &lives[k].value, sizeof(float));
        words[25 + 2 * k] = tables->recordLives[k];
    }
}

uint32_t mvfxEventFirst(int32_t capacity)
{
    return MVFX_EVENT_FACTS * (uint32_t)capacity + 4u * mvfxScanBlocks(capacity);
}

uint64_t mvfxEventBufferWords(int32_t capacity, int32_t eventCapacity)
{
    return mvfxEventFirst(capacity) + (uint64_t)MVFX_EVENT_WORDS * (uint64_t)eventCapacity;
}

uint32_t mvfxRecordOrder(const mvfxEmitterView* view)
{
    switch (view->sortMode)
    {
    case mvfx_sortYoungestFirst:
        return MVFX_ORDER_REVERSED;
    case mvfx_sortBackToFront:
    case mvfx_sortFrontToBack:
        return MVFX_ORDER_SORTED;
    default:
        return MVFX_ORDER_POOL;
    }
}

uint32_t mvfxSortBlocks(int32_t capacity)
{
    return ((uint32_t)capacity + MVFX_SORT_BLOCK - 1u) / MVFX_SORT_BLOCK;
}

// A record pass's order, the sort's measure, the keys' flip (back to front
// sorts their complements, so equal keys keep the pool's order) and the
// sort's workgroups; the eye's offset from the view's origin, rounded once,
// and the view's forward direction.
static void RecordOrder(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, const mvfxView* v,
                        uint32_t words[12])
{
    uint32_t blocks = mvfxSortBlocks(plan->capacity);
    words[0] = mvfxRecordOrder(view);
    words[1] = view->sortMeasure == mvfx_sortByDepth ? 1u : 0u;
    words[2] = view->sortMode == mvfx_sortBackToFront ? 0xFFFFFFFFu : 0u;
    words[3] = blocks < 65535u ? blocks : 65535u;
    float eye[4] = {0};
    float forward[4] = {0};
    for (int32_t k = 0; k < 3; ++k)
    {
        eye[k] = (float)(v->eye[k] - v->origin[k]);
        forward[k] = v->forward[k];
    }
    memcpy(words + 4, eye, sizeof eye);
    memcpy(words + 8, forward, sizeof forward);
}

void mvfxRecordNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                       const mvfxLayoutView* layout, const mvfxGpuRecordsDef* def,
                       const mvfxStepTables* tables, const float srgb[MVFX_SRGB_STEPS],
                       uint32_t words[MVFX_RECORD_WORDS])
{
    memset(words, 0, MVFX_RECORD_WORDS * sizeof(uint32_t));
    uint64_t end = (uint64_t)(plan->start + plan->ticks);
    int64_t capacity = def->capacity < (int64_t)UINT32_MAX ? def->capacity : (int64_t)UINT32_MAX;
    const uint32_t head[12] = {(uint32_t)plan->capacity,
                               (uint32_t)capacity,
                               (uint32_t)(def->offset / 4u),
                               layout->stride / 4u,
                               (uint32_t)layout->fieldCount,
                               0u,
                               def->count.index1 != 0 ? (uint32_t)(def->countOffset / 4u)
                                                      : 0xFFFFFFFFu,
                               0u,
                               (uint32_t)end,
                               (uint32_t)(end >> 32u),
                               0u,
                               0u};
    memcpy(words, head, sizeof head);
    RecordWords(&view->arrays, words + 12);
    RecordMap(view, plan, def->view.origin, words + MVFX_RECORD_MAP);
    RecordOverLife(view, tables, words + 44);
    memcpy(words + 92, srgb, MVFX_SRGB_STEPS * sizeof(float));
    for (int32_t f = 0; f < layout->fieldCount; ++f)
    {
        const mvfxLayoutField* field = &layout->fields[f];
        uint32_t* at = words + 348 + 4 * f;
        at[0] = field->field;
        at[1] = field->format;
        at[2] = field->components | (uint32_t)field->custom << 8u | (field->srgb ? 1u << 16u : 0u);
        at[3] = field->offset;
    }
    RecordOrder(view, plan, &def->view, words + MVFX_RECORD_ORDER);
}

void mvfxLightNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                      const mvfxGpuLightsDef* def, const mvfxStepTables* tables,
                      const float srgb[MVFX_SRGB_STEPS], uint32_t words[MVFX_RECORD_WORDS])
{
    mvfxGpuRecordsDef records = mvfxDefaultGpuRecordsDef();
    records.emitter = def->emitter;
    records.view = def->view;
    records.offset = def->offset;
    records.capacity = def->capacity;
    records.count = def->count;
    records.countOffset = def->countOffset;
    const mvfxLayoutView layout = {.stride = (uint32_t)sizeof(mvfxLight)};
    mvfxRecordNumbers(view, plan, &layout, &records, tables, srgb, words);
    const mvfxLightDef* light = &view->light;
    uint32_t blocks = mvfxSortBlocks(plan->capacity);
    uint32_t* at = words + MVFX_RECORD_LIGHT;
    at[0] = (uint32_t)light->every;
    at[1] = light->alphaScales ? 1u : 0u;
    at[2] = blocks < 65535u ? (blocks > 0u ? blocks : 1u) : 65535u;
    at[3] = 0u;
    mvfxFloatWords(at + 4, light->intensity, light->radius, light->radiusBySize, 0.0f);
}

void mvfxLivesOf(const mvfxEmitterView* view, mvfxLifeView lives[MVFX_GPU_LIVES])
{
    const mvfxModulesView* m = &view->modules;
    lives[0] = view->rotationSpeed;
    lives[1] = m->speedLimit;
    lives[2] = m->motionScale;
    lives[3] = m->addedSpeed;
    lives[4] = m->radialSpeed;
    lives[5] = m->orbitRate;
}

// An index's draw (sim.wgsl's draws): first, last, whether random.
static void IndexDraw(const mvfxIndexView* index, uint32_t* words)
{
    words[0] = (uint32_t)index->first;
    words[1] = (uint32_t)index->last;
    words[2] = index->random ? 1u : 0u;
    words[3] = 0u;
}

// The sources (sim.wgsl's Source, 16 words each), the frame's and mesh
// index's draws, the bounds' growth by size, how particles turn and move
// (the orientation's mode, the angular drag, whether the speed is limited
// and modules move them, how fast the speed falls to the limit) and the
// values over life (each one's value and curve).
static void Sources(const mvfxEmitterView* view, const mvfxStepTables* tables, uint32_t* words)
{
    mvfxSourceView sources[MVFX_GPU_SOURCES];
    mvfxSourcesOf(view, sources);
    for (int32_t s = 0; s < MVFX_GPU_SOURCES; ++s)
    {
        const mvfxSourceView* source = &sources[s];
        uint32_t* at = words + 16 * s;
        const uint32_t head[8] = {source->kind,
                                  source->uniform ? 1u : 0u,
                                  (uint32_t)source->components,
                                  (uint32_t)source->parameter,
                                  tables->sources[s][0],
                                  tables->sources[s][1],
                                  0u,
                                  0u};
        memcpy(at, head, sizeof head);
        memcpy(at + 8, source->min, 4 * sizeof(float));
        memcpy(at + 12, source->max, 4 * sizeof(float));
    }
    IndexDraw(&view->frame, words + 16 * MVFX_GPU_SOURCES);
    IndexDraw(&view->meshIndex, words + 16 * MVFX_GPU_SOURCES + 4);
    const float growth[4] = {view->sizeBounds[0], view->sizeBounds[1], view->sizeBounds[2],
                             view->sizeMargin};
    memcpy(words + 16 * MVFX_GPU_SOURCES + 8, growth, sizeof growth);
    const mvfxModulesView* m = &view->modules;
    uint32_t* turns = words + 16 * MVFX_GPU_SOURCES + 12;
    turns[0] = view->orientationMode;
    memcpy(&turns[1], &view->angularDrag, sizeof(float));
    turns[2] = (m->limitSpeed ? 1u : 0u) | (m->moves ? 2u : 0u);
    memcpy(&turns[3], &m->limitDampen, sizeof(float));
    mvfxLifeView lives[MVFX_GPU_LIVES];
    mvfxLivesOf(view, lives);
    for (int32_t k = 0; k < MVFX_GPU_LIVES; ++k)
    {
        uint32_t* at = words + 16 * MVFX_GPU_SOURCES + 16 + 2 * k;
        memcpy(&at[0], &lives[k].value, sizeof(float));
        at[1] = tables->lives[k];
    }
}

// The velocity modules (sim.wgsl's modules): the added direction and the
// scale into the particles' space, the orbit's axis and center, the
// radial center.
static void Modules(const mvfxPlannedModules* m, uint32_t* words)
{
    const float* d = m->addedDirection;
    mvfxFloatWords(words, d[0], d[1], d[2], m->sigma);
    mvfxFloatWords(words + 4, m->orbitAxis[0], m->orbitAxis[1], m->orbitAxis[2], 0.0f);
    mvfxFloatWords(words + 8, m->orbitCenter[0], m->orbitCenter[1], m->orbitCenter[2], 0.0f);
    mvfxFloatWords(words + 12, m->radialCenter[0], m->radialCenter[1], m->radialCenter[2], 0.0f);
}

// The kill rules' head (their count) and each rule's 20 words (sim.wgsl's
// KillRule): its kind, the collisions that kill, its least age in ticks
// (two words); its center and radius; its half sizes and speed; its
// rotation; its normal.
static void KillRules(const mvfxEmitterPlan* plan, uint32_t* words)
{
    words[0] = (uint32_t)plan->killRuleCount;
    for (int32_t r = 0; r < plan->killRuleCount; ++r)
    {
        const mvfxPlannedKillRule* rule = &plan->killRules[r];
        uint32_t* at = words + (MVFX_KILL_RULES_FIRST - MVFX_KILLS_FIRST) + 20 * r;
        at[0] = rule->kind;
        at[1] = (uint32_t)rule->collisions;
        at[2] = (uint32_t)rule->minimumAgeTicks;
        at[3] = (uint32_t)(rule->minimumAgeTicks >> 32u);
        const float* c = rule->center;
        const float* h = rule->halfExtents;
        mvfxFloatWords(at + 4, c[0], c[1], c[2], rule->radius);
        mvfxFloatWords(at + 8, h[0], h[1], h[2], rule->speed);
        memcpy(at + 12, rule->rotation, 4 * sizeof(float));
        mvfxFloatWords(at + 16, rule->normal[0], rule->normal[1], rule->normal[2], 0.0f);
    }
}

// Where a particle's values are among its words (sim.wgsl's Words): its
// values' first words, its arrays, then the death tick and the step's
// word; its custom values' and its neighbor attributes'.
static void Words(const mvfxAttributeArrays* arrays, uint32_t* words)
{
    uint32_t count = (uint32_t)arrays->count;
    const uint32_t head[20] = {mvfxWordOf(arrays->position),
                               mvfxWordOf(arrays->velocity),
                               mvfxWordOf(arrays->serial),
                               mvfxWordOf(arrays->birth),
                               mvfxWordOf(arrays->inverseLife),
                               mvfxWordOf(arrays->collisions),
                               count,
                               count + 2u,
                               mvfxWordOf(arrays->size),
                               mvfxWordOf(arrays->color),
                               mvfxWordOf(arrays->angle),
                               mvfxWordOf(arrays->spin),
                               mvfxWordOf(arrays->orientation),
                               mvfxWordOf(arrays->angular),
                               mvfxWordOf(arrays->frame),
                               mvfxWordOf(arrays->drag),
                               mvfxWordOf(arrays->mesh),
                               count,
                               mvfxWordOf(arrays->ribbon),
                               mvfxWordOf(arrays->ribbonDistance)};
    memcpy(words, head, sizeof head);
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        words[20 + i] = mvfxWordOf(arrays->custom[i]);
    }
    words[28] = mvfxWordOf(arrays->neighborCount);
    words[29] = mvfxWordOf(arrays->neighborCenter);
    words[30] = mvfxWordOf(arrays->neighborVelocity);
    words[31] = 0xFFFFFFFFu;
}

// The rigid particles (sim/rigid.wgsl's numbers): the shape, the
// substeps (0 for particles that are not rigid), the sleep's first word,
// a hull's vertices; the density, the sleep speed and time, a hull's
// volume at a size of 1; its second moments; how the size goes over life
// (whether, its input, whether a value an axis) and the hull's table, its
// speed range, a hull's face count and its faces' table (words, in the
// range's last two); the sizes over life (a value and its table each); the
// restitution, the static and dynamic friction and the body contacts.
static void Rigid(const mvfxEmitterView* view, const mvfxStepTables* tables, uint32_t rigid[36])
{
    const mvfxRigidDef* r = &view->rigid;
    const uint32_t head[4] = {r->shape, r->enabled ? (uint32_t)r->substeps : 0u,
                              mvfxWordOf(view->arrays.sleep), (uint32_t)view->hullVertexCount};
    memcpy(rigid, head, sizeof head);
    mvfxFloatWords(rigid + 4, r->density, r->sleepSpeed, r->sleepTime, view->hullVolume);
    mvfxFloatWords(rigid + 8, view->hullMoments[0], view->hullMoments[1], view->hullMoments[2],
                   view->hullMoments[3]);
    mvfxFloatWords(rigid + 12, view->hullMoments[4], view->hullMoments[5], 0.0f, 0.0f);
    const mvfxOverLifeView* o = &view->overLife;
    const uint32_t size[4] = {o->size ? 1u : 0u, o->sizeInput, o->perAxis ? 1u : 0u, tables->hull};
    memcpy(rigid + 16, size, sizeof size);
    mvfxFloatWords(rigid + 20, o->sizeRange[0], o->sizeRange[1], 0.0f, 0.0f);
    rigid[22] = (uint32_t)view->hullFaceCount;
    rigid[23] = tables->hullFaces;
    for (int32_t k = 0; k < 3; ++k)
    {
        memcpy(&rigid[24 + 2 * k], &o->sizes[k].value, sizeof(float));
        rigid[25 + 2 * k] = tables->recordLives[1 + k];
    }
    mvfxFloatWords(rigid + 32, r->restitution, r->staticFriction, r->dynamicFriction, 0.0f);
    rigid[35] = (uint32_t)r->bodyContacts;
}

// The beam as resolved for the step (sim.wgsl's beam, beamNoise and
// beamPoints): none without one.
static void Beam(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, uint32_t* at)
{
    const mvfxBeamDef* beam = &view->beam;
    if (!beam->enabled)
    {
        return;
    }
    at[0] = (uint32_t)beam->segments;
    at[1] = (uint32_t)beam->controlCount;
    at[2] = plan->beam.timeCell;
    mvfxFloatWords(at + 4, beam->noiseAmplitude, beam->noiseFrequency, plan->beam.timeFraction,
                   0.0f);
    for (int32_t p = 0; p < 4; ++p)
    {
        const float* point = plan->beam.points[p];
        mvfxFloatWords(at + 8 + 4 * p, point[0], point[1], point[2], 0.0f);
    }
}

// The shape beyond the sphere's numbers, and the delay its arc's age is
// from.
static void ShapeRest(const mvfxEmitterView* view, const mvfxStepTables* tables, uint32_t shape[16])
{
    const mvfxShapeView* s = &view->shape;
    mvfxFloatWords(shape, s->length, s->halfExtents[0], s->halfExtents[1], s->halfExtents[2]);
    mvfxFloatWords(shape + 4, s->arc, s->arcSpeed, s->tangent, 0.0f);
    uint64_t delay = (uint64_t)view->delayTicks;
    shape[8] = s->arcMode;
    shape[9] = s->coneVolume ? 1u : 0u;
    shape[10] = (uint32_t)delay;
    shape[11] = (uint32_t)(delay >> 32u);
    shape[12] = tables->mesh;
    shape[13] = s->meshMode;
}

// The units: a step across and down as the core's UnitStep takes it before
// the instance's turn and scale, the angular drag the unit pass turns its
// points by a substep, the pins' arrays and kinds, the air's drag, and how
// they tear (the strain, the torn edges' array).
static void Units(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, uint32_t units[33])
{
    const mvfxUnitDef* u = &view->unit;
    units[0] = (uint32_t)(view->unitPoints > 1 ? view->unitPoints : 1);
    units[1] = (uint32_t)(view->unitWidth > 1 ? view->unitWidth : 1);
    units[2] = (uint32_t)(u->substeps > 1 ? u->substeps : 1);
    // The unit pass's workgroups, a unit each, at most a dispatch's.
    int64_t unitCount = (int64_t)plan->capacity / (view->unitPoints > 1 ? view->unitPoints : 1);
    units[3] = (uint32_t)(unitCount < 65535 ? (unitCount > 0 ? unitCount : 1) : 65535);
    mvfxFloatWords(units + 4, u->direction[0] * u->spacing, u->direction[1] * u->spacing,
                   u->direction[2] * u->spacing, view->angularDrag);
    mvfxFloatWords(units + 8, u->downDirection[0] * u->spacing, u->downDirection[1] * u->spacing,
                   u->downDirection[2] * u->spacing, 0.0f);
    mvfxFloatWords(units + 12, view->unitRests[0], view->unitRests[1], view->unitRests[2],
                   view->unitRests[3]);
    mvfxFloatWords(units + 16, u->stretchCompliance, u->shearCompliance, u->bendCompliance,
                   u->damping);
    const mvfxAttributeArrays* a = &view->arrays;
    units[20] = mvfxWordOf(a->anchor);
    units[21] = mvfxWordOf(a->attach);
    units[22] = mvfxWordOf(a->attachRotation);
    units[23] = mvfxWordOf(a->attachFlags);
    units[24] =
        (u->pinFirst ? MVFX_UNIT_PIN_FIRST : 0u) | (u->pinFirstRow ? MVFX_UNIT_PIN_ROW : 0u);
    units[25] = (uint32_t)u->attach;
    units[26] = mvfxWordOf(a->attachSource);
    // The air blows sheets alone, as the core's mvfxBlowSheets.
    bool sheet = u->kind == mvfx_unitSheet;
    mvfxFloatWords(units + 28, sheet ? u->normalDrag : 0.0f, sheet ? u->tangentDrag : 0.0f,
                   u->tearStrain, 0.0f);
    units[32] = mvfxWordOf(a->torn);
}

// A particle liquid: its substeps and iterations (liquid.wgsl's); a
// liquid gas's particles moved by the gas's velocity, by their affine
// matrices' first word (sim/step.wgsl's, zero at birth; NONE for others).
static void Liquid(const mvfxEmitterView* view, uint32_t liquid[4])
{
    bool particles = view->liquid.enabled && view->liquid.solver == mvfx_liquidParticles;
    bool grid = view->liquid.enabled && view->liquid.solver == mvfx_liquidGrid;
    liquid[0] = particles ? 1u : 0u;
    liquid[1] = particles ? (uint32_t)view->liquid.substeps : 0u;
    liquid[2] = particles ? (uint32_t)view->liquid.iterations : 0u;
    liquid[3] = grid ? mvfxWordOf(view->arrays.affine) : 0xFFFFFFFFu;
}

void mvfxStepNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, int32_t emitter,
                     const uint32_t (*forceGrids)[5], const uint32_t (*colliderGrids)[5],
                     bool normals, const mvfxStepTables* tables, uint32_t words[MVFX_STEP_WORDS])
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
                                 plan->ribbonCounter};
    memcpy(words, counts, sizeof counts);
    // The anchor's shift (none for an emitter in the instance's space,
    // whose anchor stays).
    const double* before = plan->anchorBefore;
    const double* after = plan->anchorAfter;
    mvfxFloatWords(words + 12, (float)(before[0] - after[0]), (float)(before[1] - after[1]),
                   (float)(before[2] - after[2]), view->inheritVelocity);
    float sharedDrag = SharedDrag(view, plan);
    float factors[3];
    DragFactors((double)sharedDrag, (double)plan->ticks / (double)MVFX_TICKS_PER_SECOND, factors);
    mvfxFloatWords(words + 16, factors[0], factors[1], factors[2], view->drag);
    float gravity[3];
    Gravity(view, &plan->from, gravity);
    // Its size as the core's rigid solve takes it, in binary32.
    float size = sqrtf(gravity[0] * gravity[0] + gravity[1] * gravity[1] + gravity[2] * gravity[2]);
    mvfxFloatWords(words + 20, gravity[0], gravity[1], gravity[2], size);
    const float* m = view->shape.matrix;
    for (int32_t r = 0; r < 3; ++r)
    {
        mvfxFloatWords(words + 24 + 4 * r, m[3 * r], m[3 * r + 1], m[3 * r + 2], 0.0f);
    }
    const mvfxShapeView* s = &view->shape;
    mvfxFloatWords(words + 36, s->offset[0], s->offset[1], s->offset[2], s->radius);
    mvfxFloatWords(words + 40, s->thickness, 0.0f, 0.0f, 0.0f);
    const mvfxVelocityView* v = &view->velocity;
    mvfxFloatWords(words + 44, v->direction[0], v->direction[1], v->direction[2], v->coneCosine);
    mvfxFloatWords(words + 48, v->tangentA[0], v->tangentA[1], v->tangentA[2], v->randomize);
    mvfxFloatWords(words + 52, v->tangentB[0], v->tangentB[1], v->tangentB[2], v->speedMin);
    mvfxFloatWords(words + 56, v->speedMax, 0.0f, 0.0f, 0.0f);
    Place(words + 60, &plan->from, after);
    Place(words + 68, &plan->to, after);
    mvfxFloatWords(words + 76, plan->velocity[0], plan->velocity[1], plan->velocity[2], 0.0f);
    mvfxForceWords(plan->forces, plan->forceCount, sharedDrag, forceGrids, words + 80);
    Colliders(plan, colliderGrids, normals, words + 84 + 132 * MVFX_MAX_FORCES);
    uint32_t* at = words + MVFX_VALUES_FIRST;
    Words(&view->arrays, at);
    memcpy(at + 32, plan->parameters, sizeof plan->parameters);
    double seconds = (double)plan->ticks / (double)MVFX_TICKS_PER_SECOND;
    float angular[3];
    DragFactors((double)view->angularDrag, seconds, angular);
    float limit[3];
    DragFactors((double)view->modules.limitDampen, seconds, limit);
    mvfxFloatWords(at + 96, angular[0], angular[1], limit[0], 0.0f);
    Modules(&plan->modules, at + 100);
    Beam(view, plan, at + 116);
    KillRules(plan, words + MVFX_KILLS_FIRST);
    // The events the GPU writes (every kind the emitter writes), how many,
    // and where in the events buffer.
    words[MVFX_KILLS_FIRST + 1] = view->events;
    words[MVFX_KILLS_FIRST + 2] = (uint32_t)view->eventCapacity;
    words[MVFX_KILLS_FIRST + 3] = mvfxEventFirst(plan->capacity);
    Sources(view, tables, words + MVFX_SOURCES_FIRST);
    ShapeRest(view, tables, words + MVFX_SHAPE_FIRST);
    uint32_t* ribbon = words + MVFX_RIBBON_FIRST;
    ribbon[0] = view->ribbon.ids;
    memcpy(&ribbon[1], &view->ribbon.breakDistance, sizeof(float));
    Rigid(view, tables, words + MVFX_RIGID_FIRST);
    Units(view, plan, words + MVFX_UNITS_FIRST);
    Liquid(view, words + MVFX_LIQUID_HEAD);
}
