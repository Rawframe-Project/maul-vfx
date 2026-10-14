// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sorting for output: binary32 values made order-preserving as uint32
// keys, and a stable least-significant-digit radix sort of keys with
// their indices in passes of 8 bits, on memory the caller gives.

#ifndef MAUL_VFX_SRC_SORT_H
#define MAUL_VFX_SRC_SORT_H

#include <stdint.h>

// The bytes of scratch a sort of count pairs takes: keys and indices,
// twice.
#define MVFX_SORT_BYTES_PER_ITEM 16

// A key that orders as the value does: negative values below positive,
// -0 as +0 (Herf's trick on the value plus zero).
uint32_t mvfxOrderedKey(float value);

// Sorts count keys and their indices by key, ascending, keeping the order
// of equal keys; temporary keys and indices hold count each. The result is
// in keys and indices.
void mvfxRadixSort(uint32_t* keys, uint32_t* indices, uint32_t* temporaryKeys,
                   uint32_t* temporaryIndices, int32_t count);

#endif
