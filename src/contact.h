// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particle contacts: position-based projections, then impulses.

#ifndef MVFX_CONTACT_H
#define MVFX_CONTACT_H

#include "instance.h"

// At an emitter's step, after motion and births: for an emitter whose
// particles keep apart, projects pairs apart, then applies restitution
// and friction to pairs touching and closing.
void mvfxSolveContacts(mvfxInstance* instance, int32_t emitter);

#endif
