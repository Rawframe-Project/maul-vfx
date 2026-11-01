// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene of vfx_scene.h run as the determinism tests step it: its
// instances moved along a fixed path, its scene queries answered, waters
// pushed, and a hash of every instance's state, read through the
// library's internal records.

#ifndef MAUL_VFX_TEST_VFX_SCENE_RUN_H
#define MAUL_VFX_TEST_VFX_SCENE_RUN_H

#include "context.h"
#include "instance.h"
#include "vfx_scene.h"

// Moves every instance of the context before a step: along a fixed path
// of exact values (quaternions of exact floats, scales of 1 and 1.5), with
// a teleport far enough at step 10 for world-space anchors to rebase.
// A depth image of 32 pixels a side: a camera 6 above an instance looking
// down its -Z at a plane tilted along X through its origin, 90 degrees
// wide, depth from 0.1 to 100.
#define SCENE_DEPTH_SIDE 32
static float s_sceneDepth[SCENE_DEPTH_SIDE * SCENE_DEPTH_SIDE];

static inline mvfxDepthImage SceneDepth(const double origin[3])
{
    const float a = (float)(100.0 / (0.1 - 100.0));
    for (int32_t r = 0; r < SCENE_DEPTH_SIDE; ++r)
    {
        for (int32_t c = 0; c < SCENE_DEPTH_SIDE; ++c)
        {
            // Along the ray (x, y, -1) s from the camera: 6 - s = 0.4 x s.
            float x = ((float)c + 0.5f) / (float)SCENE_DEPTH_SIDE * 2.0f - 1.0f;
            float s = 6.0f / (1.0f + 0.4f * x);
            s_sceneDepth[r * SCENE_DEPTH_SIDE + c] = (a * -s + a * 0.1f) / s;
        }
    }
    mvfxDepthImage image = {
        .pixels = s_sceneDepth,
        .byteCount = (int64_t)sizeof s_sceneDepth,
        .width = SCENE_DEPTH_SIDE,
        .height = SCENE_DEPTH_SIDE,
        .stride = SCENE_DEPTH_SIDE * 4,
        .format = mvfx_depthFloat32,
        .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, a, -1, 0, 0, a * 0.1f, 0},
        .origin = {origin[0], origin[1], origin[2] + 6.0},
        .rotation = {0, 0, 0, 1},
    };
    return image;
}

// Answers every scene query of the context as a host's scene might: a
// wall halfway along the segment of every third particle by serial, of
// every other one from step 8, facing back along it; nothing for the rest.
static inline void AnswerScene(mvfxContext* context, int32_t step)
{
    uint32_t every = step < 8 ? 3u : 2u;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        const mvfxInstance* instance = context->instances.slots[i].object;
        if (instance == NULL)
        {
            continue;
        }
        mvfxInstanceId id = {i + 1, context->instances.slots[i].generation};
        for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
        {
            const mvfxSceneQuery* queries = NULL;
            int32_t count = 0;
            CHECK(mvfxInstance_GetSceneQueries(context, id, e, &queries, &count) == mvfx_success,
                  "scene queries");
            mvfxSceneAnswer answers[32] = {0};
            for (int32_t q = 0; q < count && q < 32; ++q)
            {
                const mvfxSceneQuery* query = &queries[q];
                const float* d = query->segment;
                answers[q] = (mvfxSceneAnswer){
                    .hit = query->serial % every == 0,
                    .point = {query->origin[0] + 0.5 * (double)d[0],
                              query->origin[1] + 0.5 * (double)d[1],
                              query->origin[2] + 0.5 * (double)d[2]},
                    .normal = {-d[0], -d[1], -d[2]},
                };
            }
            CHECK(mvfxInstance_AnswerSceneQueries(context, id, e, answers, count) == mvfx_success,
                  "scene answered");
        }
    }
}

// The scene's water pushed: a dent crossing the pond, and a lift on the
// strip where it was put every fourth step.
static inline void PushSceneWater(mvfxContext* context, mvfxInstanceId id, const double origin[3],
                                  int32_t step)
{
    const mvfxWaterPush dent = {.point = {origin[0] - 0.8 + 0.1 * step, origin[1], origin[2] + 0.2},
                                .radius = 0.15f,
                                .depth = 0.02f};
    CHECK(mvfxInstance_PushWater(context, id, 0, &dent, 1) == mvfx_success, "pond push");
    mvfxWaterInfo info = {0};
    CHECK(mvfxInstance_GetWaterInfo(context, id, 1, &info) == mvfx_success, "strip info");
    const mvfxWaterPush lift = {
        .point = {info.center[0] + 1.5 - 0.25 * step, info.center[1], info.center[2]},
        .radius = 0.4f,
        .depth = -0.01f};
    CHECK(mvfxInstance_PushWater(context, id, 1, &lift, step % 4 == 0 ? 1 : 0) == mvfx_success,
          "strip push");
    // A paddle through the shallow water every third step.
    const mvfxWaterPush paddle = {
        .point = {origin[0] - 0.6 + 0.05 * step, origin[1] - 0.5, origin[2]},
        .radius = 0.2f,
        .depth = step % 2 == 0 ? 0.05f : -0.03f};
    CHECK(mvfxInstance_PushWater(context, id, 2, &paddle, step % 3 == 0 ? 1 : 0) == mvfx_success,
          "paddle push");
}

