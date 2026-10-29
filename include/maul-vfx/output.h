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
        // Its angle in radians, a sprite's turn about the view (0 where
        // none): its initial angle plus the turns its spin summed, at most
        // MVFX_MAX_SPIN times MVFX_MAX_LIFETIME (1e12) from it.
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
        // The index of the host's mesh it is drawn as (0 where none):
        // uint32 or binary32.
        mvfx_recordMesh = 10,
        // A ribbon point's unit tangent in the world toward the younger
        // end, a sheet vertex's across its sheet, 3 (0 in a particle's
        // record).
        mvfx_recordTangent = 11,
        // Its width: its size's X in the world times the ribbon's width
        // over length (a particle's record: its size's X).
        mvfx_recordWidth = 12,
        // Its normalized length along its ribbon, from 0 at the oldest point
        // to 1 at the youngest (0 in a particle's record).
        mvfx_recordLength = 13,
        // Its texture coordinate along its ribbon (0 in a particle's
        // record).
        mvfx_recordAlong = 14,
        // Its ribbon's index in the ribbon list, a sheet vertex's sheet's
        // among the emitter's: uint32 or binary32 (0 in a particle's
        // record).
        mvfx_recordRibbon = 15,
        // Its unit side across the ribbon, toward which its width spans:
        // the tangent crossed with the way to the eye or with the facing
        // normal; a sheet vertex's down its sheet, 3 (0 in a particle's
        // record or where they are parallel).
        mvfx_recordSide = 16,
        // The unit normal of its face, the side crossed with the tangent; a
        // sheet vertex's, the tangent crossed with the side, 3 (0 in a
        // particle's record).
        mvfx_recordFaceNormal = 17,
        // An expanded vertex's texture coordinate, 2: a sprite's corner, from
        // (0, 0) at its left bottom to (1, 1) at its right top; a ribbon's
        // coordinate along and 0 or 1 across; a sheet vertex's place across
        // and row over the last of each, from (0, 0) at its first point to
        // (1, 1) (0 elsewhere).
        mvfx_recordCorner = 18,
        // Its count of neighbors (0 where it finds none): uint32 or
        // binary32.
        mvfx_recordNeighborCount = 19,
        // The offset from it to its neighbors' mean place in the world, 1 to
        // 3 (0 where none).
        mvfx_recordNeighborCenter = 20,
        // The offset from its velocity to its neighbors' mean velocity in
        // the world, 1 to 3 (0 where none).
        mvfx_recordNeighborVelocity = 21,
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
        // A uint32: a frame, a serial, a mesh index or a ribbon only.
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
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def without its cookie, a count or stride below 1, a field,
    ///         format, component count, offset or overlap out of range,
    ///         which counts as misuse when the context is given;
    ///         `mvfx_errorLimit` for more fields than
    ///         MVFX_MAX_LAYOUT_FIELDS or a stride past
    ///         MVFX_MAX_LAYOUT_STRIDE, which counts none;
    ///         `mvfx_errorCapacity` when the context holds as many layouts
    ///         as its def allows, or when the allocator fails.
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

    // A layout as made: its fields, staying while it does, and its stride.
    typedef struct mvfxLayoutView
    {
        const mvfxLayoutField* fields;
        int32_t fieldCount;
        uint32_t stride;
    } mvfxLayoutView;

    /// Returns a layout as made, for a simulation elsewhere to write it.
    ///
    /// @param context  The context that owns the layout.
    /// @param layout   The layout.
    /// @param viewOut  Receives the view; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         layout; `mvfx_errorInvalid` for a NULL context or out
    ///         parameter.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxLayout_GetView(const mvfxContext* context,
                                                          mvfxLayoutId layout,
                                                          mvfxLayoutView* viewOut);

// The steps of an sRGB byte: one for each byte value above 0.
#define MVFX_SRGB_STEPS 255

    /// Fills the least binary32 values at which a linear colour channel's
    /// byte in an sRGB unorm8 field steps up: a value's byte is how many of
    /// them it is not below (none for NaN), as records are written; for a
    /// simulation elsewhere to write the same bytes.
    ///
    /// @param steps  Receives MVFX_SRGB_STEPS values, rising.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API void mvfxSrgbSteps(float steps[MVFX_SRGB_STEPS]);

    // A view the host draws: where positions are made relative to, the eye
    // a sort measures from, and the direction it looks.
    typedef struct mvfxView
    {
        // Records' positions are less this (each coordinate at most 2^52
        // in size).
        double origin[3];
        // The eye, within the same bounds.
        double eye[3];
        // The direction depth is measured along: finite, of any length.
        float forward[3];
        // Which way is up on the view, for sprites facing it: finite, not
        // along forward; any length.
        float up[3];
    } mvfxView;

    /// Returns the bytes of scratch an emitter's output takes, enough for
    /// any step: 16 a particle of its capacity when it sorts by place or is
    /// drawn as ribbons, else 0.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param emitter   The emitter's index in the effect's def.
    /// @param bytesOut  Receives the bytes.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance that names
    ///         nothing live; `mvfx_errorInvalid` for a NULL context or out
    ///         parameter or an emitter out of range, which counts as misuse
    ///         when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetSortScratchBytes(mvfxContext* context,
                                                                        mvfxInstanceId instance,
                                                                        int32_t emitter,
                                                                        int64_t* bytesOut);

    /// Writes a record in a layout for each live particle of an emitter for
    /// a view, in the order its def asks (mvfxSortMode): the pool's, its
    /// reverse, or by distance or depth from the view's eye through a
    /// stable radix sort, so equal keys keep the pool's order. A buffer too
    /// small takes the first capacity records of that order; the count is
    /// always the emitter's live particles.
    ///
    /// @param context       The context that owns the instance and the
    ///                      layout.
    /// @param instance      The instance.
    /// @param emitter       The emitter's index in the effect's def.
    /// @param layout        The layout.
    /// @param view          The view.
    /// @param scratch       Memory for a sort by place, aligned to 4 bytes,
    ///                      of at least 16 bytes a live particle
    ///                      (mvfxInstance_GetSortScratchBytes gives enough
    ///                      for any step); NULL when scratchBytes is 0.
    ///                      Its contents are not kept.
    /// @param scratchBytes  Its bytes, at least 0.
    /// @param records       The buffer, capacity times the layout's stride
    ///                      bytes; NULL when capacity is 0.
    /// @param capacity      The records it holds, at least 0.
    /// @param countOut      Receives the records there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout
    ///         that names nothing live; `mvfx_errorCapacity` for scratch too
    ///         small for a sort by place, with nothing written;
    ///         `mvfx_errorInvalid` for a NULL context, view or count, a
    ///         view out of range, an emitter out of range, a negative
    ///         capacity or scratch size, a NULL or misaligned buffer with a
    ///         size, which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs (calls that only read a stepped context and
    /// write the host's buffers) may run on several threads at once, each
    /// with its own scratch.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteParticles(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, void* scratch, int64_t scratchBytes, void* records, int64_t capacity,
        int64_t* countOut);

    // A ribbon of a ribbon list: its points in the point buffer and its id.
    typedef struct mvfxRibbon
    {
        int64_t first;
        int32_t count;
        uint32_t id;
    } mvfxRibbon;

    // Where mvfxInstance_WriteRibbons writes: points in a layout, the list
    // of ribbons, and scratch for chaining them (aligned to 4 bytes, 16
    // bytes a live particle: mvfxInstance_GetSortScratchBytes). Each
    // pointer may be NULL only when its size is 0.
    typedef struct mvfxRibbonBuffers
    {
        void* points;
        int64_t pointCapacity;
        mvfxRibbon* ribbons;
        int64_t ribbonCapacity;
        void* scratch;
        int64_t scratchBytes;
    } mvfxRibbonBuffers;

    /// Writes an emitter's ribbons for a view: each run of its live
    /// particles with one ribbon id and at least two particles, oldest
    /// first, in the order of their ids. Each point is a record of the
    /// layout, its ribbon fields (tangent, width, length, coordinate along,
    /// ribbon) filled; its colour is times the ribbon's colour over length.
    /// Buffers too small take the first ribbons that fit whole, so the list
    /// never names a point not written; the counts are always all of them,
    /// 0 for an emitter not drawn as ribbons.
    ///
    /// @param context         The context that owns the instance and the
    ///                        layout.
    /// @param instance        The instance.
    /// @param emitter         The emitter's index in the effect's def.
    /// @param layout          The points' layout.
    /// @param view            The view.
    /// @param buffers         The buffers.
    /// @param pointCountOut   Receives the points there are.
    /// @param ribbonCountOut  Receives the ribbons there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout
    ///         that names nothing live; `mvfx_errorCapacity` for scratch too
    ///         small, with nothing written; `mvfx_errorInvalid` for a NULL
    ///         context, view, buffers or count, a view out of range, an
    ///         emitter out of range, a negative size or a NULL or misaligned
    ///         buffer with a size, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once, each with
    /// its own scratch.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteRibbons(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, const mvfxRibbonBuffers* buffers, int64_t* pointCountOut,
        int64_t* ribbonCountOut);

    // Where an expansion writes: vertices in a layout, uint32 indices of
    // triangles into them (from 0 at the buffer's first vertex), and
    // scratch for the emitter's sort (mvfxInstance_GetSortScratchBytes).
    // Each pointer may be NULL only when its size is 0.
    typedef struct mvfxExpandBuffers
    {
        void* vertices;
        int64_t vertexCapacity;
        uint32_t* indices;
        int64_t indexCapacity;
        void* scratch;
        int64_t scratchBytes;
    } mvfxExpandBuffers;

    /// Expands an emitter's particles for a view into quads, for renderers
    /// that cannot draw instances: four vertices a particle (left bottom,
    /// right bottom, left top, right top; records of the layout with their
    /// corner fields, at the corners' places) and six indices (two
    /// triangles, counterclockwise seen from the side the quad faces), in
    /// the order its def sorts, each quad its size's X wide and Y high
    /// about the particle and turned as its sprite def's billboard says.
    /// Buffers too small take the first whole quads; the count is always
    /// the live particles.
    ///
    /// @param context     The context that owns the instance and the layout.
    /// @param instance    The instance.
    /// @param emitter     The emitter's index in the effect's def.
    /// @param layout      The vertices' layout.
    /// @param view        The view.
    /// @param buffers     The buffers.
    /// @param countOut    Receives the quads there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout
    ///         that names nothing live; `mvfx_errorCapacity` for scratch too
    ///         small for the emitter's sort, with nothing written;
    ///         `mvfx_errorInvalid` for a NULL context, view, buffers or
    ///         count, a view out of range, an emitter out of range, a
    ///         negative size or a NULL or misaligned buffer with a size, which
    ///         counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once, each with
    /// its own scratch.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandSprites(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* countOut);

    /// Expands an emitter's ribbons for a view into triangle strips, for
    /// renderers that cannot expand them in a shader: each point (as
    /// mvfxInstance_WriteRibbons makes them, tessellated) two vertices
    /// across its width along its side (corner 0 on the side's back, 1 on
    /// it, the coordinate along as the corner's first), and each segment
    /// six indices (two triangles, counterclockwise seen from its face
    /// normal's side). Buffers too small take the first whole ribbons that
    /// fit both; the counts are always all of them, 0 for an emitter not
    /// drawn as ribbons.
    ///
    /// @param context         The context that owns the instance and the
    ///                        layout.
    /// @param instance        The instance.
    /// @param emitter         The emitter's index in the effect's def.
    /// @param layout          The vertices' layout.
    /// @param view            The view.
    /// @param buffers         The buffers; scratch as for
    ///                        mvfxInstance_WriteRibbons.
    /// @param vertexCountOut  Receives the vertices there are.
    /// @param indexCountOut   Receives the indices there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout
    ///         that names nothing live; `mvfx_errorCapacity` for scratch too
    ///         small, with nothing written; `mvfx_errorInvalid` for a NULL
    ///         context, view, buffers or count, a view out of range, an
    ///         emitter out of range, a negative size or a NULL or misaligned
    ///         buffer with a size, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once, each with
    /// its own scratch.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandRibbons(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* vertexCountOut,
        int64_t* indexCountOut);

// The most sides of a tube.
#define MVFX_MAX_TUBE_SIDES 64

    // How ribbons are drawn as tubes: the sides of each ring (3 to
    // MVFX_MAX_TUBE_SIDES), and whether each tube's ends are capped.
    typedef struct mvfxTubeDef
    {
        int32_t sides;
        bool caps;
    } mvfxTubeDef;

    /// Expands an emitter's ribbons for a view into tubes, for chains and
    /// trails drawn round: each point (as mvfxInstance_WriteRibbons makes
    /// them, tessellated) a ring of sides + 1 vertices at half its width
    /// about it (the last at the first's place, so the texture's seam
    /// closes), turned along the ribbon by rotation-minimizing frames (Wang
    /// et al. 2008) from a normal across its first tangent, the face normal
    /// outward, the side around, the corner the coordinate along and the
    /// share around; each segment sides quads, six indices each,
    /// counterclockwise seen from outside. With caps, after a ribbon's
    /// rings, its start and end each a center and a ring (sides + 2
    /// vertices facing back along and along the tangent, corners a disc's)
    /// and sides triangles. Buffers too small take the first whole ribbons
    /// that fit both; the counts are always all of them, 0 for an emitter
    /// not drawn as ribbons.
    ///
    /// @param context         The context that owns the instance and the
    ///                        layout.
    /// @param instance        The instance.
    /// @param emitter         The emitter's index in the effect's def.
    /// @param layout          The vertices' layout.
    /// @param view            The view.
    /// @param tube            The tubes' sides and caps.
    /// @param buffers         The buffers; scratch as for
    ///                        mvfxInstance_WriteRibbons.
    /// @param vertexCountOut  Receives the vertices there are.
    /// @param indexCountOut   Receives the indices there are.
    /// @return As mvfxInstance_ExpandRibbons; `mvfx_errorInvalid` too for a
    ///         NULL tube or its sides out of range.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once, each with
    /// its own scratch.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandTubes(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, const mvfxTubeDef* tube, const mvfxExpandBuffers* buffers,
        int64_t* vertexCountOut, int64_t* indexCountOut);

    /// Writes an emitter's plexus links for a view, as two-point ribbons:
    /// each link the step found (the order it found them) is a ribbon of
    /// its two particles' records, owner first, its ribbon fields filled as
    /// a ribbon's (straight, the coordinate along 0 and 1, facing the eye,
    /// the width the plexus width; its id the link's index), its colour's
    /// alpha times the fade when the plexus fades. Buffers too small take
    /// the first links that fit whole; the counts are always all of them, 0
    /// for an emitter without plexus links. Scratch is not used.
    ///
    /// @param context         The context that owns the instance and the
    ///                        layout.
    /// @param instance        The instance.
    /// @param emitter         The emitter's index in the effect's def.
    /// @param layout          The points' layout.
    /// @param view            The view.
    /// @param buffers         The buffers.
    /// @param pointCountOut   Receives the points there are.
    /// @param ribbonCountOut  Receives the links there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout
    ///         that names nothing live; `mvfx_errorInvalid` for a NULL
    ///         context, view, buffers or count, a view out of range, an
    ///         emitter out of range, a negative size or a NULL or misaligned
    ///         buffer with a size, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WritePlexus(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, const mvfxRibbonBuffers* buffers, int64_t* pointCountOut,
        int64_t* ribbonCountOut);

    /// Expands an emitter's plexus links for a view into triangle strips,
    /// as mvfxInstance_ExpandRibbons expands ribbons: each link four
    /// vertices and six indices. Buffers too small take the first whole
    /// links that fit both; the counts are always all of them. Scratch is
    /// not used.
    ///
    /// @param context         The context that owns the instance and the
    ///                        layout.
    /// @param instance        The instance.
    /// @param emitter         The emitter's index in the effect's def.
    /// @param layout          The vertices' layout.
    /// @param view            The view.
    /// @param buffers         The buffers.
    /// @param vertexCountOut  Receives the vertices there are.
    /// @param indexCountOut   Receives the indices there are.
    /// @return As mvfxInstance_WritePlexus.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandPlexus(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* vertexCountOut,
        int64_t* indexCountOut);

    /// Expands an emitter's sheets (mvfx_unitSheet) into meshes, oldest
    /// first: each sheet a vertex a point, row after row (records of the
    /// layout with their tangent across, side down and face normal from
    /// the neighboring points, their corner the place across and row over
    /// the last of each, their ribbon the sheet's index), and each quad
    /// between rows not torn (none of its edges, mvfxUnitDef.tearStrain)
    /// six indices (two triangles, counterclockwise seen from the face
    /// normal's side). Buffers too small take the first whole
    /// sheets that fit both; the counts are always all of them, 0 for an
    /// emitter of other units. Scratch is not used.
    ///
    /// @param context         The context that owns the instance and the
    ///                        layout.
    /// @param instance        The instance.
    /// @param emitter         The emitter's index in the effect's def.
    /// @param layout          The vertices' layout.
    /// @param view            The view.
    /// @param buffers         The buffers.
    /// @param vertexCountOut  Receives the vertices there are.
    /// @param indexCountOut   Receives the indices there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout
    ///         that names nothing live; `mvfx_errorInvalid` for a NULL
    ///         context, view, buffers or count, a view out of range, an
    ///         emitter out of range, a negative size or a NULL or misaligned
    ///         buffer with a size, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandSheets(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout,
        const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* vertexCountOut,
        int64_t* indexCountOut);

    // A light a particle carries.
    typedef struct mvfxLight
    {
        // Its place in the world less the view's origin.
        float position[3];
        float radius;
        // Its colour times its intensity.
        float color[3];
        // The serial of the particle that carries it.
        uint32_t serial;
    } mvfxLight;

    /// Writes the lights an emitter's particles carry (mvfxLightDef: every
    /// n-th particle by serial), in the pool's order (oldest first), for a
    /// view (positions less its origin). A buffer too small takes the first capacity, so
    /// a host that wants at most n lights passes n; the count is always all
    /// of them, 0 for an emitter without lights.
    ///
    /// @param context     The context that owns the instance.
    /// @param instance    The instance.
    /// @param emitter     The emitter's index in the effect's def.
    /// @param view        The view.
    /// @param lights      The buffer; NULL when capacity is 0.
    /// @param capacity    The lights it holds, at least 0.
    /// @param countOut    Receives the lights there are.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance that names
    ///         nothing live; `mvfx_errorInvalid` for a NULL context, view or
    ///         count, a view out of range, an emitter out of range, a
    ///         negative capacity or a NULL buffer with a capacity, which
    ///         counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteLights(
        mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxView* view,
        mvfxLight* lights, int64_t capacity, int64_t* countOut);

    // A box in the world.
    typedef struct mvfxBounds
    {
        // Whether it holds nothing (an emitter with no particles and no
        // fixed bounds); min and max are 0 then.
        bool empty;
        double min[3];
        double max[3];
    } mvfxBounds;

    /// Reads an emitter's bounds in the world after the last step: its
    /// fixed box through the instance's transform, or the box of its live
    /// particles (measured at the step's end, grown on every side by the
    /// largest size component any of them can have over its life: its
    /// stored size times the most its size over life scales it); with
    /// emitter -1, the box around every emitter's and every gas's box.
    ///
    /// @param context    The context that owns the instance.
    /// @param instance   The instance.
    /// @param emitter    The emitter's index in the effect's def, or -1.
    /// @param boundsOut  Receives the bounds.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         instance; `mvfx_errorInvalid` for a NULL argument or an emitter
    ///         out of range, which counts as misuse when the context is
    ///         given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetBounds(mvfxContext* context,
                                                              mvfxInstanceId instance,
                                                              int32_t emitter,
                                                              mvfxBounds* boundsOut);

    // How a gas's field is read out: over a box of cells (first, then size
    // along each axis, within the gas), in a format, a row of cells from
    // rowBytes to the next and a layer from layerBytes (0 for packed, else
    // at least the packed size). float32 and float16 take the values as
    // they are; unorm8 and unorm16 map low to 0 and high to 1, snorm8 and
    // snorm16 to -1 and 1, clamped and rounded as a layout's fields are
    // (low and high finite, high above low). A velocity's component is the
    // mean of the two faces' about each center.
    typedef struct mvfxGasRead
    {
        mvfxGasField field;
        mvfxFormat format;
        int32_t first[3];
        int32_t size[3];
        float low;
        float high;
        uint32_t rowBytes;
        uint32_t layerBytes;
    } mvfxGasRead;

    /// Writes a field of a gas of an instance over a box of its cells in a
    /// format, as the last step left it, X fastest, then Y, then Z: for a
    /// volume texture, or a region of one.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param gas       The gas's index in its effect's def.
    /// @param read      What to read and how.
    /// @param out       Receives the values.
    /// @param bytes     out's size in bytes: at least the last value's end.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no
    ///         live instance; `mvfx_errorInvalid` for a NULL argument, a
    ///         gas index, field, format, box, range or pitch out of range
    ///         (a surface's only field its distance, which no other gas
    ///         has), or bytes too few, which counts as misuse when the
    ///         context is given; `mvfx_errorUnsupported` for a planned
    ///         instance, whose gases' fields its simulation holds.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ReadGasAs(const mvfxContext* context,
                                                              mvfxInstanceId instance, int32_t gas,
                                                              const mvfxGasRead* read, void* out,
                                                              int64_t bytes);

    /// Samples a field of a gas of an instance at points in the world, as
    /// the last step left it: trilinear, as particles following the gas
    /// read it, 0 outside its box (a surface's distance its reach; a 2D
    /// gas's reaching along its Z without end); a velocity's component
    /// along the box's axis.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param gas       The gas's index in its effect's def.
    /// @param field     The field.
    /// @param points    count points, x, y and z each, finite.
    /// @param count     How many, from 0 to INT64_MAX / 3; points and out may
    ///                  be NULL for 0.
    /// @param out       Receives count values.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no
    ///         live instance; `mvfx_errorInvalid` for a NULL argument, a
    ///         gas index or field out of range, a count out of range or a
    ///         point not finite, which counts as misuse when the context is
    ///         given; `mvfx_errorUnsupported` for a planned instance, whose
    ///         gases' fields its simulation holds.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SampleGas(const mvfxContext* context,
                                                              mvfxInstanceId instance, int32_t gas,
                                                              mvfxGasField field,
                                                              const double* points, int64_t count,
                                                              float* out);

    // How a water surface's field is read out: over a box of cells (first,
    // then size, along X and Z), in a format, a row of cells (along X) from
    // rowBytes to the next (0 for packed, else at least the packed size).
    // float32 and float16 take the values as they are; unorm8 and unorm16
    // map low to 0 and high to 1, snorm8 and snorm16 to -1 and 1, clamped
    // and rounded as a layout's fields are (low and high finite, high above
    // low).
    typedef struct mvfxWaterRead
    {
        mvfxWaterField field;
        mvfxFormat format;
        int32_t first[2];
        int32_t size[2];
        float low;
        float high;
        uint32_t rowBytes;
    } mvfxWaterRead;

    /// Writes a field of a water surface of an instance over a box of its
    /// cells in a format, as the last step left it.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param water     The surface's index in its effect's def.
    /// @param read      The field, format, box, range and pitch.
    /// @param out       The buffer.
    /// @param bytes     Its bytes, at least the read's.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no
    ///         live instance; `mvfx_errorInvalid` for a NULL argument, a
    ///         surface index, field, format, box, range or pitch out of
    ///         range, or bytes too few, which counts as misuse when the
    ///         context is given; `mvfx_errorUnsupported` for a planned
    ///         instance, whose fields are its simulation's.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ReadWater(const mvfxContext* context,
                                                              mvfxInstanceId instance,
                                                              int32_t water,
                                                              const mvfxWaterRead* read, void* out,
                                                              int64_t bytes);

    // A water surface at a point: the surface's point along its frame's Y
    // through it (in the world), its unit normal and its velocity (in the
    // world's directions), and whether the point is over the grid (else
    // the rest plane, its normal the frame's Y, still).
    typedef struct mvfxWaterSample
    {
        double surface[3];
        float normal[3];
        float velocity[3];
        bool inside;
    } mvfxWaterSample;

    /// Samples a water surface of an instance at points in the world, as
    /// the last step left it: its heights, rates and normals bilinear
    /// between the cells' centers, held at the outer cells' within the
    /// grid's edge.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param water     The surface's index in its effect's def.
    /// @param points    count points, x, y and z each, finite.
    /// @param count     How many, from 0 to INT64_MAX / 3; points and out may
    ///                  be NULL for 0.
    /// @param out       Receives count samples.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no
    ///         live instance; `mvfx_errorInvalid` for a NULL argument, a
    ///         surface index out of range, a count out of range or a point
    ///         not finite, which counts as misuse when the context is
    ///         given; `mvfx_errorUnsupported` for a planned instance, whose
    ///         fields are its simulation's.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SampleWater(const mvfxContext* context,
                                                                mvfxInstanceId instance,
                                                                int32_t water, const double* points,
                                                                int64_t count,
                                                                mvfxWaterSample* out);

