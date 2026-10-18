// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every analytic spawn shape, arc mode and velocity mode, planned instances
// on a real device (lavapipe on CI) beside the CPU path's, compared after
// every step (rhi_harness.h): the point, line, circle, hemisphere, box and
// cone by their thickness and volume, turned and offset; arcs drawn,
// looping, going and coming and spread over a burst, after a start delay
// and at ages past binary32's ticks; velocities from the center, blended
// toward random directions. Skips (77) without an adapter, unless
// MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite on a shape of a kind, turned a little about each axis and
// offset, under a moving instance.
static mvfxEmitterDef OnShape(mvfxShapeKind kind)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.shape = (mvfxShape){.kind = kind,
                                .rotation = {0.2f, -0.1f, 0.3f, 0.9273618f},
                                .offset = {0.1f, -0.2f, 0.3f},
                                .radius = 0.5f,
                                .length = 1.5f,
                                .halfExtents = {0.4f, 0.25f, 0.6f},
                                .angle = 0.5f,
                                .arc = 6.28f};
    return emitter;
}

static void Compared(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, const char* what)
{
    Compare(gpu, kernels, &(Run){.emitters = emitter, .emitterCount = 1, .moving = true}, what);
}

static void TestShapes(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = OnShape(mvfx_shapePoint);
    Compared(gpu, kernels, &emitter, "a point");
    emitter = OnShape(mvfx_shapeLine);
    Compared(gpu, kernels, &emitter, "a line");
    emitter = OnShape(mvfx_shapeCircle);
    emitter.shape.thickness = 0.5f;
    emitter.shape.arc = 3.0f;
    Compared(gpu, kernels, &emitter, "a half ring's part of a circle");
    emitter = OnShape(mvfx_shapeHemisphere);
    emitter.shape.thickness = 0.3f;
    Compared(gpu, kernels, &emitter, "a hemisphere's shell");
    // A box through its volume, on its surface, and in a shell.
    emitter = OnShape(mvfx_shapeBox);
    emitter.shape.thickness = 1.0f;
    Compared(gpu, kernels, &emitter, "a box's volume");
    emitter.shape.thickness = 0.0f;
    Compared(gpu, kernels, &emitter, "a box's surface");
    emitter.shape.thickness = 0.25f;
    Compared(gpu, kernels, &emitter, "a box's shell");
    // A flat box's surface: its faces of no area are never chosen.
    emitter.shape.thickness = 0.0f;
    emitter.shape.halfExtents[1] = 0.0f;
    Compared(gpu, kernels, &emitter, "a flat box's surface");
    // A cone on its base, through its volume, and from an apex.
    emitter = OnShape(mvfx_shapeCone);
    emitter.shape.thickness = 0.4f;
    Compared(gpu, kernels, &emitter, "a cone's base");
    emitter.shape.coneVolume = true;
    Compared(gpu, kernels, &emitter, "a cone's volume");
    // From an apex: through the volume, its radius growing from 0, and on
    // the base of radius 0, every direction within the cone's angle.
    emitter.shape.radius = 0.0f;
    Compared(gpu, kernels, &emitter, "a cone's volume from its apex");
    emitter.shape.coneVolume = false;
    Compared(gpu, kernels, &emitter, "a cone from its apex");
}

