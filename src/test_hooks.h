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

// Folds everything a live instance holds into hash, as the determinism
// tests hash it: its clock; each emitter's particles' attributes, its
// counters, events, links and bounds; its gases' and waters' fields.
// Returns false, leaving hash as it was, for an id that names no live
// instance. The outputs show such a state only once it reaches them,
// steps later or never (a gas's pressure, an attribute no record field
// reads), so a difference would surface late or not at all.
bool mvfxTestHashInstance(const mvfxContext* context, mvfxInstanceId instance, uint64_t* hash);

// The next six exist only so the GPU part's harness (test/rhi_harness.c)
// can lock-step a CPU instance with a planned one of the same effect:
// read its stored values as the GPU's are read, set them to the GPU's
// after each step so both paths step from the same state, and find again
// from those what a step finds from its own places. A host module can
// neither run in an effect a GPU plays nor set a value between steps, and
// records give values as drawn (through the instance's transform and the
// values over life), not as stored.

// Copies an emitter's live particles' words of one of its arrays, in the
// order it holds them: array is an index the emitter's view gives
// (mvfxEmitterView's arrays, a value's first array plus its component).
// Returns the live count, copying up to capacity, or -1 for an id that
// names no live instance, an emitter out of range or an array it does not
// store.
int32_t mvfxTestReadParticles(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                              int32_t array, uint32_t* out, int32_t capacity);

// Sets one of an emitter's arrays, as mvfxTestReadParticles reads it, to
// count words, its live count. Returns false for an id that names no live
// instance, an emitter out of range, an array it does not store or
// another count.
bool mvfxTestAdoptParticles(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                            int32_t array, const uint32_t* words, int32_t count);

// The id an emitter gives its next ribbon, or -1 for an id that names no
// live instance or an emitter out of range. A planned instance's plan
// carries its own (mvfxEmitterPlan's ribbonCounter); no public call reads
// a CPU instance's.
int64_t mvfxTestRibbonCounter(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter);

// Finds an emitter's plexus links again from its particles' places as
// they are, as a step's end does, and copies up to capacity of them, each
// a pair of particle indices (the owner first), into links; dropped
// receives the links this finding dropped, the emitter's count of drops
// left as it was. Returns the links found, or -1 for an id that names no
// live instance or an emitter out of range or without plexus links. A
// step finds links only from places it moved, and
// mvfxInstance_WritePlexus writes a link's ends, not its particles.
int32_t mvfxTestFindLinks(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                          int32_t* links, int32_t capacity, uint64_t* dropped);

// A water surface's reach above and below its grid's center, which a
// planned instance's reports set (mvfxInstance_SetWaterCounts), into
// reach. Returns false for an id that names no live instance or a surface
// out of range. The bounds hold it joined with the emitters' boxes.
bool mvfxTestWaterReach(const mvfxContext* context, mvfxInstanceId instance, int32_t water,
                        float* reach);

// The substeps an instance's liquid gases take in a step of ticks, from
// their fastest particles' speeds (a planned instance's reported,
// mvfxInstance_SetLiquidSpeed), or -1 for an id that names no live
// instance. A planned instance's plan gives its own (mvfxPlannedLiquid);
// no public call gives a CPU instance's.
int32_t mvfxTestLiquidSubsteps(const mvfxContext* context, mvfxInstanceId instance, int64_t ticks);

// Runs the convex search rigid hulls meet by between the hulls of a's and
// b's points (x, y and z each, countA and countB of them, from 1 to
// MVFX_MAX_HULL_VERTICES, already where they meet) within margin: whether
// they meet, and when they do, whether a search stopped at its limit
// (stopped), EPA's steps, their distance (below 0 overlapping) and the
// unit normal from b toward a. Returns false too for a count out of range.
// A step counts its stopped searches (mvfxEmitterStats.rigidSearchStopCount)
// but no public call gives one's steps, nor the search's normal before
// contacts are made from it.
bool mvfxTestConvexMeet(const float* a, int32_t countA, const float* b, int32_t countB,
                        float margin, bool* stopped, int32_t* epaSteps, float* distance,
                        float normal[3]);

// The most points and faces of EPA's polytope (as MVFX_EPA_POINTS and
// MVFX_EPA_FACES).
#define MVFX_TEST_EPA_POINTS 68
#define MVFX_TEST_EPA_FACES  136

// The polytope EPA grows in at most most steps for the hulls of a's and
// b's points (as mvfxTestConvexMeet takes them), where they overlap: its
// points (of the difference a less b) and their count, and its faces
// (counterclockwise from outside, indices into points); their count, 0
// when they are apart, the difference is flat or a count is out of range.
// No public call gives the search's polytope, whose shape a test checks.
int32_t mvfxTestEpaPolytope(const float* a, int32_t countA, const float* b, int32_t countB,
                            int32_t most, double points[MVFX_TEST_EPA_POINTS][3],
                            int32_t* pointCount, uint8_t faces[MVFX_TEST_EPA_FACES][3]);

#endif // MAUL_VFX_SRC_TEST_HOOKS_H
