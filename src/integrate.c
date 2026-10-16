// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "integrate.h"

#include "collide.h"
#include "direction.h"
#include "effect.h"
#include "elementary.h"
#include "event.h"
#include "force.h"
#include "gas_api.h"
#include "kill.h"
#include "rigid_wake.h"
#include "space.h"
#include "water_couple.h"

#include <math.h>

// Below this k h, the series: their first terms dropped are below 2^-40
// of the sum.
#define SERIES_LIMIT 0.25

mvfxDragFactors mvfxDragFactorsOf(double drag, double seconds)
{
    double x = drag * seconds;
    double decay = 0.0;
    double phi1 = 0.0;
    double phi2 = 0.0;
    if (x < SERIES_LIMIT)
    {
        // phi_n(x) = sum over j of (-x)^j / (j + n)!, eight terms.
        double term1 = 1.0;
        double term2 = 0.5;
        for (int32_t j = 0; j < 8; ++j)
        {
            phi1 += term1;
            phi2 += term2;
            term1 *= -x / (double)(j + 2);
            term2 *= -x / (double)(j + 3);
        }
        decay = 1.0 - x * phi1;
    }
    else
    {
        decay = mvfxExpDouble(-x);
        phi1 = (1.0 - decay) / x;
        phi2 = (1.0 - phi1) / x;
    }
    return (mvfxDragFactors){
        .decay = (float)decay,
        .carry = (float)(seconds * phi1),
        .push = (float)(seconds * seconds * phi2),
    };
}

// The emitter's gravity in the space its particles are stored in, with
// the instance's transform at the step's start.
void mvfxGravityOf(const mvfxInstance* instance, const mvfxCompiledEmitter* emitter, float out[3])
{
    const mvfxTransform* transform = &instance->from;
    if (emitter->gravitySpace == emitter->space)
    {
        out[0] = emitter->gravity[0];
        out[1] = emitter->gravity[1];
        out[2] = emitter->gravity[2];
        return;
    }
    const float* q = transform->rotation;
    if (emitter->gravitySpace == mvfx_spaceLocal)
    {
        // From the instance's space into the world: scaled, then turned.
        float scaled[3] = {emitter->gravity[0] * transform->scale,
                           emitter->gravity[1] * transform->scale,
                           emitter->gravity[2] * transform->scale};
        mvfxRotate(q, scaled, out);
        return;
    }
    // From the world into the instance's space: turned back, then unscaled.
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float turned[3];
    mvfxRotate(inverse, emitter->gravity, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = turned[k] / transform->scale;
    }
}

// A particle's own drag: the emitter's times its multiplier, held to 0
// and MVFX_MAX_DRAG.
static double ParticleDrag(const mvfxCompiledEmitter* emitter, float scale)
{
    float drag = emitter->drag * scale;
    return !(drag > 0.0f) ? 0.0 : (double)fminf(drag, MVFX_MAX_DRAG);
}

// Moves particle i's position and velocity by the factors.
static void MoveLinear(float* const p[3], float* const v[3], int32_t i, const float a[3],
                       mvfxDragFactors f)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        float v0 = v[k][i];
        p[k][i] += v0 * f.carry + a[k] * f.push;
        v[k][i] = v0 * f.decay + a[k] * f.carry;
    }
}

// Turns a unit quaternion q by the rotation vector theta (axis times
// angle), in the parent's frame: q' = r(theta) q, then normalized.
static void TurnBy(float q[4], const float theta[3])
{
    float angle = sqrtf(theta[0] * theta[0] + theta[1] * theta[1] + theta[2] * theta[2]);
    if (!(angle > 0.0f))
    {
        return;
    }
    float sine = 0.0f;
    float cosine = 1.0f;
    mvfxSinCos(0.5f * angle, &sine, &cosine);
    float s = sine / angle;
    float r[4] = {theta[0] * s, theta[1] * s, theta[2] * s, cosine};
    float out[4] = {
        r[3] * q[0] + r[0] * q[3] + r[1] * q[2] - r[2] * q[1],
        r[3] * q[1] - r[0] * q[2] + r[1] * q[3] + r[2] * q[0],
        r[3] * q[2] + r[0] * q[1] - r[1] * q[0] + r[2] * q[3],
        r[3] * q[3] - r[0] * q[0] - r[1] * q[1] - r[2] * q[2],
    };
    float length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] = out[k] / length;
    }
}

// Moves particle i's angle and spin, and its orientation and angular
// velocity, by the angular factors.
static void MoveAngular(mvfxInstance* instance, int32_t emitter, int32_t i, mvfxDragFactors f,
                        float speedScale)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxAttributePlan* plan = &compiled->plan;
    if (mvfxHasAttribute(plan, mvfx_attributeAngle))
    {
        float* angle = mvfxAttributeArray(instance, emitter, mvfx_attributeAngle);
        float* spin = mvfxAttributeArray(instance, emitter, mvfx_attributeSpin);
        angle[i] += spin[i] * (f.carry * speedScale);
        spin[i] *= f.decay;
    }
    if (mvfxHasAttribute(plan, mvfx_attributeOrientationX))
    {
        float* q[4];
        float* w[3];
        for (int32_t k = 0; k < 4; ++k)
        {
            q[k] = mvfxAttributeArray(instance, emitter,
                                      (mvfxAttribute)(mvfx_attributeOrientationX + k));
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            w[k] =
                mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeAngularX + k));
        }
        float carry = f.carry * speedScale;
        float theta[3] = {w[0][i] * carry, w[1][i] * carry, w[2][i] * carry};
        float turned[4] = {q[0][i], q[1][i], q[2][i], q[3][i]};
        TurnBy(turned, theta);
        for (int32_t k = 0; k < 4; ++k)
        {
            q[k][i] = turned[k];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            w[k][i] *= f.decay;
        }
    }
}

// The drag the winds and velocity fields without a falloff give, summed
// in order as mvfxForcesAt sums it.
static float SharedWind(const mvfxResolvedForce* forces, int32_t count)
{
    float wind = 0.0f;
    for (int32_t f = 0; f < count; ++f)
    {
        if (forces[f].falloff == mvfx_falloffNone)
        {
            wind += 1.0f * mvfxForceDrag(&forces[f]);
        }
    }
    return wind;
}

// How a space maps to the space the emitter's particles are stored in,
// with the instance's transform at the step's start.
static mvfxSpaceMap SpaceMapOf(const mvfxInstance* instance, int32_t emitter,
                               mvfxSimulationSpace space)
{
    return mvfxSpaceMapAt(instance, emitter, space, &instance->from);
}

// Moves particles from first to end over seconds.
// What a range of particles moves by over a span.
typedef struct Range
{
    mvfxInstance* instance;
    int32_t emitter;
    const mvfxCompiledEmitter* compiled;
    float* p[3];
    float* v[3];
    float gravity[3];
    const float* dragScales;
    const mvfxResolvedForce* forces;
    float sharedDrag;
    mvfxDragFactors shared;
    const mvfxResolvedVelocityModules* modules;
    float limitDecay;
    const uint32_t* birthLow;
    const uint32_t* birthHigh;
    const float* inverseLife;
    // The emitter's water surface, NULL for none.
    const mvfxResolvedWater* water;
    // The tick and time the span starts at, and its seconds.
    int64_t tick;
    double time;
    double seconds;
} Range;

// Particle i's normalized age at the span's start, held to [0, 1].
static float AgeOf(const Range* r, int32_t i)
{
    int64_t birth = (int64_t)(((uint64_t)r->birthHigh[i] << 32u) | r->birthLow[i]);
    float age =
        (float)((double)(r->tick - birth) / (double)MVFX_TICKS_PER_SECOND) * r->inverseLife[i];
    return fminf(fmaxf(age, 0.0f), 1.0f);
}

