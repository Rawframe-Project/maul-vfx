// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scene queries: written at the end of an emitter's step for its moving
// particles, answered by the host with contact planes between steps.

#ifndef MAUL_VFX_SRC_SCENE_QUERY_H
#define MAUL_VFX_SRC_SCENE_QUERY_H

#include "instance.h"

// Writes an emitter's scene queries for the step just ended, when it
// collides with the scene.
void mvfxWriteSceneQueries(mvfxInstance* instance, int32_t emitter);

#endif