static inline void MoveScene(mvfxContext* context, int32_t step)
{
    AnswerScene(context, step);
    static const float turns[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.6f, 0.8f}};
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        if (context->instances.slots[i].object == NULL)
        {
            continue;
        }
        mvfxInstanceId id = {i + 1, context->instances.slots[i].generation};
        double far = step >= 10 ? 3000.0 : 0.0;
        const double origin[3] = {far + 2.5 * step + (double)i, 0.125 * step * step, -1.0 * step};
        CHECK(mvfxInstance_SetTransform(context, id, origin, turns[step % 2],
                                        step % 3 == 0 ? 1.5f : 1.0f, step == 10) == mvfx_success,
              "scene moved");
        const mvfxEffect* effect =
            ((const mvfxInstance*)context->instances.slots[i].object)->effect;
        // The gases' host obstacle: a sphere in the instance's frame
        // crossing the 3D gas, so it moves through the box.
        if (effect->gasCount > 0)
        {
            const mvfxCollider ball = {.kind = mvfx_colliderSphere,
                                       .space = mvfx_spaceLocal,
                                       .center = {-0.3f + 0.04f * (float)step, 0.45f, 0.05f},
                                       .radius = 0.15f};
            CHECK(mvfxInstance_SetColliders(context, id, &ball, 1) == mvfx_success, "gas ball");
        }
        if (effect->waterCount > 0)
        {
            PushSceneWater(context, id, origin, step);
        }
        if (step == 5 && effect->emitterCount == 5 && effect->parameterCount > 0)
        {
            static const float aimed[4] = {-2.0f, 3.0f, 1.0f, 0.0f};
            CHECK(mvfxInstance_SetParameter(context, id, 0, aimed) == mvfx_success, "scene aim");
        }
        if (step == 6)
        {
            CHECK(mvfxInstance_BreakRibbons(context, id, -1) == mvfx_success, "scene break");
        }
        // Banners' frames: one swinging round the instance, one unset until
        // step 4.
        for (int32_t e = 0; e < effect->emitterCount; ++e)
        {
            if (effect->emitters[e].unit.attach != mvfx_attachHost)
            {
                continue;
            }
            const mvfxAttachFrame frames[2] = {
                {.position = {origin[0] + 1.0, origin[1] + 0.25 * step, origin[2]},
                 .rotation = {0.0f, 0.6f, 0.0f, 0.8f}},
                {.position = {origin[0] - 2.0, origin[1], origin[2] + 1.0},
                 .rotation = {0.0f, 0.0f, 0.0f, 1.0f}}};
            CHECK(mvfxInstance_SetAttachFrames(context, id, e, frames, step < 4 ? 1 : 2) ==
                      mvfx_success,
                  "scene frames");
            // A banner on frame 1 before it is set (held), and after.
            static const int32_t second = 1;
            mvfxSpawnRequest request = {
                .count = 1, .positions = frames[1].position, .attachFrames = &second};
            if (step == 2 || step == 6)
            {
                CHECK(mvfxInstance_Spawn(context, id, e, &request) == mvfx_success, "scene banner");
            }
        }
        // The throttled unseen over the longest step, the paused for three.
        const mvfxInstance* live = context->instances.slots[i].object;
        bool hidden = (live->offscreen == mvfx_offscreenThrottle && step >= 3 && step < 15) ||
                      (live->offscreen == mvfx_offscreenPause && step >= 7 && step < 10);
        CHECK(mvfxInstance_SetVisible(context, id, !hidden) == mvfx_success, "scene seen");
        mvfxDepthImage depth = SceneDepth(origin);
        CHECK(mvfxInstance_SetDepthImage(context, id, &depth) == mvfx_success, "scene depth");
    }
}

