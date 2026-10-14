// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// One emitter's step to a new time on its instance's clock: particles
// whose life has ended are removed, keeping the rest in order, and what
// the emitter owes by the new time is born at its exact ticks. The result
// is the state stepping one tick at a time would reach (each tick's deaths
// before its births), so it does not depend on how time was split.

#ifndef MAUL_VFX_SRC_EMITTER_STEP_H
#define MAUL_VFX_SRC_EMITTER_STEP_H

#include "instance.h"

// Particle k of an emitter spawning at rate per second is born at tick
// ceil(k * MVFX_TICKS_PER_SECOND / rate); this is how many are born by
// tick clock (particles 0 to the result less one), for rate above 0.
uint64_t mvfxOwedBy(float rate, int64_t clock);

// The tick particle k is born at, for rate above 0.
int64_t mvfxBirthTick(float rate, uint64_t k);

// Steps emitter e of instance to the instance's clock.
void mvfxStepEmitter(mvfxInstance* instance, int32_t emitter);

#endif
