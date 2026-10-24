// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tests' switches (test_hooks.h).

#include "test_hooks.h"

#include "effect.h"
#include "instance.h"
#include "instance_api.h"

bool mvfxTestSetOwed(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, uint64_t owed)
{
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr || emitter < 0 || emitter >= live->effect->emitterCount)
    {
        return false;
    }
    live->emitters[emitter].owed = owed;
    return true;
}
