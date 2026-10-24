// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Switches and reads for the tests, which no host needs, for states no
// public call reaches within a test. They build into a library of their
// own, maul-vfx-test-hooks, which is never installed and which only the
// suites that use them link; nothing here is MVFX_API, and no header here
// is installed. A test reaches what it needs through these functions,
// never through the library's structs.

#ifndef MAUL_VFX_SRC_TEST_HOOKS_H
#define MAUL_VFX_SRC_TEST_HOOKS_H

#include "maul-vfx/context.h"
#include "maul-vfx/instance.h"

#include <stdint.h>

// Sets the births an emitter of a live instance has owed so far, so its
// next serials are owed's low 32 bits on: a test reaches the serials'
// 32-bit wrap without four billion births. Returns false for an id that
// names no live instance or an emitter out of range.
bool mvfxTestSetOwed(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, uint64_t owed);

// Sets which of a unit's edges from a particle are torn: bit 0 the edge
// across to its neighbor, bit 1 the edge down, as tearing would leave
// them, so a test can tear any one edge. Returns false for an id that
// names no live instance, an emitter out of range or one whose units do
// not tear, or a particle not live.
bool mvfxTestTearEdges(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                       int32_t particle, uint32_t bits);

// Copies up to capacity of the pairs of an emitter's rigid particles the
// last step's broadphase found, by their indices among the emitter's
// particles, into a and b; returns how many there are, or -1 for an id
// that names no live instance or an emitter out of range.
int32_t mvfxTestRigidPairs(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                           int32_t* a, int32_t* b, int32_t capacity);

// Sets a shallow water surface's depths at its cells, count of them (its
// cells), as a step would leave them, so a test can let go any water.
// Returns false for an id that names no live instance, a surface out of
// range or not shallow water, or another count.
bool mvfxTestWriteWaterDepth(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                             const float* values, int64_t count);

// Sets a shallow water surface's velocities on its faces across an axis
// (0 along X, 1 along Z), count of them (the faces there are), as a step
// would leave them, so a test can hold any flow. Returns false for an id
// that names no live instance, a surface out of range or not shallow
// water, an axis out of range, or another count.
bool mvfxTestWriteWaterFaces(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                             int32_t axis, const float* values, int64_t count);

#endif // MAUL_VFX_SRC_TEST_HOOKS_H
