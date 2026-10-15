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
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def
    ///         without its cookie, a count out of range, an index past the
    ///         vertices or a value out of range, which counts as misuse when
    ///         the context is given; `mvfx_errorLimit` when the context holds
    ///         its limit of meshes; `mvfx_errorCapacity` when the allocator
    ///         fails.
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
    ///         count not the mesh's or a value out of range, which counts as
    ///         misuse when the context is given (the mesh is then unchanged).
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
    /// @param context    The context that owns the mesh.
    /// @param mesh       The mesh.
    /// @param centerOut  Receives x, y and z.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         mesh; `mvfx_errorInvalid` for a NULL argument or vertices that
    ///         span no volume or whose hull has more than
    ///         MVFX_MAX_HULL_VERTICES vertices, which counts as misuse when
    ///         the context is given; `mvfx_errorCapacity` when the allocator
    ///         fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_HullCenter(mvfxContext* context, mvfxMeshId mesh,
                                                           float centerOut[3]);

// The most vertices of a hull mvfxMesh_HullVertexCount counts.
#define MVFX_MAX_HULL_COUNT 500

    /// Counts the vertices of the convex hull of a mesh's vertices, past
    /// the MVFX_MAX_HULL_VERTICES a rigid hull particle takes: an editor
    /// tells an artist by how much to simplify a mesh refused for one.
    ///
    /// @param context   The context that owns the mesh.
    /// @param mesh      The mesh.
    /// @param countOut  Receives the count; 0 on failure.
    /// @return `mvfx_success`; `mvfx_errorStale` for an id that names no live
    ///         mesh; `mvfx_errorInvalid` for a NULL argument or vertices that
    ///         span no volume, which counts as misuse when the context is
    ///         given; `mvfx_errorLimit` for a hull of more than
    ///         MVFX_MAX_HULL_COUNT vertices (it may count somewhat more);
    ///         `mvfx_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_HullVertexCount(mvfxContext* context,
                                                                mvfxMeshId mesh, int32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif
