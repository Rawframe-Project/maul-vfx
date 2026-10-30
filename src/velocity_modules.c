// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "velocity_modules.h"

#include "direction.h"
#include "elementary.h"
#include "refusal.h"

#include "maul-vfx/effect.h"

#include <math.h>

// A direction's status: within MVFX_MAX_EXTENT, and of length 0 only when
// it is not used; naming the field of a tag below at.
static mvfxResult CheckDirection(const mvfxFieldPath* at, uint32_t tag, const float v[3], bool used)
{
    float copy[3] = {v[0], v[1], v[2]};
    mvfxResult within = mvfxRefuseVector(at, tag, v, MVFX_MAX_EXTENT);
    return within != mvfx_success
               ? within
               : mvfxRefuseField(at, tag, mvfxRequire(!used || mvfxNormalize(copy)));
}

static bool IsUsed(const mvfxLifeCurve* life)
{
    return life->value != 0.0f;
}

mvfxResult mvfxCheckVelocityModules(const mvfxFieldPath* at, const mvfxVelocityModules* m)
{
    if (m->limitSpeed && !(m->speedLimit.value >= 0.0f))
    {
        mvfxFieldPath limit = mvfxFieldIn(at, MVFX_TAG_VELOCITY_MODULES_SPEED_LIMIT, -1);
        return mvfxRefuseField(&limit, MVFX_TAG_LIFE_CURVE_VALUE, mvfx_errorInvalid);
    }
    if (m->limitSpeed && !(m->limitDampen > 0.0f))
    {
        return mvfxRefuseField(at, MVFX_TAG_VELOCITY_MODULES_LIMIT_DAMPEN, mvfx_errorInvalid);
    }
    const mvfxLifeCurve* lives[5] = {&m->motionScale, &m->addedSpeed, &m->orbitRate,
                                     &m->radialSpeed, &m->speedLimit};
    const uint32_t tags[5] = {
        MVFX_TAG_VELOCITY_MODULES_MOTION_SCALE, MVFX_TAG_VELOCITY_MODULES_ADDED_SPEED,
        MVFX_TAG_VELOCITY_MODULES_ORBIT_RATE,   MVFX_TAG_VELOCITY_MODULES_RADIAL_SPEED,
        MVFX_TAG_VELOCITY_MODULES_SPEED_LIMIT,
    };
    for (int32_t i = 0; i < 5; ++i)
    {
        mvfxFieldPath life = mvfxFieldIn(at, tags[i], -1);
        mvfxResult checked = mvfxCheckLifeCurve(&life, lives[i]);
        if (checked != mvfx_success)
        {
            return checked;
        }
    }
    mvfxResult checked = CheckDirection(at, MVFX_TAG_VELOCITY_MODULES_ADDED_DIRECTION,
                                        m->addedDirection, IsUsed(&m->addedSpeed));
    checked = checked != mvfx_success ? checked
                                      : CheckDirection(at, MVFX_TAG_VELOCITY_MODULES_ORBIT_AXIS,
                                                       m->orbitAxis, IsUsed(&m->orbitRate));
    checked = checked != mvfx_success ? checked
                                      : mvfxRefuseVector(at, MVFX_TAG_VELOCITY_MODULES_ORBIT_CENTER,
                                                         m->orbitCenter, MVFX_MAX_EXTENT);
    checked = checked != mvfx_success
                  ? checked
                  : mvfxRefuseVector(at, MVFX_TAG_VELOCITY_MODULES_RADIAL_CENTER, m->radialCenter,
                                     MVFX_MAX_EXTENT);
    return checked != mvfx_success || !m->limitSpeed
               ? checked
               : mvfxRefuseField(at, MVFX_TAG_VELOCITY_MODULES_LIMIT_DAMPEN,
                                 mvfxBounded(m->limitDampen, 0.0f, MVFX_MAX_DRAG));
}