// Particle i's contact plane from the scene, grown by its radius times
// sigma, as a plane collider; false when it has none.
static bool ScenePlaneOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float sigma,
                         mvfxResolvedCollider* out)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    if (!mvfxHasAttribute(&compiled->plan, mvfx_attributeScenePointX))
    {
        return false;
    }
    float radius = compiled->collision.radius * sigma;
    *out = (mvfxResolvedCollider){.kind = mvfx_colliderPlane};
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* point =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeScenePointX + k));
        const float* normal =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeSceneNormalX + k));
        out->normal[k] = normal[i];
        out->center[k] = point[i] + radius * normal[i];
    }
    return out->normal[0] != 0.0f || out->normal[1] != 0.0f || out->normal[2] != 0.0f;
}

mvfxOwnColliders mvfxOwnCollidersOf(const mvfxInstance* instance, int32_t emitter, int32_t i,
                                    float sigma, mvfxResolvedCollider* plane)
{
    const mvfxEmitterState* state = &instance->emitters[emitter];
    return (mvfxOwnColliders){
        .plane = ScenePlaneOf(instance, emitter, i, sigma, plane) ? plane : nullptr,
        .depth = state->hasDepth ? &state->depth : nullptr,
    };
}

// Collides particle i, gone from start to place: its place and velocity
// after its contacts, its count of them, and the life a contact takes;
// whether a contact kills it.
static bool Collide(const Range* r, int32_t i, const float start[3], float place[3])
{
    const mvfxCollision* collision = &r->compiled->collision;
    const mvfxEmitterState* state = &r->instance->emitters[r->emitter];
    float velocity[3] = {r->v[0][i], r->v[1][i], r->v[2][i]};
    float restSpeed = collision->restSpeed * r->modules->sigma;
    mvfxResolvedCollider plane;
    mvfxOwnColliders own =
        mvfxOwnCollidersOf(r->instance, r->emitter, i, r->modules->sigma, &plane);
    mvfxContact first = {0};
    int32_t contacts = mvfxCollide(state->colliders, state->colliderCount, &own, collision,
                                   restSpeed, start, place, velocity, &first);
    if (state->hasDepth && collision->killOutsideView && !mvfxDepthSees(&state->depth, place))
    {
        return true;
    }
    if (contacts == 0)
    {
        return false;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        r->v[k][i] = velocity[k];
    }
    uint32_t* count = mvfxAttributeArray(r->instance, r->emitter, mvfx_attributeCollisions);
    count[i] =
        count[i] > UINT32_MAX - (uint32_t)contacts ? UINT32_MAX : count[i] + (uint32_t)contacts;
    // At the first contact's tick within the span.
    int64_t tick =
        r->tick +
        (int64_t)floor((double)first.t * r->seconds * (double)MVFX_TICKS_PER_SECOND + 0.5);
    mvfxRecordEvent(r->instance, r->emitter,
                    &(mvfxEventFact){.kind = mvfx_eventCollision,
                                     .particle = i,
                                     .tick = tick,
                                     .placeTick = tick,
                                     .point = first.point,
                                     .normal = first.normal});
    if (collision->killOnCollision || collision->lifeLoss >= 1.0f)
    {
        return true;
    }
    if (collision->lifeLoss > 0.0f)
    {
        // Each contact takes its share of the life left: the life shrinks by
        // (1 - loss) a contact, so its inverse grows; past its age, it ends.
        float* inverseLife = mvfxAttributeArray(r->instance, r->emitter, mvfx_attributeInverseLife);
        for (int32_t c = 0; c < contacts; ++c)
        {
            inverseLife[i] /= 1.0f - collision->lifeLoss;
        }
        int64_t birth = (int64_t)(((uint64_t)r->birthHigh[i] << 32u) | r->birthLow[i]);
        double age = (double)(r->tick - birth) / (double)MVFX_TICKS_PER_SECOND + r->seconds;
        return age * (double)inverseLife[i] >= 1.0;
    }
    return false;
}