// A record of every field, in many formats: what the scene's output
// writes for a view.
static const mvfxLayoutField s_sceneFields[22] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 3, .offset = 12},
    {.field = mvfx_recordColor,
     .format = mvfx_formatUnorm8,
     .components = 4,
     .srgb = true,
     .offset = 18},
    {.field = mvfx_recordAngle, .format = mvfx_formatFloat32, .components = 1, .offset = 24},
    {.field = mvfx_recordOrientation, .format = mvfx_formatSnorm16, .components = 4, .offset = 28},
    {.field = mvfx_recordVelocity, .format = mvfx_formatFloat16, .components = 3, .offset = 36},
    {.field = mvfx_recordAge, .format = mvfx_formatUnorm16, .components = 1, .offset = 42},
    {.field = mvfx_recordFrame, .format = mvfx_formatUint32, .components = 1, .offset = 44},
    {.field = mvfx_recordCustom, .format = mvfx_formatSnorm8, .components = 4, .offset = 48},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 52},
    {.field = mvfx_recordMesh, .format = mvfx_formatUint32, .components = 1, .offset = 56},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat16, .components = 3, .offset = 60},
    {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 68},
    {.field = mvfx_recordLength, .format = mvfx_formatUnorm16, .components = 1, .offset = 72},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 76},
    {.field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 1, .offset = 80},
    {.field = mvfx_recordSide, .format = mvfx_formatSnorm16, .components = 3, .offset = 84},
    {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 3, .offset = 92},
    {.field = mvfx_recordCorner, .format = mvfx_formatUnorm16, .components = 2, .offset = 104},
    {.field = mvfx_recordNeighborCount,
     .format = mvfx_formatUint32,
     .components = 1,
     .offset = 108},
    {.field = mvfx_recordNeighborCenter,
     .format = mvfx_formatFloat32,
     .components = 3,
     .offset = 112},
    {.field = mvfx_recordNeighborVelocity,
     .format = mvfx_formatFloat16,
     .components = 3,
     .offset = 124},
};
#define SCENE_RECORD_BYTES 132
#define SCENE_RECORDS      128

