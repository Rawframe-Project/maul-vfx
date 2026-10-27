// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The checks and compiled forms of an emitter's parts beside its values:
// neighbors, steering, contacts, its pair force, plexus links and units.

#include "effect_parts.h"

#include "direction.h"
#include "life.h"

#include <math.h>

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

// Whether a steer point is valid: a weight, a point or parameter, a
// space and a radius in range.
static bool IsSteerPointValid(const mvfxSteerPoint* point, int32_t parameterCount)
{
    return IsWithin(point->weight, 0.0f, MVFX_MAX_EXTENT) && point->space <= mvfx_spaceLocal &&
           IsWithin(point->radius, 0.0f, MVFX_MAX_EXTENT) &&
           IsWithin(point->point[0], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           IsWithin(point->point[1], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           IsWithin(point->point[2], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           (!point->fromParameter || (point->parameter >= 0 && point->parameter < parameterCount));
}

// Whether a steering path is valid: none (weight 0), or its points, space,
// radius and lookahead in range.
static bool IsSteerPathValid(const mvfxSteerPath* path)
{
    if (!IsWithin(path->weight, 0.0f, MVFX_MAX_EXTENT))
    {
        return false;
    }
    if (path->weight == 0.0f)
    {
        return true;
    }
    if (path->points == nullptr || path->pointCount < 2 ||
        path->pointCount > MVFX_MAX_PATH_POINTS || path->space > mvfx_spaceLocal ||
        !IsWithin(path->radius, 0.0f, MVFX_MAX_EXTENT) || !IsWithin(path->lookahead, 0.0f, 10.0f))
    {
        return false;
    }
    for (int32_t i = 0; i < 3 * path->pointCount; ++i)
    {
        if (!IsWithin(path->points[i], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT))
        {
            return false;
        }
    }
    return true;
}

// Whether a flocking rule is valid: no weight, or a weight with neighbors
// and a radius within theirs.
static bool IsFlockRuleValid(const mvfxEmitterDef* def, float weight, float radius)
{
    return IsWithin(weight, 0.0f, MVFX_MAX_EXTENT) &&
           (weight == 0.0f ||
            (def->neighbors.enabled && radius > 0.0f && radius <= def->neighbors.radius));
}

// Whether an emitter's pair force is valid: a radius within its
// neighbors', a strength in range.
static bool IsPairForceValid(const mvfxEmitterDef* def)
{
    const mvfxPairForceDef* pair = &def->pairForce;
    return !pair->enabled ||
           (def->neighbors.enabled && pair->radius > 0.0f &&
            pair->radius <= def->neighbors.radius && mvfxIsLifeCurveValid(&pair->strength) &&
            IsWithin(pair->strength.value, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
}

// Whether a direction is in range and of a length that normalizes; out
// the direction normalized.
static bool IsDirectionValid(const float v[3], float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = v[k];
    }
    return IsWithin(v[0], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           IsWithin(v[1], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           IsWithin(v[2], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) && mvfxNormalize(out);
}

// Whether a unit's shape is valid: a chain's points, or a sheet's sides
// and two directions at least 3 degrees apart (the cosine between them
// at most cos 3 degrees either way).
static bool IsUnitShapeValid(const mvfxUnitDef* unit)
{
    float across[3];
    if (!IsDirectionValid(unit->direction, across) || !(unit->spacing > 0.0f) ||
        !(unit->spacing <= MVFX_MAX_EXTENT))
    {
        return false;
    }
    if (unit->kind == mvfx_unitChain)
    {
        return unit->points >= 2 && unit->points <= MVFX_MAX_CHAIN_POINTS;
    }
    float down[3];
    bool sides = unit->points >= 2 && unit->points <= MVFX_MAX_SHEET_SIDE && unit->rows >= 2 &&
                 unit->rows <= MVFX_MAX_SHEET_SIDE;
    if (unit->kind != mvfx_unitSheet || !sides || !IsDirectionValid(unit->downDirection, down))
    {
        return false;
    }
    float cosine = across[0] * down[0] + across[1] * down[1] + across[2] * down[2];
    return fabsf(cosine) <= 0.99863f && IsWithin(unit->shearCompliance, 0.0f, MVFX_MAX_EXTENT) &&
           IsWithin(unit->normalDrag, 0.0f, MVFX_MAX_EXTENT) &&
           IsWithin(unit->tangentDrag, 0.0f, MVFX_MAX_EXTENT);
}

// Whether an emitter's units are valid: a particle, or a chain or sheet in
// range whose capacities hold whole units, not on a beam nor (a sheet)
// drawn as ribbons.
static bool IsUnitValid(const mvfxEmitterDef* def)
{
    const mvfxUnitDef* unit = &def->unit;
    if (unit->kind == mvfx_unitParticle)
    {
        return true;
    }
    bool shape = IsUnitShapeValid(unit);
    bool solve = unit->substeps >= 1 && unit->substeps <= MVFX_MAX_SUBSTEPS &&
                 IsWithin(unit->stretchCompliance, 0.0f, MVFX_MAX_EXTENT) &&
                 IsWithin(unit->bendCompliance, 0.0f, MVFX_MAX_EXTENT) &&
                 IsWithin(unit->damping, 0.0f, MVFX_MAX_DRAG) &&
                 IsWithin(unit->tearStrain, 0.0f, MVFX_MAX_EXTENT) &&
                 IsWithin(unit->selfThickness, 0.0f, MVFX_MAX_EXTENT) &&
                 (unit->selfThickness == 0.0f ||
                  (IsWithin(unit->selfFriction, 0.0f, 1.0f) && unit->selfContacts >= 1 &&
                   unit->selfContacts <= MVFX_MAX_NEIGHBORS));
    bool sheet = unit->kind == mvfx_unitSheet;
    // Counted only within the shape's bounds, where the product fits.
    int32_t points = !shape ? 1 : sheet ? unit->points * unit->rows : unit->points;
    int32_t units = shape ? def->capacity / points : 0;
    bool whole = shape && def->capacity % points == 0 && def->hostSpawnCapacity <= units &&
                 (!def->eventSpawn.enabled || def->eventSpawn.capacity <= units);
    bool drawn = !def->beam.enabled && !(sheet && def->ribbon.ids != mvfx_ribbonNone);
    return shape && solve && whole && drawn;
}

// The length of a + sign b.
static float LengthOf(const float a[3], const float b[3], float sign)
{
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = a[k] + sign * b[k];
        square += d * d;
    }
    return sqrtf(square);
}

// Compiles an emitter's units: the directions normalized and what its
// kind does not read cleared; its points, width and rest lengths.
static void CompileUnit(const mvfxUnitDef* unit, mvfxCompiledEmitter* out)
{
    out->unitPoints = 1;
    out->unitWidth = 1;
    if (unit->kind == mvfx_unitParticle)
    {
        // A particle reads nothing of it.
        out->unit = (mvfxUnitDef){0};
        return;
    }
    out->unit = *unit;
    mvfxUnitDef* u = &out->unit;
    (void)mvfxNormalize(u->direction);
    out->unitWidth = unit->points;
    out->unitPoints = unit->points;
    out->unitRests[0] = unit->spacing;
    if (unit->kind == mvfx_unitChain)
    {
        u->rows = 1;
        u->shearCompliance = 0.0f;
        u->normalDrag = 0.0f;
        u->tangentDrag = 0.0f;
        u->pinFirstRow = false;
        for (int32_t k = 0; k < 3; ++k)
        {
            u->downDirection[k] = 0.0f;
        }
        return;
    }
    (void)mvfxNormalize(u->downDirection);
    out->unitPoints = unit->points * unit->rows;
    out->unitRests[1] = unit->spacing;
    out->unitRests[2] = unit->spacing * LengthOf(u->direction, u->downDirection, 1.0f);
    out->unitRests[3] = unit->spacing * LengthOf(u->direction, u->downDirection, -1.0f);
}

// Whether emitter i's units' attachment is valid: none; or with a pinned
// point, the instance for a world-space emitter, the host's frames with
// room for some, or the particles of an event source in the same space.
static bool IsAttachValid(const mvfxEffectDef* def, int32_t i)
{
    const mvfxEmitterDef* emitter = &def->emitters[i];
    const mvfxUnitDef* unit = &emitter->unit;
    if (unit->kind == mvfx_unitParticle || unit->attach == mvfx_attachNone)
    {
        return true;
    }
    bool pinned = unit->pinFirst || (unit->kind == mvfx_unitSheet && unit->pinFirstRow);
    switch (unit->attach)
    {
    case mvfx_attachInstance:
        return pinned && emitter->space == mvfx_spaceWorld;
    case mvfx_attachHost:
        return pinned && unit->attachFrameCapacity >= 1 &&
               unit->attachFrameCapacity <= MVFX_MAX_ATTACH_FRAMES;
    case mvfx_attachParticles:
        // The source is in range: the event spawn's own check passed.
        return pinned && emitter->eventSpawn.enabled &&
               def->emitters[emitter->eventSpawn.source].space == emitter->space;
    default:
        return false;
    }
}

static bool IsSameCurve(const mvfxCurve* a, const mvfxCurve* b)
{
    bool same = a->keyCount == b->keyCount && a->tangents == b->tangents;
    for (int32_t i = 0; same && i < a->keyCount; ++i)
    {
        const mvfxCurveKey* x = &a->keys[i];
        const mvfxCurveKey* y = &b->keys[i];
        same = x->time == y->time && x->value == y->value && x->inTangent == y->inTangent &&
               x->outTangent == y->outTangent && x->interpolation == y->interpolation;
    }
    return same;
}

// Whether an emitter's sizes keep the axes its rigid shape does not take
// equal to X (mvfxRigidShape): its size source's ends on them X's (a
// birth takes X's draw for them, mvfxTieSize), and with size over life
// per axis, their curves X's.
static bool IsSizeTied(const mvfxEmitterDef* def)
{
    int32_t axes = mvfxSizeAxes(&def->rigid);
    const mvfxVectorSource* size = &def->size;
    bool drawn = size->kind != mvfx_sourceNone;
    bool tied = true;
    const mvfxSizeOverLife* over = &def->overLife.size;
    for (int32_t k = axes; k < 3; ++k)
    {
        tied = tied && (!drawn || (size->min[k] == size->min[0] && size->max[k] == size->max[0]));
        tied = tied && (!over->enabled || !over->perAxis ||
                        (over->size[k].value == over->size[0].value &&
                         IsSameCurve(&over->size[k].curve, &over->size[0].curve)));
    }
    return tied;
}

// Whether an emitter's rigid particles are valid: a shape, a density,
// substeps, restitution and friction in range, its sizes tied to its
// shape's axes; particles, not units nor a beam.
static bool IsRigidValid(const mvfxEmitterDef* def)
{
    const mvfxRigidDef* rigid = &def->rigid;
    if (!rigid->enabled)
    {
        return true;
    }
    bool body = rigid->shape <= mvfx_rigidHull && rigid->density > 0.0f &&
                rigid->density <= MVFX_MAX_EXTENT && rigid->substeps >= 1 &&
                rigid->substeps <= MVFX_MAX_SUBSTEPS && IsWithin(rigid->restitution, 0.0f, 1.0f) &&
                IsWithin(rigid->dynamicFriction, 0.0f, MVFX_MAX_DRAG) &&
                IsWithin(rigid->staticFriction, rigid->dynamicFriction, MVFX_MAX_DRAG) &&
                rigid->bodyContacts >= 0 && rigid->bodyContacts <= MVFX_MAX_BODY_CONTACTS &&
                IsWithin(rigid->sleepSpeed, 0.0f, MVFX_MAX_EXTENT) &&
                (rigid->sleepSpeed == 0.0f ||
                 (rigid->sleepTime > 0.0f && rigid->sleepTime <= MVFX_MAX_SLEEP_TIME));
    return body && IsSizeTied(def) && def->unit.kind == mvfx_unitParticle && !def->beam.enabled;
}

// Whether an emitter's liquid is valid: each parameter in range, the
// kernel radius within the neighbor radii; particles, not units, rigid
// bodies nor a beam.
static bool IsLiquidValid(const mvfxEmitterDef* def)
{
    const mvfxLiquidDef* liquid = &def->liquid;
    if (!liquid->enabled)
    {
        return true;
    }
    bool kernel = IsWithin(liquid->kernelScale, MVFX_MIN_LIQUID_KERNEL, MVFX_MAX_LIQUID_KERNEL) &&
                  IsWithin(liquid->spacing, MVFX_MIN_NEIGHBOR_RADIUS,
                           MVFX_MAX_NEIGHBOR_RADIUS / liquid->kernelScale);
    bool solve = liquid->substeps >= 1 && liquid->substeps <= MVFX_MAX_SUBSTEPS &&
                 liquid->iterations >= 1 && liquid->iterations <= MVFX_MAX_LIQUID_ITERATIONS &&
                 liquid->relaxation > 0.0f && liquid->relaxation <= 1.0f &&
                 liquid->maxNeighbors >= 1 && liquid->maxNeighbors <= MVFX_MAX_NEIGHBORS;
    bool terms = IsWithin(liquid->tensile, 0.0f, 1.0f) && liquid->tensileDistance > 0.0f &&
                 liquid->tensileDistance < 1.0f && liquid->tensilePower >= 1 &&
                 liquid->tensilePower <= MVFX_MAX_LIQUID_TENSILE_POWER &&
                 IsWithin(liquid->viscosity, 0.0f, 1.0f) &&
                 IsWithin(liquid->vorticity, 0.0f, MVFX_MAX_EXTENT);
    return kernel && solve && terms && liquid->solver <= mvfx_liquidGrid &&
           def->unit.kind == mvfx_unitParticle && !def->rigid.enabled && !def->beam.enabled;
}

// Whether an emitter's plexus links are valid.
static bool IsPlexusValid(const mvfxPlexusDef* plexus)
{
    return !plexus->enabled ||
           (IsWithin(plexus->distance, MVFX_MIN_NEIGHBOR_RADIUS, MVFX_MAX_NEIGHBOR_RADIUS) &&
            plexus->maxLinks >= 1 && plexus->maxLinks <= MVFX_MAX_PLEXUS_LINKS &&
            plexus->maxPerParticle >= 1 && plexus->maxPerParticle <= MVFX_MAX_NEIGHBORS &&
            IsWithin(plexus->width, 0.0f, MVFX_MAX_EXTENT));
}

// Whether an emitter's particle contacts are valid.
static bool AreContactsValid(const mvfxContactDef* contacts)
{
    return !contacts->enabled ||
           (contacts->radiusScale > 0.0f && contacts->radiusScale <= MVFX_MAX_EXTENT &&
            contacts->iterations >= 1 && contacts->iterations <= MVFX_MAX_CONTACT_ITERATIONS &&
            contacts->maxContacts >= 1 && contacts->maxContacts <= MVFX_MAX_NEIGHBORS &&
            IsWithin(contacts->restitution, 0.0f, 1.0f) &&
            IsWithin(contacts->friction, 0.0f, 1.0f));
}

// Whether an emitter's steering is valid.
static bool IsSteeringValid(const mvfxEmitterDef* def, int32_t parameterCount)
{
    const mvfxSteeringDef* s = &def->steering;
    if (!s->enabled)
    {
        return true;
    }
    return IsFlockRuleValid(def, s->separation, s->separationRadius) &&
           IsFlockRuleValid(def, s->alignment, s->alignmentRadius) &&
           IsFlockRuleValid(def, s->cohesion, s->cohesionRadius) &&
           IsWithin(s->viewAngle, 0.0f, 3.14159274f) &&
           IsWithin(s->cruiseSpeed, 0.0f, MVFX_MAX_EXTENT) &&
           IsWithin(s->maxForce, 0.0f, MVFX_MAX_EXTENT) &&
           IsSteerPointValid(&s->seek, parameterCount) &&
           IsSteerPointValid(&s->flee, parameterCount) && IsSteerPathValid(&s->path);
}

// Whether an emitter's neighbor queries are valid against its effect: a
// source of its own or in its space, a radius and a cap in range.
static bool AreNeighborsValid(const mvfxEffectDef* def, int32_t i)
{
    const mvfxNeighborDef* neighbors = &def->emitters[i].neighbors;
    if (!neighbors->enabled)
    {
        return true;
    }
    int32_t source = neighbors->source;
    bool sourceValid = source == -1 || (source >= 0 && source < def->emitterCount &&
                                        def->emitters[source].space == def->emitters[i].space);
    return sourceValid &&
           IsWithin(neighbors->radius, MVFX_MIN_NEIGHBOR_RADIUS, MVFX_MAX_NEIGHBOR_RADIUS) &&
           neighbors->maxNeighbors >= 1 && neighbors->maxNeighbors <= MVFX_MAX_NEIGHBORS;
}

static bool AreEmitterPartsValid(const mvfxEffectDef* def, int32_t i)
{
    const mvfxEmitterDef* emitter = &def->emitters[i];
    return AreNeighborsValid(def, i) && IsSteeringValid(emitter, def->parameterCount) &&
           AreContactsValid(&emitter->contacts) && IsPairForceValid(emitter) &&
           IsPlexusValid(&emitter->plexus) && IsUnitValid(emitter) && IsAttachValid(def, i) &&
           IsRigidValid(emitter) && IsLiquidValid(emitter);
}

// Copies an emitter's steering, its path's points into the compiled
// emitter so nothing of the host's is kept.
static void CopySteering(const mvfxEmitterDef* def, mvfxCompiledEmitter* out)
{
    out->steering = def->steering.enabled ? def->steering : (mvfxSteeringDef){0};
    const mvfxSteerPath* path = &out->steering.path;
    for (int32_t i = 0; path->weight > 0.0f && i < path->pointCount; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out->pathPoints[i][k] = def->steering.path.points[3 * i + k];
        }
    }
    out->steering.path.points = nullptr;
}

static void CompileEmitterParts(const mvfxEmitterDef* def, mvfxCompiledEmitter* out)
{
    CopySteering(def, out);
    out->rigid = def->rigid.enabled ? def->rigid : (mvfxRigidDef){0};
    out->liquid = def->liquid.enabled ? def->liquid : (mvfxLiquidDef){0};
    out->plexus = def->plexus.enabled ? def->plexus : (mvfxPlexusDef){0};
    CompileUnit(&def->unit, out);
    out->contacts = def->contacts.enabled ? def->contacts : (mvfxContactDef){0};
}

// Resolves each emitter's neighbor source to an index, and sizes each
// grid's cells by the largest radius that queries it.
static void ResolveNeighbors(const mvfxEffectDef* def, mvfxEffect* effect)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        effect->emitters[i].gridCell = 0.0f;
        effect->emitters[i].neighbors = (mvfxNeighborDef){0};
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        mvfxNeighborDef neighbors = def->emitters[i].neighbors;
        if (!neighbors.enabled)
        {
            continue;
        }
        neighbors.source = neighbors.source < 0 ? i : neighbors.source;
        effect->emitters[i].neighbors = neighbors;
        float* cell = &effect->emitters[neighbors.source].gridCell;
        *cell = neighbors.radius > *cell ? neighbors.radius : *cell;
    }
}

bool mvfxAreEffectPartsValid(const mvfxEffectDef* def)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        if (!AreEmitterPartsValid(def, i))
        {
            return false;
        }
    }
    return true;
}

void mvfxCompileEffectParts(const mvfxEffectDef* def, mvfxEffect* effect)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        CompileEmitterParts(&def->emitters[i], &effect->emitters[i]);
    }
    ResolveNeighbors(def, effect);
}
