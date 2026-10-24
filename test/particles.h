// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's particles as the public output writes them, for the suites
// that check what a particle holds: every field of a particle's record,
// binary32 or uint32, for a view at the world's origin, in the pool's
// order (or the order the emitter's def asks for). The suites read
// particles through this, never through the library's internals.

#ifndef MAUL_VFX_TEST_PARTICLES_H
#define MAUL_VFX_TEST_PARTICLES_H

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// One particle's record.
typedef struct TestParticle
{
    float position[3];
    float size[3];
    float color[4];
    float angle;
    float orientation[4];
    float velocity[3];
    float age;
    uint32_t frame;
    // The emitter's first custom attribute.
    float custom[4];
    uint32_t serial;
    uint32_t mesh;
    uint32_t neighborCount;
    float neighborCenter[3];
    float neighborVelocity[3];
} TestParticle;

#define TEST_FIELD(member, kind, encoding, count)                                                  \
    {.field = (kind),                                                                              \
     .format = (encoding),                                                                         \
     .components = (count),                                                                        \
     .offset = (uint32_t)offsetof(TestParticle, member)}

// Writes up to capacity of an emitter's live particles into out, their
// positions less origin (the world's origin when NULL, which keeps a
// particle far from it to binary32's precision there), and returns how
// many it has; -1 when the output refuses.
static int32_t ReadParticlesFrom(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                 const double* origin, TestParticle* out, int32_t capacity)
{
    static const mvfxLayoutField fields[] = {
        TEST_FIELD(position, mvfx_recordPosition, mvfx_formatFloat32, 3),
        TEST_FIELD(size, mvfx_recordSize, mvfx_formatFloat32, 3),
        TEST_FIELD(color, mvfx_recordColor, mvfx_formatFloat32, 4),
        TEST_FIELD(angle, mvfx_recordAngle, mvfx_formatFloat32, 1),
        TEST_FIELD(orientation, mvfx_recordOrientation, mvfx_formatFloat32, 4),
        TEST_FIELD(velocity, mvfx_recordVelocity, mvfx_formatFloat32, 3),
        TEST_FIELD(age, mvfx_recordAge, mvfx_formatFloat32, 1),
        TEST_FIELD(frame, mvfx_recordFrame, mvfx_formatUint32, 1),
        TEST_FIELD(custom, mvfx_recordCustom, mvfx_formatFloat32, 4),
        TEST_FIELD(serial, mvfx_recordSerial, mvfx_formatUint32, 1),
        TEST_FIELD(mesh, mvfx_recordMesh, mvfx_formatUint32, 1),
        TEST_FIELD(neighborCount, mvfx_recordNeighborCount, mvfx_formatUint32, 1),
        TEST_FIELD(neighborCenter, mvfx_recordNeighborCenter, mvfx_formatFloat32, 3),
        TEST_FIELD(neighborVelocity, mvfx_recordNeighborVelocity, mvfx_formatFloat32, 3),
    };
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = fields;
    def.fieldCount = (int32_t)(sizeof fields / sizeof fields[0]);
    def.stride = (uint32_t)sizeof(TestParticle);
    mvfxLayoutId layout = {0};
    if (mvfxCreateLayout(context, &def, &layout) != mvfx_success)
    {
        return -1;
    }
    mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    for (int32_t k = 0; origin != NULL && k < 3; ++k)
    {
        view.origin[k] = origin[k];
        view.eye[k] = origin[k];
    }
    int64_t scratchBytes = 0;
    void* scratch = NULL;
    int64_t count = 0;
    bool written = mvfxInstance_GetSortScratchBytes(context, instance, emitter, &scratchBytes) ==
                       mvfx_success &&
                   (scratchBytes == 0 || (scratch = malloc((size_t)scratchBytes)) != NULL) &&
                   mvfxInstance_WriteParticles(context, instance, emitter, layout, &view, scratch,
                                               scratchBytes, out, capacity, &count) == mvfx_success;
    free(scratch);
    CHECK(mvfxDestroyLayout(context, layout) == mvfx_success, "the reading layout destroyed");
    return written ? (int32_t)count : -1;
}

// ReadParticlesFrom for a view at the world's origin.
static int32_t ReadParticles(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                             TestParticle* out, int32_t capacity)
{
    return ReadParticlesFrom(context, instance, emitter, NULL, out, capacity);
}

#undef TEST_FIELD

#endif
