// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas as the library keeps it: its def checked and copied, its fields'
// sizes and their places in an instance's block.

#ifndef MAUL_VFX_SRC_GAS_H
#define MAUL_VFX_SRC_GAS_H

#include "allocator.h"

#include "maul-vfx/gas.h"

#include <stdbool.h>
#include <stddef.h>

// A gas's def as compiled: its sources copied in.
typedef struct mvfxCompiledGas
{
    mvfxGasDef def;
    mvfxGasSource sources[MVFX_MAX_GAS_SOURCES];
    // Its cells, and along each axis its faces across that axis.
    int64_t cells;
    int64_t faces[3];
} mvfxCompiledGas;

// A gas's state in an instance: its velocity's components at the faces
// across their axes and its density, temperature and fuel at the cells'
// centers, each in two buffers (current the one now); where its box was
// put (world space: the instance's place at its start); whether it holds
// anything (a value of a scalar above MVFX_GAS_EMPTY).
typedef struct mvfxGasState
{
    float* velocity[3][2];
    float* scalars[3][2];
    int32_t current;
    double origin[3];
    float rotation[4];
    float scale;
    bool holds;
} mvfxGasState;

// A scalar at most this is nothing.
#define MVFX_GAS_EMPTY 1e-6f

// Whether a gas def is valid.
bool mvfxIsGasDefValid(const mvfxGasDef* def);

// A valid gas def compiled.
mvfxCompiledGas mvfxCompileGas(const mvfxGasDef* def);

// Places a gas's buffers in a layout: at[0..2] its velocity's components',
// at[3..5] its scalars' (each two buffers in a row).
void mvfxAddGasLayout(mvfxLayout* layout, const mvfxCompiledGas* gas, size_t at[6]);

// Points a gas's state at its buffers in block, all zero.
void mvfxStartGas(mvfxGasState* state, const mvfxCompiledGas* gas, unsigned char* block,
                  const size_t at[6]);

// Zeroes a gas's fields.
void mvfxClearGas(mvfxGasState* state, const mvfxCompiledGas* gas);

#endif