// Whether a kill rule or an attractor's kill radius kills particle i,
// gone from start to end over the span.
static bool IsKilled(const Range* r, int32_t i, const float start[3], const float end[3])
{
    const mvfxCompiledEmitter* compiled = r->compiled;
    const mvfxEmitterState* state = &r->instance->emitters[r->emitter];
    for (int32_t f = 0; f < compiled->forceCount; ++f)
    {
        const mvfxResolvedForce* force = &r->forces[f];
        if (force->kind == mvfx_forceAttractor && force->killRadius > 0.0f &&
            mvfxSegmentMeetsSphere(start, end, force->point, force->killRadius))
        {
            return true;
        }
    }
    if (compiled->killRuleCount == 0)
    {
        return false;
    }
    const uint32_t* low = r->birthLow;
    const uint32_t* high = r->birthHigh;
    int64_t birth = (int64_t)(((uint64_t)high[i] << 32u) | low[i]);
    double age = (double)(r->tick - birth) / (double)MVFX_TICKS_PER_SECOND + r->seconds;
    const float velocity[3] = {r->v[0][i], r->v[1][i], r->v[2][i]};
    uint32_t collisions = 0;
    if (compiled->collision.enabled)
    {
        collisions = ((const uint32_t*)mvfxAttributeArray(r->instance, r->emitter,
                                                          mvfx_attributeCollisions))[i];
    }
    return mvfxIsKilled(state->killRules, compiled->killRuleCount, start, end, velocity, age,
                        collisions);
}

// Moves particle i: its speed held to the limit, its motion under gravity,
// drag and forces, then the velocity modules' moves.
static void MoveParticle(const Range* r, int32_t i)
{
    float start[3] = {r->p[0][i], r->p[1][i], r->p[2][i]};
    float age = AgeOf(r, i);
    float acceleration[3] = {r->gravity[0], r->gravity[1], r->gravity[2]};
    float wind = 0.0f;
    mvfxForcesAt(r->forces, r->compiled->forceCount, start, r->time, acceleration, &wind);
    if (r->water != nullptr)
    {
        mvfxWaterPull(r->water, start, acceleration, &wind);
    }
    float drag = r->compiled->drag + wind;
    mvfxDragFactors f = r->shared;
    if (r->dragScales != nullptr)
    {
        f = mvfxDragFactorsOf(ParticleDrag(r->compiled, r->dragScales[i]) + (double)wind,
                              r->seconds);
    }
    else if (drag != r->sharedDrag)
    {
        f = mvfxDragFactorsOf((double)drag, r->seconds);
    }
    float velocity[3] = {r->v[0][i], r->v[1][i], r->v[2][i]};
    mvfxLimitSpeed(r->modules, age, r->limitDecay, velocity);
    for (int32_t k = 0; k < 3; ++k)
    {
        r->v[k][i] = velocity[k];
    }
    MoveLinear(r->p, r->v, i, acceleration, f);
    float place[3] = {r->p[0][i], r->p[1][i], r->p[2][i]};
    mvfxMoveByModules(r->modules, age, r->seconds, start, place);
    // Rigid particles meet the colliders at their shapes' surfaces, in their
    // own solve.
    bool collisionKills = r->compiled->collision.enabled && !r->compiled->rigid.enabled &&
                          Collide(r, i, start, place);
    for (int32_t k = 0; k < 3; ++k)
    {
        r->p[k][i] = place[k];
    }
    if (r->water != nullptr)
    {
        mvfxMeetWater(r->instance, r->emitter, i, start, place, r->tick, r->seconds);
    }
    // Kept over a step's substeps: a kill in any of them holds.
    r->instance->killScratch[i] |= collisionKills || IsKilled(r, i, start, place) ? 1 : 0;
}

