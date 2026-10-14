// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The checks and compiled forms of an emitter's parts beside its values:
// neighbors, steering, contacts, its pair force, plexus links and units.

#include "effect_parts.h"

#include "direction.h"
#include "life.h"

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

// Whether an emitter's units are valid: a particle, or a chain in range
// whose capacities hold whole units.
static bool IsUnitValid(const mvfxEmitterDef* def)
{
    const mvfxUnitDef* unit = &def->unit;
    if (unit->kind == mvfx_unitParticle)
    {
        return true;
    }
    float direction[3] = {unit->direction[0], unit->direction[1], unit->direction[2]};
    bool direct = IsWithin(direction[0], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
                  IsWithin(direction[1], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
                  IsWithin(direction[2], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
                  mvfxNormalize(direction);
    bool shape = unit->kind == mvfx_unitChain && unit->points >= 2 &&
                 unit->points <= MVFX_MAX_CHAIN_POINTS && unit->spacing > 0.0f &&
                 unit->spacing <= MVFX_MAX_EXTENT && direct;
    bool solve = unit->substeps >= 1 && unit->substeps <= MVFX_MAX_SUBSTEPS &&
                 IsWithin(unit->stretchCompliance, 0.0f, MVFX_MAX_EXTENT) &&
                 IsWithin(unit->bendCompliance, 0.0f, MVFX_MAX_EXTENT) &&
                 IsWithin(unit->damping, 0.0f, MVFX_MAX_DRAG);
    int32_t units = shape ? def->capacity / unit->points : 0;
    bool whole = shape && def->capacity % unit->points == 0 && def->hostSpawnCapacity <= units &&
                 (!def->eventSpawn.enabled || def->eventSpawn.capacity <= units);
    return shape && solve && whole && !def->beam.enabled;
}

// Compiles an emitter's units, the direction normalized.
static void CompileUnit(const mvfxUnitDef* unit, mvfxCompiledEmitter* out)
{
    out->unit = *unit;
    out->unitPoints = 1;
    if (unit->kind == mvfx_unitChain)
    {
        out->unitPoints = unit->points;
        (void)mvfxNormalize(out->unit.direction);
    }
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
           IsPlexusValid(&emitter->plexus) && IsUnitValid(emitter);
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
