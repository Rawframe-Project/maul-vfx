// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The typical sprite emitter the benchmarks share: spawning at a rate from
// a sphere into a cone, under gravity and drag, coloured and sized over
// its life, dying of age at 1.8 to 2.2 s.

#ifndef MAUL_VFX_BENCH_BENCH_SPRITE_H
#define MAUL_VFX_BENCH_BENCH_SPRITE_H

#include "maul-vfx/effect.h"

static const mvfxColorKey s_colors[2] = {{0.0f, 1.0f, 0.9f, 0.6f}, {1.0f, 0.8f, 0.2f, 0.1f}};
static const mvfxAlphaKey s_alphas[2] = {{0.0f, 1.0f}, {1.0f, 0.0f}};
static const mvfxCurveKey s_growing[2] = {{.time = 0.0f, .value = 0.5f},
                                          {.time = 1.0f, .value = 1.5f}};

// The typical sprite emitter of capacity particles at rate a second.
static inline mvfxEmitterDef Sprite(int32_t capacity, float rate)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = capacity;
    emitter.rate = rate;
    emitter.lifetime = mvfxRangeSource(1.8f, 2.2f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapeSphere, .rotation = {0, 0, 0, 1}, .radius = 0.5f};
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {0, 1, 0},
                                      .coneAngle = 0.4f,
                                      .speed = {2.0f, 4.0f}};
    emitter.motion.gravity[1] = -9.8f;
    emitter.motion.drag = 0.5f;
    emitter.overLife.color = (mvfxColorOverLife){
        .enabled = true,
        .gradient = {
            .colorKeys = s_colors, .colorKeyCount = 2, .alphaKeys = s_alphas, .alphaKeyCount = 2}};
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 1.0f, .curve = {.keys = s_growing, .keyCount = 2}}}};
    return emitter;
}

#endif
