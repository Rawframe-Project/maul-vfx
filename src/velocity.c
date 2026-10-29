// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Initial velocities.

#include "velocity.h"

#include "direction.h"
#include "elementary.h"
#include "refusal.h"

#include <math.h>

#define TWO_PI 6.28318530717958647692f
#define PI     3.14159265358979323846f

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

mvfxResult mvfxCheckVelocity(const mvfxVelocity* velocity)
{
    if (velocity->mode > mvfx_directionFixed || !IsWithin(velocity->coneAngle, 0.0f, PI) ||
        !IsWithin(velocity->randomize, 0.0f, 1.0f) || !(velocity->speed.min >= 0.0f) ||
        !(velocity->speed.min <= velocity->speed.max))
    {
        return mvfx_errorInvalid;
    }
    bool needsDirection =
        velocity->mode == mvfx_directionCone || velocity->mode == mvfx_directionFixed;
    float direction[3] = {velocity->direction[0], velocity->direction[1], velocity->direction[2]};
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxResult within = mvfxBounded(direction[i], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
        if (within != mvfx_success)
        {
            return within;
        }
    }
    if (needsDirection && !mvfxNormalize(direction))
    {
        return mvfx_errorInvalid;
    }
    return mvfxBounded(velocity->speed.max, 0.0f, MVFX_MAX_EXTENT);
}

void mvfxCompileVelocity(const mvfxVelocity* velocity, mvfxCompiledVelocity* out)
{
    *out = (mvfxCompiledVelocity){
        .mode = velocity->mode,
        .direction = {velocity->direction[0], velocity->direction[1], velocity->direction[2]},
        .randomize = velocity->randomize,
        .speed = velocity->speed,
    };
    if (!mvfxNormalize(out->direction))
    {
        out->direction[0] = 0.0f;
        out->direction[1] = 0.0f;
        out->direction[2] = 1.0f;
    }
    float sine = 0.0f;
    mvfxSinCos(velocity->coneAngle, &sine, &out->coneCosine);
    // An orthonormal basis about the axis (Duff, Burgess, Christensen and
    // others, "Building an Orthonormal Basis, Revisited", JCGT 2017).
    const float* n = out->direction;
    float sign = n[2] < 0.0f ? -1.0f : 1.0f;
    float a = -1.0f / (sign + n[2]);
    float b = n[0] * n[1] * a;
    out->tangentA[0] = 1.0f + sign * n[0] * n[0] * a;
    out->tangentA[1] = sign * b;
    out->tangentA[2] = -sign * n[0];
    out->tangentB[0] = b;
    out->tangentB[1] = sign + n[1] * n[1] * a;
    out->tangentB[2] = -n[1];
}

void mvfxDrawVelocity(const mvfxCompiledVelocity* velocity, const mvfxShapeSample* sample,
                      mvfxWords4 words, float out[3])
{
    float direction[3] = {0.0f, 0.0f, 0.0f};
    switch (velocity->mode)
    {
    case mvfx_directionFromCenter:
        direction[0] = sample->local[0];
        direction[1] = sample->local[1];
        direction[2] = sample->local[2];
        if (!mvfxNormalize(direction))
        {
            mvfxRandomDirection(words.x, words.y, direction);
        }
        break;
    case mvfx_directionCone:
    {
        // A uniform cosine over the cap, about the axis.
        float z = 1.0f - mvfxUnitFloat(words.x) * (1.0f - velocity->coneCosine);
        float across = sqrtf(fmaxf(0.0f, 1.0f - z * z));
        float sine = 0.0f;
        float cosine = 0.0f;
        mvfxSinCos(TWO_PI * mvfxUnitFloat(words.y), &sine, &cosine);
        for (int32_t i = 0; i < 3; ++i)
        {
            direction[i] = across * cosine * velocity->tangentA[i] +
                           across * sine * velocity->tangentB[i] + z * velocity->direction[i];
        }
        break;
    }
    case mvfx_directionFixed:
        direction[0] = velocity->direction[0];
        direction[1] = velocity->direction[1];
        direction[2] = velocity->direction[2];
        break;
    default:
        direction[0] = sample->normal[0];
        direction[1] = sample->normal[1];
        direction[2] = sample->normal[2];
        break;
    }
    if (velocity->randomize > 0.0f)
    {
        float random[3];
        mvfxRandomDirection(words.z, words.w, random);
        for (int32_t i = 0; i < 3; ++i)
        {
            direction[i] += (random[i] - direction[i]) * velocity->randomize;
        }
        if (!mvfxNormalize(direction))
        {
            direction[0] = random[0];
            direction[1] = random[1];
            direction[2] = random[2];
        }
    }
    // The speed from the low byte of every word, apart from the bits the
    // direction's draws read.
    uint32_t speedWord = ((words.x & 0xFFu) << 24u) | ((words.y & 0xFFu) << 16u) |
                         ((words.z & 0xFFu) << 8u) | (words.w & 0xFFu);
    float speed = velocity->speed.min +
                  (velocity->speed.max - velocity->speed.min) * mvfxUnitFloat(speedWord);
    for (int32_t i = 0; i < 3; ++i)
    {
        out[i] = direction[i] * speed;
    }
}