static void TestArcs(Gpu* gpu, mvfxGpu* kernels)
{
    // A circle's arc looping, going and coming, and spread over its burst,
    // after a start delay its age is from.
    mvfxEmitterDef emitter = OnShape(mvfx_shapeCircle);
    emitter.startDelay = 0.05f;
    emitter.shape.arcMode = mvfx_arcLoop;
    emitter.shape.arcSpeed = 2.5f;
    Compared(gpu, kernels, &emitter, "a looping arc");
    emitter.shape.arcMode = mvfx_arcPingPong;
    emitter.shape.arcSpeed = 1.7f;
    Compared(gpu, kernels, &emitter, "an arc going and coming");
    emitter.shape.arcMode = mvfx_arcBurstSpread;
    Compared(gpu, kernels, &emitter, "an arc spread over a burst");
    // A cone and a line read the arc as well.
    emitter = OnShape(mvfx_shapeCone);
    emitter.shape.arcMode = mvfx_arcPingPong;
    emitter.shape.arcSpeed = 0.75f;
    emitter.shape.arc = 2.0f;
    Compared(gpu, kernels, &emitter, "a cone's arc going and coming");
    emitter = OnShape(mvfx_shapeLine);
    emitter.shape.arcMode = mvfx_arcLoop;
    emitter.shape.arcSpeed = 0.3f;
    Compared(gpu, kernels, &emitter, "a line looping");
    // Steps of a second, 40 of them: the births the last steps keep are
    // past 2^24 ticks old (16 s), past binary32's integers, at nearly the
    // fastest arc speed, the arc's fraction from the exact product of
    // ticks and speed (binary32 turns would be off by a hundredth).
    SetStepTicks(MVFX_TICKS_PER_SECOND);
    emitter = OnShape(mvfx_shapeCircle);
    emitter.rate = 200.0f;
    emitter.burstCount = 0;
    emitter.shape.arcMode = mvfx_arcLoop;
    emitter.shape.arcSpeed = 997.3f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 20},
            "a fast looping arc past 2^24 ticks");
    emitter.shape.arcMode = mvfx_arcPingPong;
    emitter.shape.arcSpeed = 0.0123f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 20},
            "a slow arc going and coming past 2^24 ticks");
    SetStepTicks(STEP_TICKS);
}

static void TestVelocities(Gpu* gpu, mvfxGpu* kernels)
{
    // From the center of a box's volume, and of a point, whose place is
    // the center: a random direction instead.
    mvfxEmitterDef emitter = OnShape(mvfx_shapeBox);
    emitter.shape.thickness = 1.0f;
    emitter.velocity.mode = mvfx_directionFromCenter;
    Compared(gpu, kernels, &emitter, "velocities from a box's center");
    emitter = OnShape(mvfx_shapePoint);
    emitter.velocity.mode = mvfx_directionFromCenter;
    Compared(gpu, kernels, &emitter, "velocities from a point's center");
    // Blended toward a random direction: half way from the cone, and all
    // the way from a fixed one.
    emitter = OnShape(mvfx_shapeHemisphere);
    emitter.velocity.randomize = 0.5f;
    Compared(gpu, kernels, &emitter, "cone velocities half random");
    emitter.velocity.mode = mvfx_directionFixed;
    emitter.velocity.randomize = 1.0f;
    Compared(gpu, kernels, &emitter, "fixed velocities all random");
    emitter.velocity.mode = mvfx_directionShapeNormal;
    emitter.velocity.randomize = 0.25f;
    Compared(gpu, kernels, &emitter, "normal velocities a quarter random");
    // Along the shapes' own normals, which only velocities read: a box's
    // volume's nearest face, a cone's slant on its base, through its volume
    // and from its apex.
    emitter = OnShape(mvfx_shapeBox);
    emitter.shape.thickness = 1.0f;
    emitter.velocity.mode = mvfx_directionShapeNormal;
    Compared(gpu, kernels, &emitter, "velocities along a box's volume's normals");
    emitter = OnShape(mvfx_shapeCone);
    emitter.shape.thickness = 0.4f;
    emitter.velocity.mode = mvfx_directionShapeNormal;
    Compared(gpu, kernels, &emitter, "velocities along a cone base's slant");
    emitter.shape.coneVolume = true;
    Compared(gpu, kernels, &emitter, "velocities along a cone volume's slant");
    emitter.shape.radius = 0.0f;
    emitter.shape.coneVolume = false;
    Compared(gpu, kernels, &emitter, "velocities from a cone's apex");
}

