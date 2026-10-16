// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The effect unit of an effect file: a walk of the def's tables
// (file_fields.h) that writes a def into bytes or measures them, or reads
// bytes into a def or checks them and measures the memory the def's
// arrays take, so the two directions cannot drift.

#ifndef MAUL_VFX_SRC_FILE_CODEC_H
#define MAUL_VFX_SRC_FILE_CODEC_H

#include "file_fields.h"

#include "maul-vfx/file.h"

#include <stdbool.h>
#include <stddef.h>

// The kinds of names a file holds, as mvfxAssetKind counts them.
#define MVFX_ASSET_KINDS 3

// A walk over an effect unit.
typedef struct mvfxCodec
{
    bool writing;
    // Writing: the bytes written into, NULL to measure them; reading: the
    // file's bytes.
    uint8_t* out;
    const uint8_t* in;
    // The cursor and the unit's end, as offsets in the file.
    size_t at;
    size_t end;
    // Reading: the memory the def's arrays are laid in, NULL to measure
    // it, its size and the bytes laid so far.
    uint8_t* memory;
    size_t capacity;
    size_t used;
    // The first fault and the offset of its byte, -1 for none.
    mvfxResult fault;
    int64_t faultAt;
    const mvfxEffectAsset* assets;
    int32_t assetCount;
    // Each kind's names as indices of assets: writing, gathered in the
    // order the def first refers to them; reading, bound, or NULL to
    // check the references only. Their counts: writing, so far; reading,
    // the next a reference may name first, against the most the unit
    // table holds.
    int32_t* names[MVFX_ASSET_KINDS];
    int32_t nameCount[MVFX_ASSET_KINDS];
    int32_t nameCap[MVFX_ASSET_KINDS];
} mvfxCodec;

// Walks a struct of a type at object: writing, its fields into the bytes;
// reading, the bytes into its fields, NULL to check and measure only.
void mvfxCodeStruct(mvfxCodec* codec, const mvfxFileType* type, uint8_t* object);

// Records a fault at an offset unless one is recorded.
void mvfxCodecFault(mvfxCodec* codec, mvfxResult fault, int64_t offset);

#endif
