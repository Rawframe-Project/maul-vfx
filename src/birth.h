// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A particle's initial values, drawn from pcg4d at its serial with a salt
// per purpose (record mvfx-0001): its life, its place on the emitter's
// shape, its velocity and its optional values.

#ifndef MAUL_VFX_SRC_BIRTH_H
#define MAUL_VFX_SRC_BIRTH_H

#include "instance.h"

// One over the life of the particle of a serial born at a tick, drawn
// from the emitter's lifetime source and held to one tick at least and
// MVFX_MAX_LIFETIME at most.
float mvfxInverseLifeOf(const mvfxInstance* instance, int32_t emitter, uint64_t serial,
                        int64_t tick);

// The age a value source reads for a birth at a tick: the loop's
// normalized age, in [0, 1), with a duration; else the seconds since the
// emitter began.
float mvfxSourceAge(const mvfxCompiledEmitter* emitter, int64_t tick);

// The velocity of a host's queued particle of a serial born at a tick, in
// the particles' space: as given, or drawn in the instance's space at its
// place (its direction from the instance's origin standing for the
// shape's normal and center) and turned, scaled and given the inherited
// share as a shape's birth is, the host's added velocity after.
void mvfxHostVelocity(const mvfxInstance* instance, int32_t emitter, const mvfxHostBirth* host,
                      uint64_t serial, int64_t tick, float velocity[3]);

// The key a birth draws its values by: its event's (mvfxEventBirthKey)
// for a birth from an event, else its serial.
uint64_t mvfxDrawKey(const mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth);

// Appends a particle to the emitter: every attribute from its birth.
void mvfxAppendBirth(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                     float inverseLife);

#endif
