// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A box body kept out of a box: the separating axes (Gottschalk, Lin and
// Manocha, "OBBTree", SIGGRAPH 1996), then a face clipped by the other's
// (as ODE's dBoxBox) or two edges' nearest points.

#ifndef MAUL_VFX_SRC_RIGID_BOX_H
#define MAUL_VFX_SRC_RIGID_BOX_H

#include "collide.h"
#include "convex.h"
#include "rigid_shape.h"

// Adds the contacts of a box body at a pose with a box collider kept out
// of: up to eight where a face meets a face, one where edges meet.
void mvfxBoxBoxContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                        const mvfxResolvedCollider* c, mvfxBodyContacts* list);

// The part of a box collider the particles' space does not turn that a
// hull body of reach r at x can meet, as its low and high corners in the
// particles' space, so a search against it takes its tolerances from the
// body and not from the box (a box 1e6 across would make them lengths
// like the body's own). Each axis is clipped to x less and plus twice the
// greatest power of two at most 8 r (8 r to 16 r, the same bits for a
// reach a rounding apart): the box's real faces within it are kept as
// its center plus or less its half size, and an artificial face is at
// least 7 r from the body. A body whose center is inside the box by more
// than 2 r on every axis gets the whole box, so it leaves by a real face.
// Returns false where the clip is empty, as no part of the body reaches
// it.
MVFX_NODISCARD bool mvfxClipBox(const mvfxResolvedCollider* c, const float x[3], float r,
                                float low[3], float high[3]);

// The part of a box collider the particles' space turns that a hull body
// of reach r at x can meet, clipped as mvfxClipBox clips an unturned one
// in the box's own frame: x taken into it, and the clip's corners and
// faces turned back, in binary64 until each is rounded near the body, so
// the box's size sets neither the search's tolerances nor how finely its
// faces near the body are held: the clip as a polytope into out. Returns
// false where the clip is empty.
MVFX_NODISCARD bool mvfxClipTurnedBox(const mvfxResolvedCollider* c, const float x[3], float r,
                                      mvfxPolytope* out);

#endif
