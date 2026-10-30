// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The effect section of an effect file: walks of the def's tables
// (file_fields.h). Writing puts each field that differs from its default
// as its tag, its length and its payload, or measures them; reading takes
// such a sequence into a def that holds its defaults, or checks it and
// measures the memory the def's arrays take. Both walk the same tables,
// so the two directions cannot drift.

#ifndef MAUL_VFX_SRC_FILE_CODEC_H
#define MAUL_VFX_SRC_FILE_CODEC_H

#include "file_fields.h"

#include "maul-vfx/file.h"

#include <stdbool.h>
#include <stddef.h>

// The kinds of names a file holds, as mvfxAssetKind counts them.
#define MVFX_ASSET_KINDS 3

// The format's alignment: every section, field and item starts at a
// multiple of it in the file, after zero bytes.
#define MVFX_FILE_ALIGNMENT 8

// A walk over an effect section.
typedef struct mvfxCodec
{
    // Writing: the bytes written into, NULL to measure them; reading: the
    // file's bytes.
    uint8_t* out;
    const uint8_t* in;
    // The cursor and the end of what it walks, as offsets in the file.
    size_t at;
    size_t end;
    // The block of defaults (mvfxFileDefaults): writing compares array
    // items with their default, reading lays them from it.
    const uint8_t* defaults;
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
    // the next a reference may name first, against the most the names
    // section holds.
    int32_t* names[MVFX_ASSET_KINDS];
    int32_t nameCount[MVFX_ASSET_KINDS];
    int32_t nameCap[MVFX_ASSET_KINDS];
} mvfxCodec;

// Puts the fields of a struct of a type at object whose bits differ from
// those of base, its default, in ascending tag order.
void mvfxWriteFields(mvfxCodec* codec, const mvfxFileType* type, const uint8_t* object,
                     const uint8_t* base);

// Puts the payload of a field of the struct at object, whatever it holds:
// what the writer puts after the field's tag and length, base the
// struct's default. The pinned defaults' suite checks the registry with
// it.
void mvfxPutPayload(mvfxCodec* codec, const mvfxFileField* field, const uint8_t* object,
                    const uint8_t* base);

// Takes a sequence of a type's fields ending at end into object, which
// holds the type's default; NULL to check and measure only.
void mvfxReadFields(mvfxCodec* codec, const mvfxFileType* type, uint8_t* object, size_t end);

// Records a fault at an offset unless one is recorded.
void mvfxCodecFault(mvfxCodec* codec, mvfxResult fault, int64_t offset);

#endif