int32_t mvfxVelocityModuleCurves(const mvfxVelocityModules* m)
{
    return mvfxLifeCurves(&m->motionScale) + mvfxLifeCurves(&m->addedSpeed) +
           mvfxLifeCurves(&m->orbitRate) + mvfxLifeCurves(&m->radialSpeed) +
           mvfxLifeCurves(&m->speedLimit);
}

void mvfxCompileVelocityModules(const mvfxVelocityModules* m, mvfxTableSpace* space,
                                mvfxCompiledVelocityModules* out)
{
    mvfxCompileLife(&m->motionScale, space, &out->motionScale);
    mvfxCompileLife(&m->addedSpeed, space, &out->addedSpeed);
    mvfxCompileLife(&m->orbitRate, space, &out->orbitRate);
    mvfxCompileLife(&m->radialSpeed, space, &out->radialSpeed);
    mvfxCompileLife(&m->speedLimit, space, &out->speedLimit);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->addedDirection[k] = m->addedDirection[k];
        out->orbitAxis[k] = m->orbitAxis[k];
        out->orbitCenter[k] = m->orbitCenter[k];
        out->radialCenter[k] = m->radialCenter[k];
    }
    (void)mvfxNormalize(out->addedDirection);
    (void)mvfxNormalize(out->orbitAxis);
    out->limitSpeed = m->limitSpeed;
    // Checked only for a limit in use: without one it stays out of every
    // product.
    out->limitDampen = m->limitSpeed ? m->limitDampen : 0.0f;
    bool scaled = !(m->motionScale.value == 1.0f && m->motionScale.curve.keyCount == 0);
    out->moves =
        scaled || IsUsed(&m->addedSpeed) || IsUsed(&m->orbitRate) || IsUsed(&m->radialSpeed);
}

void mvfxLimitSpeed(const mvfxResolvedVelocityModules* modules, float age, float decay,
                    float velocity[3])
{
    const mvfxCompiledVelocityModules* m = modules->compiled;
    if (!m->limitSpeed)
    {
        return;
    }
    float limit = fmaxf(mvfxLifeAt(&m->speedLimit, age), 0.0f) * modules->sigma;
    float speed =
        sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    if (!(speed > limit))
    {
        return;
    }
    float scale = (limit + (speed - limit) * decay) / speed;
    for (int32_t k = 0; k < 3; ++k)
    {
        velocity[k] *= scale;
    }
}

// Turns place about the axis through center by angle radians.
static void Orbit(const float axis[3], const float center[3], float angle, float place[3])
{
    float sine = 0.0f;
    float cosine = 1.0f;
    mvfxSinCos(0.5f * angle, &sine, &cosine);
    const float q[4] = {axis[0] * sine, axis[1] * sine, axis[2] * sine, cosine};
    float r[3] = {place[0] - center[0], place[1] - center[1], place[2] - center[2]};
    float turned[3];
    mvfxRotate(q, r, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        place[k] = center[k] + turned[k];
    }
}

void mvfxMoveByModules(const mvfxResolvedVelocityModules* modules, float age, double seconds,
                       const float start[3], float place[3])
{
    const mvfxCompiledVelocityModules* m = modules->compiled;
    if (!m->moves)
    {
        return;
    }
    float h = (float)seconds;
    float scale = mvfxLifeAt(&m->motionScale, age);
    float added = mvfxLifeAt(&m->addedSpeed, age) * modules->sigma * h;
    float radial = mvfxLifeAt(&m->radialSpeed, age) * modules->sigma * h;
    float out[3] = {start[0] - modules->radialCenter[0], start[1] - modules->radialCenter[1],
                    start[2] - modules->radialCenter[2]};
    bool away = mvfxNormalize(out);
    for (int32_t k = 0; k < 3; ++k)
    {
        place[k] = start[k] + scale * (place[k] - start[k]) + added * modules->addedDirection[k] +
                   (away ? radial * out[k] : 0.0f);
    }
    float angle = mvfxLifeAt(&m->orbitRate, age) * h;
    if (angle != 0.0f)
    {
        Orbit(modules->orbitAxis, modules->orbitCenter, angle, place);
    }
}
