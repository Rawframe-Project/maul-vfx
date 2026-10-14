// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instances a context owns, as the library's other parts reach them.

#ifndef MAUL_VFX_SRC_INSTANCE_API_H
#define MAUL_VFX_SRC_INSTANCE_API_H

#include "instance.h"

// The instance an id names on the context, or NULL when the id is stale.
mvfxInstance* mvfxContextInstance(const mvfxContext* context, mvfxInstanceId instance);

// Where an emitter of an instance stands.
mvfxEmitterStatus mvfxEmitterStatusOf(const mvfxInstance* instance, int32_t emitter);

// Whether every emitter of an instance is stopped.
bool mvfxIsInstanceFinished(const mvfxInstance* instance);

// The live instance and the emitter an id and an index name, counting
// misuse for an emitter out of range: stale or invalid, or success.
mvfxResult mvfxFindEmitter(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                           mvfxInstance** liveOut);

#endif
