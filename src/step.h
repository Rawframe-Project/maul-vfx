// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A context's step: every instance's clock advances and each of its
// emitters steps, one instance per task item, so the host's split and
// schedule never change what any instance holds.

#ifndef MAUL_VFX_SRC_STEP_H
#define MAUL_VFX_SRC_STEP_H

#include "context.h"
#include "instance.h"

// Advances one instance by ticks, in the library's float mode.
void mvfxStepInstance(mvfxInstance* instance, int64_t ticks);

// Advances every live instance by ticks, which the caller has checked.
void mvfxStepInstances(mvfxContext* context, int64_t ticks);

// Steps the gases due a step, of every instance in slot order, their
// passes split over the context's tasks: once every instance's own step is
// done.
void mvfxStepDueGases(mvfxContext* context);

#endif
