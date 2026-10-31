// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curves and gradients as an effect states them: keys the host authors,
// which the library bakes when it creates the effect.

#ifndef MAUL_VFX_CURVE_H
#define MAUL_VFX_CURVE_H

#include "maul-vfx/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The most keys a curve or either list of a gradient holds.
#define MVFX_CURVE_MAX_KEYS 64

// The samples a baked curve or gradient holds over its domain.
#define MVFX_CURVE_SAMPLES 256

// The largest a colour channel may be, in size, as any output shows it:
// a particle's stored colour (a source's draw, a host's or an event's
// colour) times its colour over life. Every gradient's keys are within it
// too. HDR colours keep room to spare, and a light's intensity or a
// ribbon's gradient times it stays finite.
#define MVFX_MAX_COLOR 1e6f

    // How a curve runs from a key to the next.
    typedef int32_t mvfxInterpolation;
    enum
    {
        // The key's value until the next key.
        mvfx_interpolationConstant = 0,
        // A straight line to the next key.
        mvfx_interpolationLinear = 1,
        // A cubic Hermite segment with the keys' tangents.
        mvfx_interpolationCubic = 2,
    };

    // Where a curve's cubic segments take their tangents from.
    typedef int32_t mvfxTangents;
    enum
    {
        // The keys' own inTangent and outTangent.
        mvfx_tangentsGiven = 0,
        // Each key's tangent from its neighbors (the slope between the keys
        // either side of it; a one-sided slope at the ends).
        mvfx_tangentsSmooth = 1,
        // Fritsch and Carlson's tangents: a segment between two keys never
        // leaves the range of their values.
        mvfx_tangentsMonotone = 2,
    };

    // A key of a curve: where it is, its value there, its tangents in value
    // per unit of time, and how the segment that starts at it runs.
    typedef struct mvfxCurveKey
    {
        float time;
        float value;
        float inTangent;
        float outTangent;
        mvfxInterpolation interpolation;
    } mvfxCurveKey;

    // A curve: at least one key, in times that never decrease (two keys at
    // one time make a step), finite in every field. Before its first key and
    // after its last it holds their values.
    typedef struct mvfxCurve
    {
        const mvfxCurveKey* keys;
        int32_t keyCount;
        mvfxTangents tangents;
    } mvfxCurve;

    // A color key of a gradient: where it is, and a linear RGB color.
    typedef struct mvfxColorKey
    {
        float time;
        float red;
        float green;
        float blue;
    } mvfxColorKey;

    // An alpha key of a gradient.
    typedef struct mvfxAlphaKey
    {
        float time;
        float alpha;
    } mvfxAlphaKey;

    // A gradient over [0, 1]: color keys and alpha keys apart, each list at
    // least one key, in times from 0 to 1 that never decrease, each channel
    // at most MVFX_MAX_COLOR in size, interpolated linearly in linear RGB.
    typedef struct mvfxGradient
    {
        const mvfxColorKey* colorKeys;
        int32_t colorKeyCount;
        const mvfxAlphaKey* alphaKeys;
        int32_t alphaKeyCount;
    } mvfxGradient;

#ifdef __cplusplus
}
#endif

#endif
