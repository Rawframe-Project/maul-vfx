// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A rigid body's free turning: its principal frame, and the flow of its
// spin and orientation by a Casimir splitting of its kinetic energy.

#ifndef MAUL_VFX_SRC_RIGID_FLOW_H
#define MAUL_VFX_SRC_RIGID_FLOW_H

#include <stdbool.h>

// A body's principal frame: its moments of inertia, largest first, and
// the unit quaternion turning the frame into the body's.
typedef struct mvfxPrincipal
{
    float moments[3];
    float rotation[4];
} mvfxPrincipal;

// The principal frame of an inertia (xx, yy, zz, xy, xz, yz) by cyclic
// Jacobi rotations in binary64: pairs (0, 1), (0, 2), (1, 2), at most 16
// sweeps, stopped once every off-diagonal is 0 (at once for a diagonal
// one). Its moments are sorted largest first, a tie keeping the earlier
// axis first; each axis has its largest component (the first of a tie)
// positive, and the third is negated if the three turn left-handed.
mvfxPrincipal mvfxPrincipalOf(const float inertia[6]);

// Turns a body's orientation q and its angular velocity w, both in the
// particles' space, by the free flow of its kinetic energy over t, and
// true; false, both left, for a body of one moment about every axis (or
// with one of none, a body of no volume), whose flow is a turn about w by
// |w| t, and for a turn past mvfxSinCos's range or terms past binary64's.
// The energy is split as the momentum's square over twice the middle
// moment (the Casimir part: the body turned about its momentum by
// t |L| / I1, its momentum in its frame kept) and, about each other axis
// i, L_i^2 (1 / I_i - 1 / I1) / 2 (the residual: the body and its momentum
// turned about i by t L_i (1 / I_i - 1 / I1)), stepped as the least
// moment's residual for t / 2, the largest's for t, the least's for t / 2,
// then the Casimir part for t. Each part is an exact turn, in binary64
// from mvfxSinCos's binary32 half turns scaled to unit pairs: the
// momentum's size is kept to rounding, its orbit in the body's frame
// held to its energy's, and a body symmetric about an axis turns exactly.
bool mvfxFlowBody(const mvfxPrincipal* p, float t, float q[4], float w[3]);

#endif
