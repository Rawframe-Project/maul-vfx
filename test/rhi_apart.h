// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles whose contacts the two paths may decide differently, as sim.h
// states (rhi_apart.c): contacts are decided on places, which the GPU
// holds to a tolerance of the CPU path's, so a particle whose path passes
// that close to a surface may touch it on one path only; a rigid body's
// friction holds or lets go, and its strikes bounce or not, by loads and
// speeds its masses part by their rounding. The CPU path's particle is
// near when it ends a step within that tolerance of one of the plan's
// colliders (a rigid body's surface, anywhere within its radius of its
// center); one near at the end of a step or of the last may go apart
// once, and is compared no more, nor are its events. Every other particle
// is compared exactly as before.

#ifndef MAUL_VFX_TEST_RHI_APART_H
#define MAUL_VFX_TEST_RHI_APART_H

#include "maul-vfx/plan.h"

#include <stdbool.h>
#include <stdint.h>

// Forgets every emitter's particles, near and apart, for a new run.
void ApartReset(void);

// Takes the CPU path's particles of an emitter at a step's end, in serial
// order: their serials and places (offsets in the particles' space), and
// the plan whose colliders they end near or not (a body is near a depth
// image wherever), within the relative tolerance places are compared to
// and the radius a rigid body's surface reaches at most from its center
// (0 for particles).
void ApartTakeStep(int32_t emitter, const uint32_t* serials, const float* const places[3],
                   int32_t count, const mvfxEmitterPlan* plan, float tolerance, float radius);

// Whether a particle of the emitter is apart already; whether it may go
// apart now (near at this step's end or the last), making it so.
bool IsApart(int32_t emitter, uint32_t serial);
bool GoApart(int32_t emitter, uint32_t serial);

// An event of a source emitter the two paths may give different births
// from: one of a particle apart, or one whose tick differs (a contact's,
// whose share of the move the GPU finds in binary32), whose drawn count
// may differ. Takes its tick on either path; whether a birth's tick lies
// within slack of one taken.
void TakeUncertain(int64_t tick);
bool IsUncertain(int64_t tick, int64_t slack);

// Whether an uncertain tick lies in (after, upTo], each end widened by
// slack: where a spawning emitter's serial offset between the paths may
// change. The uncertain events taken over the run.
bool UncertainBetween(int64_t after, int64_t upTo, int64_t slack);
int32_t UncertainCount(void);

// The particles gone apart over the run, the most near at one step, and
// the largest distance from a collider one gone apart lay at, a share of
// the reach (at most 1).
int32_t ApartCount(void);
int32_t MostNear(void);
float LargestShare(void);

#endif
