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
#include "maul-vfx/gas.h"
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
// not tear, or a particle not live. A step tears the edges whose strain
// passes the unit's tearStrain; no public call tears one.
bool mvfxTestTearEdges(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                       int32_t particle, uint32_t bits);

// Copies up to capacity of the pairs of an emitter's rigid particles the
// last step's broadphase found, by their indices among the emitter's
// particles, into a and b; returns how many there are, or -1 for an id
// that names no live instance or an emitter out of range. A collision
// event names the struck body but not the other, and no output lists
// pairs.
int32_t mvfxTestRigidPairs(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                           int32_t* a, int32_t* b, int32_t capacity);

// Copies up to capacity of a gas's velocities on its faces across an axis
// (0 to 2), X fastest, then Y, then Z, as the last step left them;
// returns how many faces there are, or -1 for an id that names no live
// instance, a gas or an axis out of range, or an axis the gas keeps no
// velocity along. The public reads give a cell the mean of its two faces
// or a trilinear sample, never one face.
int64_t mvfxTestReadGasFaces(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                             int32_t axis, float* out, int64_t capacity);

// Sets a gas's velocities on its faces across an axis, count of them (the
// faces there are), as a step would leave them, so a test can start a
// solve from any field. Returns false for an id that names no live
// instance, a gas or an axis out of range, an axis the gas keeps no
// velocity along, or another count. A step makes them from sources,
// forces and obstacles; no public call sets them.
bool mvfxTestWriteGasFaces(mvfxContext* context, mvfxInstanceId instance, int32_t gas, int32_t axis,
                           const float* values, int64_t count);

// Sets a gas's scalar field (mvfx_gasDensity, mvfx_gasTemperature,
// mvfx_gasFuel, or a surface's mvfx_gasDistance) at its cells, count of
// them (its cells). Returns false for an id that names no live instance,
// a gas out of range, a field it does not keep, or another count. A
// step makes them from sources, or a surface's distance from particles;
// no public call sets them.
bool mvfxTestWriteGasScalar(mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                            mvfxGasField field, const float* values, int64_t count);

// Copies up to capacity of a gas's cells' solids into out, each 0 for an
// open cell, else 1 more than the index of the obstacle filling it, as
// the last step marked them, and returns how many cells it has. Returns
// -1 for an id that names no live instance, a gas out of range, or a gas
// without obstacles. The public reads show an obstacle only by its effect
// on the fields.
int64_t mvfxTestReadGasSolid(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                             uint8_t* out, int64_t capacity);

// Sets a shallow water surface's depths at its cells, count of them (its
// cells), as a step would leave them, so a test can let go any water.
// Returns false for an id that names no live instance, a surface out of
// range or not shallow water, or another count. Steps, pushes (which
// keep the volume) and particles poured in change them; no public call
// sets them.
bool mvfxTestWriteWaterDepth(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                             const float* values, int64_t count);

// Sets a shallow water surface's velocities on its faces across an axis
// (0 along X, 1 along Z), count of them (the faces there are), as a step
// would leave them, so a test can hold any flow. Returns false for an id
// that names no live instance, a surface out of range or not shallow
// water, an axis out of range, or another count. A step makes them from
// the depths; no public call sets them.
bool mvfxTestWriteWaterFaces(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                             int32_t axis, const float* values, int64_t count);

#endif // MAUL_VFX_SRC_TEST_HOOKS_H
