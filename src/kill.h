// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Kill rules: their checks, their compiled and resolved forms, and the
// tests of a particle's step against them.

#ifndef MAUL_VFX_SRC_KILL_H
#define MAUL_VFX_SRC_KILL_H

#include "force.h"

#include "maul-vfx/effect.h"

// A kill rule as a step takes it, in the particles' space.
typedef struct mvfxResolvedKillRule
{
    mvfxKillKind kind;
    float center[3];
    float radius;
    float halfExtents[3];
    float rotation[4];
    float normal[3];
    float speed;
    float minimumAge;
} mvfxResolvedKillRule;

bool mvfxIsKillRuleValid(const mvfxKillRule* rule);

// A valid rule as compiled: its normal and rotation normalized.
mvfxKillRule mvfxCompileKillRule(const mvfxKillRule* rule);

// Resolves a compiled rule with the map from its space.
mvfxResolvedKillRule mvfxResolveKillRule(const mvfxKillRule* rule, const mvfxSpaceMap* map);

// Whether the segment from start to end comes within radius of center.
bool mvfxSegmentMeetsSphere(const float start[3], const float end[3], const float center[3],
                            float radius);

// Whether any rule kills a particle that went from start to end over a
// step, ending it with velocity at ageSeconds.
bool mvfxIsKilled(const mvfxResolvedKillRule* rules, int32_t count, const float start[3],
                  const float end[3], const float velocity[3], double ageSeconds);

#endif