// Hashes an emitter's ribbons for a view: their points and list.
static inline uint64_t HashRibbons(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                   mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                   uint64_t hash)
{
    // Room for every particle's segment cut into the most pieces.
    enum
    {
        POINTS = SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES
    };
    static _Alignas(16) unsigned char points[POINTS * SCENE_RECORD_BYTES];
    static mvfxRibbon ribbons[SCENE_RECORDS];
    mvfxRibbonBuffers buffers = {.points = points,
                                 .pointCapacity = POINTS,
                                 .ribbons = ribbons,
                                 .ribbonCapacity = SCENE_RECORDS,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    int64_t pointCount = 0;
    int64_t ribbonCount = 0;
    CHECK(mvfxInstance_WriteRibbons(context, id, emitter, layout, view, &buffers, &pointCount,
                                    &ribbonCount) == mvfx_success &&
              pointCount <= POINTS,
          "scene ribbons");
    hash = mvfxHash64(hash, points, (int64_t)pointCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, ribbons, ribbonCount * (int64_t)sizeof ribbons[0]);
}

// Hashes an emitter's quads for a view: their vertices and indices.
static inline uint64_t HashQuads(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                 mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                 uint64_t hash)
{
    static _Alignas(16) unsigned char vertices[4 * SCENE_RECORDS * SCENE_RECORD_BYTES];
    static uint32_t indices[6 * SCENE_RECORDS];
    mvfxExpandBuffers buffers = {.vertices = vertices,
                                 .vertexCapacity = 4 * SCENE_RECORDS,
                                 .indices = indices,
                                 .indexCapacity = 6 * SCENE_RECORDS,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    int64_t count = 0;
    CHECK(mvfxInstance_ExpandSprites(context, id, emitter, layout, view, &buffers, &count) ==
                  mvfx_success &&
              count <= SCENE_RECORDS,
          "scene quads");
    hash = mvfxHash64(hash, vertices, 4 * (int64_t)count * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, 6 * count * (int64_t)sizeof indices[0]);
}

// Hashes an emitter's ribbons expanded into strips for a view.
// Ribbons as capped tubes of three sides.
static inline uint64_t HashTubes(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                 mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                 uint64_t hash)
{
    enum
    {
        VERTICES = 4 * SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES + 10 * SCENE_RECORDS,
        INDICES = 18 * SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES + 18 * SCENE_RECORDS
    };
    static _Alignas(16) unsigned char vertices[VERTICES * SCENE_RECORD_BYTES];
    static uint32_t indices[INDICES];
    mvfxExpandBuffers buffers = {.vertices = vertices,
                                 .vertexCapacity = VERTICES,
                                 .indices = indices,
                                 .indexCapacity = INDICES,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    static const mvfxTubeDef tube = {.sides = 3, .caps = true};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandTubes(context, id, emitter, layout, view, &tube, &buffers,
                                   &vertexCount, &indexCount) == mvfx_success &&
              vertexCount <= VERTICES && indexCount <= INDICES,
          "scene tubes");
    hash = mvfxHash64(hash, vertices, (int64_t)vertexCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, indexCount * (int64_t)sizeof indices[0]);
}

static inline uint64_t HashStrips(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                  mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                  uint64_t hash)
{
    enum
    {
        VERTICES = 2 * SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES
    };
    static _Alignas(16) unsigned char vertices[VERTICES * SCENE_RECORD_BYTES];
    static uint32_t indices[3 * VERTICES];
    mvfxExpandBuffers buffers = {.vertices = vertices,
                                 .vertexCapacity = VERTICES,
                                 .indices = indices,
                                 .indexCapacity = 3 * VERTICES,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandRibbons(context, id, emitter, layout, view, &buffers, &vertexCount,
                                     &indexCount) == mvfx_success &&
              vertexCount <= VERTICES && indexCount <= 3 * VERTICES,
          "scene strips");
    hash = mvfxHash64(hash, vertices, (int64_t)vertexCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, indexCount * (int64_t)sizeof indices[0]);
}

// Hashes an emitter's plexus links for a view, as points and as strips.
static inline uint64_t HashPlexus(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                  mvfxLayoutId layout, const mvfxView* view, uint64_t hash)
{
    enum
    {
        LINKS = 64
    };
    static _Alignas(16) unsigned char points[4 * LINKS * SCENE_RECORD_BYTES];
    static mvfxRibbon ribbons[LINKS];
    static uint32_t indices[6 * LINKS];
    mvfxRibbonBuffers buffers = {
        .points = points, .pointCapacity = 2 * LINKS, .ribbons = ribbons, .ribbonCapacity = LINKS};
    int64_t pointCount = 0;
    int64_t linkCount = 0;
    // The layout writes its fields only: the bytes between them are the
    // buffer's, cleared so the hash is of what this call wrote (left from
    // earlier calls they differed by platform).
    memset(points, 0, sizeof points);
    memset(ribbons, 0, sizeof ribbons);
    CHECK(mvfxInstance_WritePlexus(context, id, emitter, layout, view, &buffers, &pointCount,
                                   &linkCount) == mvfx_success &&
              linkCount <= LINKS,
          "scene plexus");
    hash = mvfxHash64(hash, points, (int64_t)pointCount * SCENE_RECORD_BYTES);
    hash = mvfxHash64(hash, ribbons, linkCount * (int64_t)sizeof ribbons[0]);
    memset(points, 0, sizeof points);
    mvfxExpandBuffers strips = {.vertices = points,
                                .vertexCapacity = 4 * LINKS,
                                .indices = indices,
                                .indexCapacity = 6 * LINKS};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandPlexus(context, id, emitter, layout, view, &strips, &vertexCount,
                                    &indexCount) == mvfx_success,
          "scene plexus strips");
    hash = mvfxHash64(hash, points, (int64_t)vertexCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, indexCount * (int64_t)sizeof indices[0]);
}

// Hashes every emitter's records of an instance for a view at (1500, 3,
// -2), through a layout made on the context the first time.
static inline uint64_t HashOutput(mvfxContext* context, mvfxInstanceId id,
                                  const mvfxInstance* instance, uint64_t hash)
{
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = s_sceneFields;
    def.fieldCount = 22;
    def.stride = SCENE_RECORD_BYTES;
    mvfxLayoutId layout = {1, 1};
    if (!mvfxLayout_IsValid(context, layout))
    {
        CHECK(mvfxCreateLayout(context, &def, &layout) == mvfx_success, "scene layout");
    }
    // The eye among the first instances looks down and a little along x.
    static const mvfxView view = {.origin = {1500.0, 3.0, -2.0},
                                  .eye = {12.0, 1.0, 2.0},
                                  .forward = {0.25f, -0.125f, -1.0f},
                                  .up = {0.0f, 1.0f, 0.25f}};
    static _Alignas(16) unsigned char records[SCENE_RECORDS * SCENE_RECORD_BYTES];
    static uint32_t scratch[SCENE_RECORDS * 4];
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        int64_t count = 0;
        CHECK(mvfxInstance_WriteParticles(context, id, e, layout, &view, scratch, sizeof scratch,
                                          records, SCENE_RECORDS, &count) == mvfx_success &&
                  count <= SCENE_RECORDS,
              "scene output");
        hash = mvfxHash64(hash, records, (int64_t)count * SCENE_RECORD_BYTES);
        hash = HashRibbons(context, id, e, layout, &view, scratch, hash);
        hash = HashQuads(context, id, e, layout, &view, scratch, hash);
        hash = HashStrips(context, id, e, layout, &view, scratch, hash);
        hash = HashTubes(context, id, e, layout, &view, scratch, hash);
        hash = HashPlexus(context, id, e, layout, &view, hash);
        static mvfxLight lights[SCENE_RECORDS];
        CHECK(mvfxInstance_WriteLights(context, id, e, &view, lights, SCENE_RECORDS, &count) ==
                      mvfx_success &&
                  count <= SCENE_RECORDS,
              "scene lights");
        hash = mvfxHash64(hash, lights, count * (int64_t)sizeof lights[0]);
    }
    return hash;
}

