// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's particles as a host module sees them after motion, which
// is where a step leaves them: the arrays mvfxParticleArrays shows a host,
// in the pool's order, for the suites that check what the solvers did to
// a particle beyond what the output writes (its birth, life, spin,
// angular velocity and collisions). A suite defines PEEK_MOST before the
// include to read more than 64 particles.

#ifndef MAUL_VFX_TEST_PEEK_H
#define MAUL_VFX_TEST_PEEK_H

#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/modules.h"

#include <stdint.h>
#include <string.h>

#ifndef PEEK_MOST
#define PEEK_MOST 64
#endif

// The first PEEK_MOST particles after motion in the last step that ran
// the emitter's modules, 0 where the emitter stores none of a value; the
// step's end tick and the emitter's anchor then.
typedef struct Peeked
{
    int64_t endTick;
    double anchor[3];
    int32_t count;
    float position[3][PEEK_MOST];
    float velocity[3][PEEK_MOST];
    float orientation[4][PEEK_MOST];
    float angular[3][PEEK_MOST];
    float spin[PEEK_MOST];
    float angle[PEEK_MOST];
    float inverseLife[PEEK_MOST];
    int64_t birth[PEEK_MOST];
    uint32_t serial[PEEK_MOST];
    uint32_t collisions[PEEK_MOST];
} Peeked;

static Peeked s_peeked;

static void PeekCopy(float* out, const float* in, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        out[i] = in != NULL ? in[i] : 0.0f;
    }
}

static void Peek(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    int32_t count = p->count < PEEK_MOST ? p->count : PEEK_MOST;
    s_peeked.endTick = call->endTick;
    memcpy(s_peeked.anchor, call->anchor, sizeof s_peeked.anchor);
    s_peeked.count = p->count;
    for (int32_t k = 0; k < 3; ++k)
    {
        PeekCopy(s_peeked.position[k], p->position[k], count);
        PeekCopy(s_peeked.velocity[k], p->velocity[k], count);
        PeekCopy(s_peeked.angular[k], p->angularVelocity[k], count);
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        PeekCopy(s_peeked.orientation[k], p->orientation[k], count);
    }
    PeekCopy(s_peeked.spin, p->spin, count);
    PeekCopy(s_peeked.angle, p->angle, count);
    PeekCopy(s_peeked.inverseLife, p->inverseLife, count);
    for (int32_t i = 0; i < count; ++i)
    {
        s_peeked.birth[i] = (int64_t)(((uint64_t)p->birthHigh[i] << 32u) | p->birthLow[i]);
        s_peeked.serial[i] = p->serial[i];
        s_peeked.collisions[i] = p->collisions != NULL ? p->collisions[i] : 0u;
    }
}

static const mvfxHostModule s_peek = {.stage = mvfx_stageAfterMotion, .function = Peek};

// The emitter with the peeking module as its only one, the record of what
// it saw cleared.
static inline mvfxEmitterDef Peeking(const mvfxEmitterDef* emitter)
{
    CHECK(emitter->hostModuleCount == 0, "no modules of its own");
    mvfxEmitterDef peeking = *emitter;
    peeking.hostModules = &s_peek;
    peeking.hostModuleCount = 1;
    memset(&s_peeked, 0, sizeof s_peeked);
    return peeking;
}

// Particle i's age at the last step's end, in seconds.
static inline double PeekedAge(int32_t i)
{
    return (double)(s_peeked.endTick - s_peeked.birth[i]) / (double)MVFX_TICKS_PER_SECOND;
}

#endif
