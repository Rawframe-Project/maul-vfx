// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Output: what the host draws, as data in its own layout. The library
// draws nothing; after a step the host asks, per view and per emitter,
// for records written into its buffers.

#ifndef MAUL_VFX_OUTPUT_H
#define MAUL_VFX_OUTPUT_H

#include "maul-vfx/instance.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most fields a layout has, and the most bytes a record takes.
#define MVFX_MAX_LAYOUT_FIELDS 32
#define MVFX_MAX_LAYOUT_STRIDE 1024

    // Names a layout of a context: a 1-based slot, 0 for the null id, and a
    // generation that tells a live layout from earlier occupants of its
    // slot.
    typedef struct mvfxLayoutId
    {
        uint32_t index1;
        uint32_t generation;
    } mvfxLayoutId;

    // What a field of a record holds, from a particle.
    typedef uint8_t mvfxField;
    enum
    {
        // Its place in the world less the view's origin, 1 to 3 components.
        mvfx_recordPosition = 0,
        // Its size in the world, 1 to 3 (1 where the emitter stores none).
        mvfx_recordSize = 1,
        // Its colour with its values over life, 1 to 4 (1 where none).
        mvfx_recordColor = 2,
        // Its angle in radians, a sprite's turn about the view (0 where none).
        mvfx_recordAngle = 3,
        // Its orientation in the world, a quaternion (x, y, z, w), 4.
        mvfx_recordOrientation = 4,
        // Its velocity in the world, 1 to 3.
        mvfx_recordVelocity = 5,
        // Its age over its life, from 0 to 1.
        mvfx_recordAge = 6,
        // Its flipbook frame (0 where none): uint32 or binary32.
        mvfx_recordFrame = 7,
        // A custom attribute's components, 1 to 4 (0 past those it has, or
        // for one the emitter does not declare).
        mvfx_recordCustom = 8,
        // Its serial: uint32.
        mvfx_recordSerial = 9,
    };

    // How a field's components are written.
    typedef uint8_t mvfxFormat;
    enum
    {
        // binary32.
        mvfx_formatFloat32 = 0,
        // binary16, rounded to nearest, ties to even.
        mvfx_formatFloat16 = 1,
        // From 0 to 1 (clamped) as 0 to 255, rounded half up.
        mvfx_formatUnorm8 = 2,
        // From -1 to 1 (clamped) as -127 to 127, rounded half away from 0.
        mvfx_formatSnorm8 = 3,
        // From 0 to 1 as 0 to 65,535.
        mvfx_formatUnorm16 = 4,
        // From -1 to 1 as -32,767 to 32,767.
        mvfx_formatSnorm16 = 5,
        // A uint32: a frame or a serial only.
        mvfx_formatUint32 = 6,
    };

    // A field of a layout: what, in which format, how many components, at
    // which byte offset (aligned to its format's size: 4, 2 or 1). A
    // colour's red, green and blue may be encoded to sRGB in unorm8.
    typedef struct mvfxLayoutField
    {
        mvfxField field;
        mvfxFormat format;
        uint8_t components;
        // A custom field's attribute, from 0 to MVFX_MAX_CUSTOM_ATTRIBUTES - 1.
        uint8_t custom;
        bool srgb;
        uint32_t offset;
    } mvfxLayoutField;

    // How a layout is made. Build it with mvfxDefaultLayoutDef. The layout
    // copies its fields.
    typedef struct mvfxLayoutDef
    {
        uint32_t cookie;
        // From 1 to MVFX_MAX_LAYOUT_FIELDS fields, none overlapping another.
        const mvfxLayoutField* fields;
        int32_t fieldCount;
        // The bytes from one record to the next, from 1 to
        // MVFX_MAX_LAYOUT_STRIDE, covering every field.
        uint32_t stride;
    } mvfxLayoutDef;

// The cookie of a layout def, which its default sets and mvfxCreateLayout
// checks.
#define MVFX_LAYOUT_DEF_COOKIE 0x6D766C79u

    /// Returns the default layout def: no fields yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxLayoutDef mvfxDefaultLayoutDef(void);

    /// Creates a layout: checks the def and copies its fields.
    ///
    /// @param context    The context that will own the layout.
    /// @param def        The def, from mvfxDefaultLayoutDef.
    /// @param layoutOut  Receives the layout's id; the null id on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def
    ///         without its cookie, a count, stride, field, format,
    ///         component count, offset or overlap out of range, which counts
    ///         as misuse when the context is given; `mvfx_errorLimit` when
    ///         the context holds its limit of layouts; `mvfx_errorCapacity`
    ///         when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateLayout(mvfxContext* context,
                                                        const mvfxLayoutDef* def,
                                                        mvfxLayoutId* layoutOut);

    /// Destroys a layout.
    ///
    /// @param context  The context that owns the layout.
    /// @param layout   The layout.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         layout; `mvfx_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyLayout(mvfxContext* context, mvfxLayoutId layout);

    /// Returns whether an id names a live layout of the context. A stale id
    /// is not misuse.
    ///
    /// @param context  The context.
    /// @param layout   Any id.
    /// @return Whether the layout is alive.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API bool mvfxLayout_IsValid(const mvfxContext* context, mvfxLayoutId layout);

    /// Writes a record in a layout for each live particle of an emitter, in
    /// the pool's order (oldest first), for a view whose origin positions
    /// are made relative to (binary64, each coordinate at most 2^52 in
    /// size). A buffer too small takes the first capacity records; the
    /// count is always the emitter's live particles.
    ///
    /// @param context     The context that owns the instance and the layout.
    /// @param instance    The instance.
    /// @param emitter     The emitter's index in the effect's def.
    /// @param layout      The layout.
    /// @param viewOrigin  The view's origin in the world.
    /// @param records     The buffer, capacity times the layout's stride
    ///                    bytes; NULL when capacity is 0.
    /// @param capacity    The records it holds, at least 0.
    /// @param countOut    Receives the records there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout
    ///         that names nothing live; `mvfx_errorInvalid` for a NULL
    ///         context, origin or count, an emitter out of range, a
    ///         negative capacity or a NULL buffer with a capacity, which
    ///         counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs (calls that only read a stepped context and
    /// write the host's buffers) may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteParticles(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const double viewOrigin[3], void* records, int64_t capacity, int64_t* countOut);

#ifdef __cplusplus
}
#endif

#endif