// A hash of every live instance of the context, in slot order, and of
// its output.
// An instance's water surfaces as the host reads them: the pond sampled
// across it and past its edge, and the strip's normals in unorm16.
static inline uint64_t HashWater(mvfxContext* context, mvfxInstanceId id,
                                 const mvfxInstance* instance, uint64_t hash)
{
    if (instance->effect->waterCount == 0)
    {
        return hash;
    }
    const double* at = instance->to.origin;
    const double points[9] = {at[0] - 0.5,  at[1],       at[2] + 0.1, at[0] + 0.33, at[1],
                              at[2] - 0.41, at[0] + 1.3, at[1],       at[2]};
    mvfxWaterSample samples[3];
    memset(samples, 0, sizeof samples);
    CHECK(mvfxInstance_SampleWater(context, id, 0, points, 3, samples) == mvfx_success,
          "scene water sampled");
    hash = mvfxHash64(hash, samples, (int64_t)sizeof samples);
    static uint16_t normals[64 * 16];
    const mvfxWaterRead read = {.field = mvfx_waterNormalX,
                                .format = mvfx_formatUnorm16,
                                .size = {64, 16},
                                .low = -0.01f,
                                .high = 0.01f};
    CHECK(mvfxInstance_ReadWater(context, id, 1, &read, normals, sizeof normals) == mvfx_success,
          "scene water read");
    hash = mvfxHash64(hash, normals, (int64_t)sizeof normals);
    // The shallow water's depths and flow along X, as binary32.
    static float depths[2][32 * 16];
    for (int32_t k = 0; k < 2; ++k)
    {
        const mvfxWaterRead shallow = {.field = k == 0 ? mvfx_waterDepth : mvfx_waterVelocityX,
                                       .format = mvfx_formatFloat32,
                                       .size = {32, 16}};
        CHECK(mvfxInstance_ReadWater(context, id, 2, &shallow, depths[k], sizeof depths[k]) ==
                  mvfx_success,
              "scene shallow read");
    }
    return mvfxHash64(hash, depths, (int64_t)sizeof depths);
}

static inline uint64_t HashScene(mvfxContext* context)
{
    uint64_t hash = MVFX_HASH_INIT;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        const mvfxInstance* instance = context->instances.slots[i].object;
        if (instance != NULL)
        {
            hash = mvfxHashInstance(instance, hash);
            mvfxInstanceId id = {i + 1, context->instances.slots[i].generation};
            hash = HashOutput(context, id, instance, hash);
            hash = HashWater(context, id, instance, hash);
            mvfxBounds bounds = {0};
            CHECK(mvfxInstance_GetBounds(context, id, -1, &bounds) == mvfx_success, "scene bounds");
            hash = mvfxHash64(hash, &bounds.empty, 1);
            hash = mvfxHash64(hash, bounds.min, (int64_t)sizeof bounds.min);
            hash = mvfxHash64(hash, bounds.max, (int64_t)sizeof bounds.max);
        }
    }
    // The draw order for a view looking down +x past a plane at x = 1490.
    static const double eye[3] = {1500.0, 3.0, -2.0};
    const mvfxFrustum frustum = {.planes = {{1, 0, 0, -1490}, {0, 1, 0, 40}}, .planeCount = 2};
    static mvfxDrawItem items[64];
    int64_t count = 0;
    CHECK(mvfxContext_OrderEmitters(context, eye, &frustum, items, 64, &count) == mvfx_success,
          "scene order");
    // The twins' six copies hold more than the buffer: its first 64.
    int64_t kept = count < 64 ? count : 64;
    hash = mvfxHash64(hash, &count, (int64_t)sizeof count);
    return mvfxHash64(hash, items, kept * (int64_t)sizeof items[0]);
}

#endif