// Moves particles from first to end over seconds from tick.
void mvfxIntegrateRange(mvfxInstance* instance, int32_t emitter, int32_t first, int32_t end,
                        int64_t tick, double seconds)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    Range r = {
        .instance = instance,
        .emitter = emitter,
        .compiled = compiled,
        .forces = instance->emitters[emitter].forces,
        .modules = &instance->emitters[emitter].velocityModules,
        .birthLow = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthLow),
        .birthHigh = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthHigh),
        .inverseLife = mvfxAttributeArray(instance, emitter, mvfx_attributeInverseLife),
        .water = compiled->water.enabled ? &instance->emitters[emitter].water : nullptr,
        .tick = tick,
        .time = (double)tick / (double)MVFX_TICKS_PER_SECOND,
        .seconds = seconds,
    };
    mvfxGravityOf(instance, compiled, r.gravity);
    for (int32_t k = 0; k < 3; ++k)
    {
        r.p[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        r.v[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    if (mvfxHasAttribute(&compiled->plan, mvfx_attributeDrag))
    {
        r.dragScales = mvfxAttributeArray(instance, emitter, mvfx_attributeDrag);
    }
    // The drag every particle has when its winds weigh as at no falloff:
    // its factors serve each particle whose drag comes to the same.
    r.sharedDrag = compiled->drag + SharedWind(r.forces, compiled->forceCount);
    r.shared = mvfxDragFactorsOf((double)r.sharedDrag, seconds);
    r.limitDecay = mvfxDragFactorsOf((double)compiled->velocityModules.limitDampen, seconds).decay;
    for (int32_t i = first; i < end; ++i)
    {
        MoveParticle(&r, i);
    }
    if (mvfxHasAttribute(&compiled->plan, mvfx_attributeAngle) ||
        mvfxHasAttribute(&compiled->plan, mvfx_attributeOrientationX))
    {
        mvfxDragFactors angular = mvfxDragFactorsOf((double)compiled->angularDrag, seconds);
        // Rotation speed over life scales each particle's turn at its age.
        const mvfxCompiledOverLife* over = &compiled->overLife;
        for (int32_t i = first; i < end; ++i)
        {
            float speedScale =
                over->rotation ? mvfxLifeAt(&over->rotationScale, AgeOf(&r, i)) : 1.0f;
            MoveAngular(instance, emitter, i, angular, speedScale);
        }
    }
}

// Resolves the emitter's velocity modules for the step: their centers,
// axes and directions from the emitter's space to the particles'.
static void ResolveVelocityModules(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledVelocityModules* compiled =
        &instance->effect->emitters[emitter].velocityModules;
    mvfxResolvedVelocityModules* out = &instance->emitters[emitter].velocityModules;
    mvfxSpaceMap map = SpaceMapOf(instance, emitter, mvfx_spaceLocal);
    out->compiled = compiled;
    out->sigma = map.sigma;
    mvfxMapVector(&map, compiled->addedDirection, 1.0f, out->addedDirection);
    mvfxMapVector(&map, compiled->orbitAxis, 1.0f, out->orbitAxis);
    mvfxMapPoint(&map, compiled->orbitCenter, out->orbitCenter);
    mvfxMapPoint(&map, compiled->radialCenter, out->radialCenter);
}

// Whether two maps are the same, value by value.
static bool SameMap(const mvfxSpaceMap* a, const mvfxSpaceMap* b)
{
    bool same = a->sigma == b->sigma;
    for (int32_t k = 0; k < 3; ++k)
    {
        same = same && a->before[k] == b->before[k] && a->after[k] == b->after[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        same = same && a->rotation[k] == b->rotation[k];
    }
    return same;
}

// Whether the depth image the emitter resolved changed since the step
// before: another image set, or its map from the world.
static bool DepthMoved(const mvfxInstance* instance, const mvfxEmitterState* state, bool had,
                       const mvfxSpaceMap* map)
{
    return state->hasDepth != had || (had && (state->depthSerial != instance->depthSerial ||
                                              !SameMap(map, &state->depth.map)));
}

// Resolves the emitter's colliders, then the instance's when it takes
// them, grown by the particles' radius; sleeping bodies near those that
// changed since the step before wake.
static void ResolveColliders(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    int32_t before = state->colliderCount;
    bool had = state->hasDepth;
    const mvfxSpaceMap seen = state->depth.map;
    state->colliderCount = 0;
    if (!compiled->collision.enabled)
    {
        return;
    }
    mvfxSpaceMap local = SpaceMapOf(instance, emitter, mvfx_spaceLocal);
    // Rigid particles meet colliders at their shapes, not grown.
    float radius = compiled->rigid.enabled ? 0.0f : compiled->collision.radius * local.sigma;
    state->hasDepth = compiled->collision.depthImage && instance->hasDepthView;
    if (state->hasDepth)
    {
        state->depth = (mvfxResolvedDepth){
            .view = &instance->depthView,
            .map = SpaceMapOf(instance, emitter, mvfx_spaceWorld),
            .radius = radius,
            .thickness = compiled->collision.depthThickness,
            .samples = compiled->collision.depthSamples,
        };
    }
    int32_t hosts = compiled->collision.hostColliders ? instance->hostColliderCount : 0;
    for (int32_t c = 0; c < compiled->colliderCount + hosts; ++c)
    {
        bool own = c < compiled->colliderCount;
        int32_t host = c - compiled->colliderCount;
        const mvfxCollider* collider =
            own ? &compiled->colliders[c] : &instance->hostColliders[host];
        const struct mvfxGrid* grid =
            own ? compiled->colliderGrids[c] : instance->hostColliderGrids[host];
        mvfxSpaceMap map = SpaceMapOf(instance, emitter, (mvfxSimulationSpace)collider->space);
        mvfxResolvedCollider resolved = mvfxResolveCollider(collider, grid, &map, radius);
        const mvfxResolvedCollider* was = c < before ? &state->colliders[c] : nullptr;
        if (was == nullptr || !mvfxSameCollider(was, &resolved))
        {
            mvfxWakeNear(instance, emitter, was, &resolved);
        }
        state->colliders[state->colliderCount++] = resolved;
    }
    // Those gone still lie past the count.
    for (int32_t c = state->colliderCount; c < before; ++c)
    {
        mvfxWakeNear(instance, emitter, &state->colliders[c], nullptr);
    }
    // What changed in a depth image is not known: all wake.
    if (DepthMoved(instance, state, had, &seen))
    {
        static const float anywhere[3] = {0.0f, 0.0f, 0.0f};
        mvfxWakeWithin(instance, emitter, anywhere, INFINITY);
    }
    state->depthSerial = instance->depthSerial;
}

void mvfxResolveEmitter(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    for (int32_t f = 0; f < compiled->forceCount; ++f)
    {
        const mvfxForce* force = &compiled->forces[f];
        mvfxSpaceMap map = SpaceMapOf(instance, emitter, force->space);
        if (force->kind == mvfx_forceGas)
        {
            // Its space is the gas's box.
            mvfxSpaceMap world = SpaceMapOf(instance, emitter, mvfx_spaceWorld);
            map = mvfxGasToSpace(instance, force->gas, &world);
        }
        state->forces[f] = mvfxResolveForce(force, &map, (const float (*)[4])instance->parameters,
                                            instance->seed, compiled->forceGrids[f]);
        if (force->kind == mvfx_forceGas)
        {
            mvfxGasFollow(instance, force->gas, &state->forces[f]);
        }
    }
    for (int32_t r = 0; r < compiled->killRuleCount; ++r)
    {
        const mvfxKillRule* rule = &compiled->killRules[r];
        mvfxSpaceMap map = SpaceMapOf(instance, emitter, (mvfxSimulationSpace)rule->space);
        state->killRules[r] = mvfxResolveKillRule(rule, &map);
    }
    ResolveVelocityModules(instance, emitter);
    ResolveColliders(instance, emitter);
    mvfxSpaceMap world = SpaceMapOf(instance, emitter, mvfx_spaceWorld);
    mvfxResolveWaterCoupling(instance, emitter, &world);
}

void mvfxIntegrateEmitter(mvfxInstance* instance, int32_t emitter)
{
    double seconds =
        (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND;
    mvfxIntegrateRange(instance, emitter, 0, instance->emitters[emitter].count, instance->stepStart,
                       seconds);
}

void mvfxIntegrateNewborn(mvfxInstance* instance, int32_t emitter, int32_t at, int64_t birthTick)
{
    // Even one born at the step's end, so the kill rules test where it is.
    double seconds = (double)(instance->clock - birthTick) / (double)MVFX_TICKS_PER_SECOND;
    mvfxIntegrateRange(instance, emitter, at, at + 1, birthTick, seconds);
}
