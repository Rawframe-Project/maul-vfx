// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The defs' defaults as initializers: mvfxDefault*Def returns them, and
// the effect file's block of defaults (file_fields.c) holds them as
// constants, so a field a file leaves out reads as the bits the default
// function gives.

#ifndef MAUL_VFX_SRC_DEFAULTS_H
#define MAUL_VFX_SRC_DEFAULTS_H

#include "maul-vfx/effect.h"

#define MVFX_DEFAULT_EFFECT_DEF {.cookie = MVFX_EFFECT_DEF_COOKIE}

#define MVFX_DEFAULT_EMITTER_DEF                                                                   \
    {                                                                                              \
        .cookie = MVFX_EMITTER_DEF_COOKIE,                                                         \
        .capacity = 1024,                                                                          \
        .rate = 64.0f,                                                                             \
        .lifetime = {.kind = mvfx_sourceRange, .range = {1.0f, 1.0f}},                             \
        .shape =                                                                                   \
            {                                                                                      \
                .kind = mvfx_shapePoint,                                                           \
                .rotation = {0.0f, 0.0f, 0.0f, 1.0f},                                              \
                .arc = 6.28318530717958647692f,                                                    \
            },                                                                                     \
        .velocity = {.mode = mvfx_directionShapeNormal, .direction = {0.0f, 0.0f, 1.0f}},          \
        .velocityModules = {.motionScale = {.value = 1.0f}},                                       \
        .rigid = {.sleepSpeed = 0.05f, .sleepTime = 0.5f},                                         \
        .liquid = {.spacing = 0.1f,                                                                \
                   .kernelScale = 2.0f,                                                            \
                   .substeps = 4,                                                                  \
                   .iterations = 3,                                                                \
                   .relaxation = 0.01f,                                                            \
                   .tensileDistance = 0.2f,                                                        \
                   .tensilePower = 4,                                                              \
                   .viscosity = 0.01f,                                                             \
                   .maxNeighbors = 64},                                                            \
        .ribbon = {.pieces = 1,                                                                    \
                   .tileLength = 1.0f,                                                             \
                   .normal = {0.0f, 0.0f, 1.0f},                                                   \
                   .width = {.value = 1.0f}},                                                      \
    }

#define MVFX_DEFAULT_GAS_DEF                                                                       \
    {                                                                                              \
        .cookie = MVFX_GAS_DEF_COOKIE,                                                             \
        .size = {64, 64, 1},                                                                       \
        .cellSize = 0.1f,                                                                          \
        .fillTime = 1.0f / 60.0f,                                                                  \
        .courant = 1.0f,                                                                           \
        .space = mvfx_gasLocal,                                                                    \
        .substeps = 1,                                                                             \
        .pressureCycles = 4,                                                                       \
        .gravity = {0.0f, -9.81f, 0.0f},                                                           \
        .gravitySpace = mvfx_gasWorld,                                                             \
        .surface = {.radius = 0.1f, .reach = 4.0f, .low = 0.4f, .high = 2.0f, .smoothing = 1},     \
    }

#define MVFX_DEFAULT_WATER_DEF                                                                     \
    {                                                                                              \
        .cookie = MVFX_WATER_DEF_COOKIE,                                                           \
        .size = {64, 64},                                                                          \
        .cellSize = 0.1f,                                                                          \
        .space = mvfx_gasLocal,                                                                    \
        .closedWalls = 15,                                                                         \
        .waveSpeed = 1.0f,                                                                         \
        .damping = 0.5f,                                                                           \
        .gravity = 9.81f,                                                                          \
        .terrain = {.kind = mvfx_colliderHeightfield, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}},       \
    }

#endif