// A box of uneven sides with a center: its bottom, top and front faces,
// a triangle of no area (vertex 9 on the edge from 0 to 1) and
// tetrahedra, one of no volume; then the same moved, and a larger mesh, a
// bumpy sheet of 5 by 4 vertices.
static const float s_box[] = {0, 0, 0,    1, 0, 0,    0, 2, 0,    1,    2, 0,     0, 0, 0.5f,
                              1, 0, 0.5f, 0, 2, 0.5f, 1, 2, 0.5f, 0.5f, 1, 0.25f, 2, 0, 0};
static const uint32_t s_boxTriangles[] = {0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7,
                                          6, 0, 1, 4, 1, 5, 4, 0, 1, 9};
static const uint32_t s_boxTetrahedra[] = {0, 1, 2, 4, 1, 3, 2, 7, 8, 4, 5, 6, 0, 1, 9, 4};

static mvfxMeshDef BoxMesh(void)
{
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = s_box;
    def.vertexCount = 10;
    def.triangles = s_boxTriangles;
    def.triangleCount = 7;
    def.tetrahedra = s_boxTetrahedra;
    def.tetrahedronCount = 4;
    return def;
}

// The bumpy sheet: 5 by 4 vertices, two triangles a cell.
static mvfxMeshDef SheetMesh(void)
{
    static float sheet[3 * 20];
    static uint32_t sheetTriangles[3 * 24];
    for (int32_t v = 0; v < 20; ++v)
    {
        sheet[3 * v] = (float)(v % 5) * 0.4f;
        sheet[3 * v + 1] = (float)(v / 5) * 0.3f;
        sheet[3 * v + 2] = 0.1f * (float)((v * 7) % 5);
    }
    for (int32_t q = 0; q < 12; ++q)
    {
        uint32_t corner = (uint32_t)(q / 4 * 5 + q % 4);
        const uint32_t two[6] = {corner,     corner + 1, corner + 5,
                                 corner + 1, corner + 6, corner + 5};
        for (int32_t k = 0; k < 6; ++k)
        {
            sheetTriangles[6 * q + k] = two[k];
        }
    }
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = sheet;
    def.vertexCount = 20;
    def.triangles = sheetTriangles;
    def.triangleCount = 24;
    return def;
}

static void TestMeshes(Gpu* gpu, mvfxGpu* kernels)
{
    // Each mode: the vertices, the edges by length, the surface by area,
    // the volume by volume, the elements of no measure never chosen.
    static const mvfxMeshMode modes[4] = {mvfx_meshVertices, mvfx_meshEdges, mvfx_meshSurface,
                                          mvfx_meshVolume};
    static const char* const names[4] = {"a mesh's vertices", "a mesh's edges", "a mesh's surface",
                                         "a mesh's volume"};
    mvfxMeshDef box = BoxMesh();
    for (int32_t m = 0; m < 4; ++m)
    {
        mvfxEmitterDef emitter = OnShape(mvfx_shapeMesh);
        emitter.shape.meshMode = modes[m];
        Compare(gpu, kernels,
                &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .mesh = &box},
                names[m]);
    }
    // Velocities along the blended normals of the surface and the edges.
    mvfxEmitterDef emitter = OnShape(mvfx_shapeMesh);
    emitter.shape.meshMode = mvfx_meshSurface;
    emitter.velocity.mode = mvfx_directionShapeNormal;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .mesh = &box},
            "velocities along a mesh surface's normals");
    emitter.shape.meshMode = mvfx_meshEdges;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .mesh = &box},
            "velocities along a mesh edge's normals");
    // At half time the mesh moves (a skinned mesh's new positions, its
    // areas changed), and then another, larger mesh is bound, in room the
    // simulation keeps for it.
    static float moved[30];
    for (int32_t k = 0; k < 30; ++k)
    {
        moved[k] = s_box[k] * (k % 3 == 2 ? 3.0f : 0.5f) + 0.25f;
    }
    emitter = OnShape(mvfx_shapeMesh);
    emitter.shape.meshMode = mvfx_meshSurface;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .mesh = &box,
                   .laterPositions = moved},
            "a mesh moved at half time");
    mvfxMeshDef larger = SheetMesh();
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .mesh = &box,
                   .laterMesh = &larger,
                   .hostMeshBytes = 2048},
            "a larger mesh bound at half time");
}

