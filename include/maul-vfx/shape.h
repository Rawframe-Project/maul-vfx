// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Where an emitter's particles are born and how they set off: a spawn
// shape in the emitter's space, and an initial velocity. A point is
// uniform over its shape (by length, area or volume), drawn at the
// particle's serial.

#ifndef MAUL_VFX_SHAPE_H
#define MAUL_VFX_SHAPE_H

#include "maul-vfx/base.h"
#include "maul-vfx/mesh.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A value drawn uniformly from [min, max] for each particle; min equal to
    // max gives that value.
    typedef struct mvfxRange
    {
        float min;
        float max;
    } mvfxRange;

// The largest size or offset a shape may have, and the fastest speed a
// particle may set off at, in the emitter's units.
#define MVFX_MAX_EXTENT 1e6f

// The widest a cone's half angle may be, in radians, below a right angle.
#define MVFX_MAX_CONE_ANGLE 1.5f

    // The kind of a spawn shape. Each lies in its own space: centered on its
    // origin, its axis along +Z.
    typedef uint8_t mvfxShapeKind;
    enum
    {
        // Its origin.
        mvfx_shapePoint = 0,
        // A segment along X, length long, centered.
        mvfx_shapeLine = 1,
        // A circle of radius in the XY plane, over arc radians from +X.
        mvfx_shapeCircle = 2,
        // A sphere of radius.
        mvfx_shapeSphere = 3,
        // The half of a sphere of radius where Z is at least 0.
        mvfx_shapeHemisphere = 4,
        // A box of halfExtents.
        mvfx_shapeBox = 5,
        // A cone from a base of radius in the XY plane, opening by angle as it
        // rises length along Z; its base over arc radians from +X.
        mvfx_shapeCone = 6,
        // A mesh, in its own positions.
        mvfx_shapeMesh = 7,
    };

    // Where on a mesh particles are born.
    typedef uint8_t mvfxMeshMode;
    enum
    {
        // At its vertices, each alike; the normal the vertex's.
        mvfx_meshVertices = 0,
        // On its edges, by length; the normal blended along the edge.
        mvfx_meshEdges = 1,
        // On its triangles, by area; the normal blended over the triangle.
        mvfx_meshSurface = 2,
        // In its tetrahedra, by volume; the normal any way.
        mvfx_meshVolume = 3,
    };

    // Where along its arc (a circle's or a cone's angle, or a line's length)
    // a shape places each particle.
    typedef uint8_t mvfxArcMode;
    enum
    {
        // Anywhere, uniformly.
        mvfx_arcRandom = 0,
        // Around it at arcSpeed turns a second from the emitter's start, and
        // around again.
        mvfx_arcLoop = 1,
        // Around it at arcSpeed turns a second, then back.
        mvfx_arcPingPong = 2,
        // A burst's particles spread evenly over it, a rate's at its start.
        mvfx_arcBurstSpread = 3,
    };

    // A spawn shape. Sizes and offsets are at most MVFX_MAX_EXTENT, finite
    // and not negative where they are sizes; fields a kind does not read are
    // not checked.
    typedef struct mvfxShape
    {
        mvfxShapeKind kind;
        // Where the shape's origin lies in the emitter's space.
        float offset[3];
        // How the shape is turned in the emitter's space: a unit quaternion
        // (x, y, z, w), its length within 0.001 of 1.
        float rotation[4];
        // A circle's, a sphere's or a cone base's radius.
        float radius;
        // A line's length or a cone's height.
        float length;
        // A box's half sizes along X, Y and Z.
        float halfExtents[3];
        // A cone's half angle, from 0 to MVFX_MAX_CONE_ANGLE radians; its top
        // radius, radius plus length times its tangent, at most
        // MVFX_MAX_EXTENT.
        float angle;
        // How deep below the surface particles are born, as a share of the
        // size from 0 (on the surface, a circle's edge, a cone base's rim) to
        // 1 (anywhere inside, the whole disc): a sphere's or a box's shell, a
        // circle's or a cone's ring.
        float thickness;
        // A cone emits from its whole volume rather than its base.
        bool coneVolume;
        // A circle's or a cone's arc, above 0 and at most 2 pi radians.
        float arc;
        mvfxArcMode arcMode;
        // Turns a second for the loop and ping-pong arc modes, from 0 to
        // 1,000.
        float arcSpeed;
        // A mesh shape's mesh, which an instance may replace for itself
        // (mvfxInstance_SetEmitterMesh), and where on it; the mesh has
        // triangles for the edges and the surface, tetrahedra for the volume,
        // and may not be destroyed while the effect is alive.
        mvfxMeshId mesh;
        mvfxMeshMode meshMode;
    } mvfxShape;

    // Which way a particle sets off.
    typedef uint8_t mvfxDirectionMode;
    enum
    {
        // Along the shape's normal where the particle is born: outward from a
        // sphere, a circle's center or a box's face, +Y from a line, any way
        // from a point, along a cone's slant.
        mvfx_directionShapeNormal = 0,
        // Away from the shape's origin; any way from the origin itself.
        mvfx_directionFromCenter = 1,
        // Within coneAngle of direction, uniformly over the directions.
        mvfx_directionCone = 2,
        // Along direction.
        mvfx_directionFixed = 3,
    };

    // A particle's initial velocity: a direction times a speed.
    typedef struct mvfxVelocity
    {
        mvfxDirectionMode mode;
        // The cone's axis or the fixed direction in the emitter's space, not
        // of length 0; the library normalizes it.
        float direction[3];
        // The cone's half angle, from 0 to pi radians.
        float coneAngle;
        // How far toward a uniformly random direction the direction is
        // blended, from 0 to 1.
        float randomize;
        // The speed, drawn uniformly from the range: 0 <= min <= max <=
        // MVFX_MAX_EXTENT.
        mvfxRange speed;
    } mvfxVelocity;

#ifdef __cplusplus
}
#endif

#endif
