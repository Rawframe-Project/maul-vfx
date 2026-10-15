// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sleeping rigid particles as the host sees them: woken within a sphere,
// and counted.

#ifndef MAUL_VFX_SRC_RIGID_WAKE_H
#define MAUL_VFX_SRC_RIGID_WAKE_H

#include "collide.h"
#include "instance.h"

// Body i's reach from its place, by its shape at its size.
float mvfxReachOf(const mvfxInstance* instance, int32_t emitter, int32_t i);

// Wakes an emitter's sleeping bodies whose reach meets the sphere.
void mvfxWakeWithin(mvfxInstance* instance, int32_t emitter, const float center[3], float radius);

// Wakes an emitter's sleeping bodies a collider's change reaches: those
// whose reach (and contact margin) meets the collider as it was or as it
// is, either NULL for one added or removed.
void mvfxWakeNear(mvfxInstance* instance, int32_t emitter, const mvfxResolvedCollider* before,
                  const mvfxResolvedCollider* after);

// How many of an emitter's bodies sleep.
int32_t mvfxCountAsleep(const mvfxInstance* instance, int32_t emitter);

#endif
