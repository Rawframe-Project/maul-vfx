// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A box body kept out of a box: the separating axes (Gottschalk, Lin and
// Manocha, "OBBTree", SIGGRAPH 1996), then a face clipped by the other's
// (as ODE's dBoxBox) or two edges' nearest points.

#ifndef MAUL_VFX_SRC_RIGID_BOX_H
#define MAUL_VFX_SRC_RIGID_BOX_H

#include "collide.h"
#include "rigid_shape.h"

// Adds the contacts of a box body at a pose with a box collider kept out
// of: up to eight where a face meets a face, one where edges meet.
void mvfxBoxBoxContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                        const mvfxResolvedCollider* c, mvfxBodyContacts* list);

#endif
