// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Meshes to spawn from: vertices, triangles and tetrahedra the host gives.
// A mesh shape samples its vertices, its edges by length, its surface by
// area or its volume (the tetrahedra) by volume. A skinned mesh is given
// new positions between steps.

#ifndef MAUL_VFX_MESH_H
#define MAUL_VFX_MESH_H

#include "maul-vfx/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most vertices a mesh holds.
#define MVFX_MAX_MESH_VERTICES (1 << 24)

// The most triangles or tetrahedra a mesh holds.
#define MVFX_MAX_MESH_ELEMENTS (1 << 24)

    // Names a mesh of a context: a 1-based slot, 0 for the null id, and a
    // generation that tells a live mesh from earlier occupants of its slot.
    typedef struct mvfxMeshId
    {
        uint32_t index1;
        uint32_t generation;
    } mvfxMeshId;

    // How a mesh is made. Build it with mvfxDefaultMeshDef. The mesh copies
    // what it needs: the arrays may be released once it is created.
    typedef struct mvfxMeshDef
    {
        uint32_t cookie;
        // Positions, x, y and z for each vertex, finite and at most
        // MVFX_MAX_EXTENT in size, in the space of the shape that samples it.
        const float* positions;
        // Unit normals for each vertex likewise, or NULL to compute them from
        // the triangles (by area, normalized; +Z for a vertex no triangle
        // touches).
        const float* normals;
        // From 1 to MVFX_MAX_MESH_VERTICES.
        int32_t vertexCount;
        // Three vertex indices a triangle, each below vertexCount; NULL when
        // triangleCount is 0.
        const uint32_t* triangles;
        int32_t triangleCount;
        // Four vertex indices a tetrahedron likewise.
        const uint32_t* tetrahedra;
        int32_t tetrahedronCount;
    } mvfxMeshDef;

// The cookie of a mesh def, which its default sets and mvfxCreateMesh
// checks.
#define MVFX_MESH_DEF_COOKIE 0x6D766D73u

    /// Returns the default mesh def, with no vertices yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxMeshDef mvfxDefaultMeshDef(void);

    /// Creates a mesh: checks the def, copies it, and builds its edges (each
    /// pair of vertices a triangle joins, once), its normals unless given,
    /// and the sums that choose an element by length, area or volume.
    ///
    /// @param context  The context that will own the mesh.
    /// @param def      The def, from mvfxDefaultMeshDef.
    /// @param meshOut  Receives the mesh's id; the null id on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def without its cookie, a count below its least, an index past
    ///         the vertices or a value not finite or out of range, which
    ///         counts as misuse when the context is given; `mvfx_errorLimit`
    ///         for more vertices than MVFX_MAX_MESH_VERTICES, more elements
    ///         than MVFX_MAX_MESH_ELEMENTS or a position past
    ///         MVFX_MAX_EXTENT, which counts none; `mvfx_errorCapacity` when
    ///         the context holds as many meshes as its def allows, or when
    ///         the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateMesh(mvfxContext* context, const mvfxMeshDef* def,
                                                      mvfxMeshId* meshOut);

    /// Destroys a mesh.
    ///
    /// @param context  The context that owns the mesh.
    /// @param mesh     The mesh.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         mesh; `mvfx_errorBusy` while an effect or an instance spawns
    ///         from it; `mvfx_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyMesh(mvfxContext* context, mvfxMeshId mesh);

    /// Returns whether an id names a live mesh of the context. A stale id is
    /// not misuse.
    ///
    /// @param context  The context.
    /// @param mesh     Any id.
    /// @return Whether the mesh is alive.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_API bool mvfxMesh_IsValid(const mvfxContext* context, mvfxMeshId mesh);

    // A mesh as stored, for a simulation elsewhere to copy: its vertices'
    // positions and unit normals (x, y and z each), its triangles (three
    // vertex indices each), edges (two, the lower first) and tetrahedra
    // (four), the running sums of their lengths, areas and volumes that
    // choose one (element i's sum includes it; binary64), and a serial new
    // at its creation and at each change of its positions, unique among
    // the context's meshes.
    typedef struct mvfxMeshView
    {
        const float* positions;
        const float* normals;
        int32_t vertexCount;
        const uint32_t* triangles;
        int32_t triangleCount;
        const uint32_t* edges;
        int32_t edgeCount;
        const uint32_t* tetrahedra;
        int32_t tetrahedronCount;
        const double* triangleSums;
        const double* edgeSums;
        const double* tetrahedronSums;
        uint64_t serial;
    } mvfxMeshView;

    /// Returns a mesh as stored. The arrays stay valid until the mesh's next
    /// change or destruction; the serial tells a change.
    ///
    /// @param context  The context that owns the mesh.
    /// @param mesh     The mesh.
    /// @param viewOut  Receives the view; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument;
    ///         `mvfx_errorStale` for an id that names no live mesh.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_GetView(const mvfxContext* context, mvfxMeshId mesh,
                                                        mvfxMeshView* viewOut);

    /// Gives a mesh new positions (and normals) for the same vertices, as a
    /// skinned mesh moves, and rebuilds what is drawn from them. Steps after
    /// spawn from the new positions.
    ///
    /// @param context      The context that owns the mesh.
    /// @param mesh         The mesh.
    /// @param positions    x, y and z for each vertex, as in the mesh def.
    /// @param normals      Unit normals for each vertex, or NULL to compute
    ///                     them.
    /// @param vertexCount  The mesh's count of vertices.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         mesh; `mvfx_errorInvalid` for a NULL context or positions, a
    ///         count not the mesh's or a value not finite or out of range;
    ///         `mvfx_errorLimit` for a position past MVFX_MAX_EXTENT. Either
    ///         counts as misuse when the context is given (the mesh is then
    ///         unchanged).
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_SetPositions(mvfxContext* context, mvfxMeshId mesh,
                                                             const float* positions,
                                                             const float* normals,
                                                             int32_t vertexCount);

    /// Gives the centroid of the convex hull of a mesh's vertices, the
    /// place of a rigid hull particle (mvfx_rigidHull) in the mesh's frame:
    /// a host draws the mesh at the particle's pose, scaled by its size and
    /// moved by minus this centroid scaled likewise.
    ///
    /// A mesh from a model file may have no hull a rigid particle takes:
    /// vertices that span no volume, or a hull of more than
    /// MVFX_MAX_HULL_VERTICES vertices (mvfxMesh_HullVertexCount tells by
    /// how much). That is a result, not an error.
    ///
    /// @param context    The context that owns the mesh.
    /// @param mesh       The mesh.
    /// @param hullOut    Receives whether the mesh has such a hull; false on
    ///                   failure.
    /// @param centerOut  Receives x, y and z; zeros without a hull or on
    ///                   failure.
    /// @return `mvfx_success`, with or without a hull; `mvfx_errorStale` for
    ///         an id that names no live mesh; `mvfx_errorInvalid` for a NULL
    ///         argument, which counts as misuse when the context is given;
    ///         `mvfx_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_HullCenter(mvfxContext* context, mvfxMeshId mesh,
                                                           bool* hullOut, float centerOut[3]);

// The most vertices of a hull mvfxMesh_HullVertexCount counts.
#define MVFX_MAX_HULL_COUNT 500

    /// Counts the vertices of the convex hull of a mesh's vertices, past
    /// the MVFX_MAX_HULL_VERTICES a rigid hull particle takes: an editor
    /// tells an artist by how much to simplify a mesh refused for one.
    ///
    /// @param context   The context that owns the mesh.
    /// @param mesh      The mesh.
    /// @param countOut  Receives the count: 0 for vertices that span no
    ///                  volume, which have no hull; 0 on failure, so an
    ///                  editor given `mvfx_errorLimit` reports "more than
    ///                  MVFX_MAX_HULL_COUNT", never the 0.
    /// @return `mvfx_success`, 0 among its counts; `mvfx_errorStale` for an
    ///         id that names no live mesh; `mvfx_errorInvalid` for a NULL
    ///         argument, which counts as misuse when the context is given;
    ///         `mvfx_errorLimit` for a hull past what a count holds,
    ///         never fewer than MVFX_MAX_HULL_COUNT vertices (some hulls of
    ///         a few more are still counted); `mvfx_errorCapacity` when the
    ///         allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_HullVertexCount(mvfxContext* context,
                                                                mvfxMeshId mesh, int32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif
