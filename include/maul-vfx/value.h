// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Value sources: how a particle's initial values are drawn. Every source
// is a base times a uniform draw between min and max: the base is 1, a
// curve or a gradient over the emitter's age, a gradient at a random
// point, or a parameter of the instance.

#ifndef MAUL_VFX_VALUE_H
#define MAUL_VFX_VALUE_H

#include "maul-vfx/curve.h"
#include "maul-vfx/modules.h"
#include "maul-vfx/shape.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most parameters an effect declares.
#define MVFX_MAX_PARAMETERS 16

// The largest a source's bound may be.
#define MVFX_MAX_VALUE 1e30f

// The largest size a source may draw. A source's base at its largest size
// (a curve's largest baked value, a gradient's largest key, a parameter's
// value; 1 for a range) times the larger size of its ends must be within
// its cap: what the value's role allows it to store, its role's limit over
// the most its values over life scale it (a size's MVFX_MAX_EXTENT, a
// colour's MVFX_MAX_COLOR, a spin's or an angular velocity's
// MVFX_MAX_SPIN, a custom attribute's MVFX_MAX_VALUE), and this for an
// angle, an orientation, a drag scale or a lifetime. An effect with a
// source past its cap is refused, and so is a parameter value, as a
// default or set, that takes a source reading it past its cap. Every value
// a source draws, and every value a particle shows, is then finite.
#define MVFX_MAX_DRAW 1e38f

// The fastest a sprite's spin or a mesh particle's angular velocity may
// turn, in radians a second along each axis: its stored value times its
// rotation speed over life. A step's turn is then at most this times the
// longest step, and as a spin only decays and a life is at most
// MVFX_MAX_LIFETIME, a sprite's angle stays within 1e12 radians of its
// initial one.
#define MVFX_MAX_SPIN 1e6f

    // What a source's base is. An age is the loop's normalized age at the
    // particle's birth when the emitter has a duration, 0 for a birth
    // before its start delay ends (a host's spawn); else the seconds since
    // the emitter began, negative before it begins.
    typedef int32_t mvfxSourceKind;
    enum
    {
        // No value: the attribute is not stored (an optional one), or the
        // source is invalid (a required one).
        mvfx_sourceNone = 0,
        // A base of 1: uniform between min and max.
        mvfx_sourceRange = 1,
        // The curve's value at the age.
        mvfx_sourceCurve = 2,
        // A parameter of the instance, its x for a scalar.
        mvfx_sourceParameter = 3,
        // The gradient at a uniformly random point (vectors of four only).
        mvfx_sourceGradient = 4,
        // The gradient at the age, which needs a duration (vectors of four
        // only).
        mvfx_sourceGradientOverAge = 5,
    };

    // A scalar source: base times (min + (max - min) u).
    typedef struct mvfxScalarSource
    {
        mvfxSourceKind kind;
        // Finite, min at most max, each at most MVFX_MAX_VALUE in size, the
        // base's largest size times the larger of theirs within the
        // source's cap (MVFX_MAX_DRAW).
        mvfxRange range;
        // For the curve kind: keys in [0, 1] with a duration, in seconds
        // without; baked when the effect is created.
        mvfxCurve curve;
        // For the parameter kind: an index below the effect's count.
        int32_t parameter;
    } mvfxScalarSource;

    // A vector source of up to four components: each component its base
    // times min plus (max - min) times its own draw, or one draw shared by
    // every component (uniform: a color then lies on the line between min
    // and max). Min and max are ends, in either order, each finite and at
    // most MVFX_MAX_VALUE in size; the base's largest size times each
    // component's larger end is within that component's cap
    // (MVFX_MAX_DRAW).
    typedef struct mvfxVectorSource
    {
        mvfxSourceKind kind;
        float min[4];
        float max[4];
        bool uniform;
        // For the curve kind, scaling every component.
        mvfxCurve curve;
        // For the gradient kinds.
        mvfxGradient gradient;
        // For the parameter kind: each component scales by the parameter's.
        int32_t parameter;
    } mvfxVectorSource;

    /// Returns a scalar source uniform between min and max.
    ///
    /// @param min  The least value.
    /// @param max  The greatest value.
    /// @return The source.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mvfxScalarSource mvfxMakeRangeSource(float min, float max)
    {
#ifdef __cplusplus
        mvfxScalarSource source = {};
#else
    mvfxScalarSource source = {0};
#endif
        source.kind = mvfx_sourceRange;
        source.range.min = min;
        source.range.max = max;
        return source;
    }

    // How a mesh particle's orientation starts.
    typedef int32_t mvfxOrientationMode;
    enum
    {
        // Not stored, unless an angular velocity is.
        mvfx_orientationNone = 0,
        // From Euler angles in radians, turned about X, then Y, then Z.
        mvfx_orientationEuler = 1,
        // Uniformly random over every orientation.
        mvfx_orientationRandom = 2,
    };

    // Which flipbook frame a particle starts at.
    typedef int32_t mvfxFrameMode;
    enum
    {
        // Not stored.
        mvfx_frameNone = 0,
        // The first.
        mvfx_frameFixed = 1,
        // Any from first to last, alike.
        mvfx_frameRandom = 2,
    };

// The most frames a flipbook start may name.
#define MVFX_MAX_FRAMES 65536

    // A flipbook start: 0 <= first <= last < MVFX_MAX_FRAMES.
    typedef struct mvfxFrameStart
    {
        mvfxFrameMode mode;
        int32_t first;
        int32_t last;
    } mvfxFrameStart;

    // A custom attribute: one to four floats a particle carries for the
    // host's modules and outputs, from an initial source (none starts them
    // at 0), each as shown (times overLife where scaled) within
    // MVFX_MAX_VALUE.
    typedef struct mvfxCustomAttribute
    {
        int32_t components;
        mvfxVectorSource initial;
        // Whether every component is scaled over the particle's age by
        // overLife, where the attribute is read (never written back).
        bool scaledOverLife;
        mvfxLifeCurve overLife;
    } mvfxCustomAttribute;

#ifdef __cplusplus
}
#endif

#endif