// The most planes a frustum has.
#define MVFX_MAX_FRUSTUM_PLANES 8

    // A view's frustum: planes in the world, each n.x + d >= 0 inside it
    // (n need not be of unit length).
    typedef struct mvfxFrustum
    {
        double planes[MVFX_MAX_FRUSTUM_PLANES][4];
        int32_t planeCount;
    } mvfxFrustum;

    // An emitter to draw for a view.
    typedef struct mvfxDrawItem
    {
        mvfxInstanceId instance;
        int32_t emitter;
        int32_t priority;
        // From the eye to its bounds' center.
        double distance;
    } mvfxDrawItem;

    /// Lists a view's emitters to draw, in the order to draw them: every
    /// live instance's emitters with particles (and, with a frustum, bounds
    /// not wholly outside one of its planes), by their effect's draw
    /// priority, lower first, then farther first, then by instance slot and
    /// emitter index, so the order is fixed for given inputs. A buffer too
    /// small takes the first capacity of that order; the count is always
    /// all of them.
    ///
    /// @param context   The context.
    /// @param eye       The view's eye in the world (each coordinate at most
    ///                  2^52 in size).
    /// @param frustum   Its frustum, or NULL to keep every emitter; from 0 to
    ///                  MVFX_MAX_FRUSTUM_PLANES finite planes.
    /// @param items     The buffer; NULL when capacity is 0.
    /// @param capacity  The items it holds, at least 0.
    /// @param countOut  Receives how many emitters there are to draw.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context, eye or
    ///         count, a frustum out of range, a negative capacity or a NULL
    ///         buffer with a capacity, which counts as misuse when the
    ///         context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult
    mvfxContext_OrderEmitters(mvfxContext* context, const double eye[3], const mvfxFrustum* frustum,
                              mvfxDrawItem* items, int64_t capacity, int64_t* countOut);

    // Where mvfxInstance_WriteSurfaceMesh writes: each vertex's position (3
    // floats) and, unless normals is NULL, its unit normal (3 floats); the
    // indices, 3 a triangle (2 a segment for a 2D surface); and scratch
    // (aligned to 4 bytes, mvfxInstance_GetSurfaceMeshScratchBytes). Each
    // buffer is aligned to 4 bytes; each pointer but normals may be NULL
    // only when its size is 0.
    typedef struct mvfxSurfaceMeshBuffers
    {
        float* positions;
        float* normals;
        int64_t vertexCapacity;
        uint32_t* indices;
        int64_t indexCapacity;
        void* scratch;
        int64_t scratchBytes;
    } mvfxSurfaceMeshBuffers;

    /// Returns the bytes of scratch a surface gas's mesh takes: 32 a cell of
    /// a layer of its box (X by Y) in 3D, 24 a cell of a row in 2D.
    ///
    /// @param context   The context that owns the instance.
    /// @param instance  The instance.
    /// @param gas       The surface gas's index in its effect's def.
    /// @param bytesOut  Receives the bytes; 0 on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance that names
    ///         nothing live; `mvfx_errorInvalid` for a NULL context or out
    ///         parameter, or a gas out of range or not a surface, which
    ///         counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetSurfaceMeshScratchBytes(
        mvfxContext* context, mvfxInstanceId instance, int32_t gas, int64_t* bytesOut);

    /// Writes the mesh of a surface gas at a level of its distance (0 for
    /// its surface), as the last step left it, by marching cubes (marching
    /// squares for a 2D surface) between its cells' centers: a vertex on
    /// each edge where the distance crosses the level, by linear
    /// interpolation, shared by the cubes about it and numbered as the walk
    /// (Z, then Y, then X) first meets it, and its normal the distance's
    /// gradient there, out of the liquid; a face whose diagonal corners
    /// are alike is decided by the asymptotic decider (Nielson and Hamann
    /// 1991), the same from either cube, so the mesh has no cracks; a cube
    /// whose loop no triangles can close off its faces takes a vertex
    /// inside it, at the mean of the loop's points, its normal the mean of
    /// theirs. A sample exactly at the level is outside; the edges from it
    /// take its own vertex there, and a primitive whose vertices are not
    /// all distinct is left out, so no edge has zero length. A mesh has
    /// fewer than 2^24 vertices. Triangles turn counterclockwise seen from outside; a 2D
    /// surface's segments have the outside on their right. Positions are
    /// in the world less origin. The mesh's first triangles (or segments)
    /// that fit both buffers are written; the counts are always the whole
    /// mesh's. The same state gives the same bytes on every platform.
    ///
    /// @param context         The context that owns the instance.
    /// @param instance        The instance.
    /// @param gas             The surface gas's index in its effect's def.
    /// @param origin          Subtracted from positions (each coordinate at
    ///                        most 2^52 in size).
    /// @param level           The distance meshed, finite.
    /// @param buffers         The buffers.
    /// @param vertexCountOut  Receives the mesh's vertices; 0 on failure.
    /// @param indexCountOut   Receives the mesh's indices; 0 on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an instance that names
    ///         nothing live; `mvfx_errorUnsupported` for a planned instance
    ///         (its fields are its simulation's); `mvfx_errorCapacity` for
    ///         scratch too small, with nothing written; `mvfx_errorInvalid`
    ///         for a NULL context, origin, buffers or count, a gas out of
    ///         range or not a surface, an origin or level out of range, a
    ///         negative size or a NULL or misaligned buffer with a size,
    ///         which counts as misuse when the context is given.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// except that outputs may run on several threads at once, each with
    /// its own scratch.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteSurfaceMesh(
        mvfxContext* context, mvfxInstanceId instance, int32_t gas, const double origin[3],
        float level, const mvfxSurfaceMeshBuffers* buffers, int64_t* vertexCountOut,
        int64_t* indexCountOut);

#ifdef __cplusplus
}
#endif

#endif