static void TestMeshRoom(mvfxGpu* kernels)
{
    // A mesh larger than the room the simulation keeps is refused when
    // bound through it, the binding unchanged; one that fits is bound; one
    // of another context's emitter or no mesh shape is refused.
    mvfxMeshDef box = BoxMesh();
    mvfxEmitterDef emitters[2] = {OnShape(mvfx_shapeMesh), Sprite()};
    emitters[0].shape.meshMode = mvfx_meshSurface;
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim =
        OpenRun(kernels, &(Run){.emitters = emitters, .emitterCount = 2, .mesh = &box}, &effect);
    mvfxContext* context = RunContext();
    // The sheet's surface takes 976 bytes, past the box's 396; a single
    // triangle's 108.
    mvfxMeshDef largerDef = SheetMesh();
    static const float corners[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    static const uint32_t triangle[] = {0, 1, 2};
    mvfxMeshDef smallerDef = mvfxDefaultMeshDef();
    smallerDef.positions = corners;
    smallerDef.vertexCount = 3;
    smallerDef.triangles = triangle;
    smallerDef.triangleCount = 1;
    mvfxMeshId larger = {0};
    mvfxMeshId smaller = {0};
    CHECK(mvfxCreateMesh(context, &largerDef, &larger) == mvfx_success &&
              mvfxCreateMesh(context, &smallerDef, &smaller) == mvfx_success,
          "meshes");
    int64_t room = 0;
    int64_t largerBytes = 0;
    int64_t smallerBytes = 0;
    mvfxMeshView before;
    CHECK(mvfxGpuSim_MeshBytes(context, larger, mvfx_meshSurface, &largerBytes) == mvfx_success &&
              mvfxGpuSim_MeshBytes(context, smaller, mvfx_meshSurface, &smallerBytes) ==
                  mvfx_success &&
              largerBytes == 976 && smallerBytes == 108 &&
              mvfxInstance_GetEmitterMesh(context, PlannedInstance(), 0, &before) == mvfx_success,
          "the bytes");
    mvfxMeshView after;
    CHECK(mvfxGpuSim_SetEmitterMesh(sim, context, 0, larger) == mvfx_errorCapacity &&
              mvfxInstance_GetEmitterMesh(context, PlannedInstance(), 0, &after) == mvfx_success &&
              after.serial == before.serial,
          "a mesh past the room refused, unbound");
    CHECK(mvfxGpuSim_SetEmitterMesh(sim, context, 0, smaller) == mvfx_success &&
              mvfxInstance_GetEmitterMesh(context, PlannedInstance(), 0, &after) == mvfx_success &&
              after.serial != before.serial && after.triangleCount == 1,
          "a mesh within the room bound");
    CHECK(mvfxGpuSim_SetEmitterMesh(sim, context, 1, smaller) == mvfx_errorInvalid &&
              mvfxGpuSim_SetEmitterMesh(sim, context, 2, smaller) == mvfx_errorInvalid &&
              mvfxGpuSim_SetEmitterMesh(sim, NULL, 0, smaller) == mvfx_errorInvalid &&
              mvfxGpuSim_SetEmitterMesh(NULL, context, 0, smaller) == mvfx_errorInvalid &&
              mvfxGpuSim_SetEmitterMesh(sim, context, 0, (mvfxMeshId){0}) == mvfx_errorStale &&
              mvfxGpuSim_MeshBytes(context, (mvfxMeshId){0}, mvfx_meshSurface, &room) ==
                  mvfx_errorStale &&
              mvfxGpuSim_MeshBytes(context, smaller, (mvfxMeshMode)9, &room) == mvfx_errorInvalid,
          "no mesh shape, no emitter, no context, no simulation, a stale mesh, a bad mode");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestShapes(gpu, kernels);
    TestArcs(gpu, kernels);
    TestVelocities(gpu, kernels);
    TestMeshes(gpu, kernels);
    TestMeshRoom(kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
