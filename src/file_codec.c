// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "file_codec.h"

#include <string.h>

static_assert(sizeof(bool) == 1, "a bool is one byte, as the file's");
static_assert(sizeof(mvfxGridId) == sizeof(mvfxMeshId), "ids are coded alike");

// The bytes of each scalar kind.
static const uint8_t s_scalarBytes[] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8};

void mvfxCodecFault(mvfxCodec* codec, mvfxResult fault, int64_t offset)
{
    if (codec->fault == mvfx_success)
    {
        codec->fault = fault;
        codec->faultAt = offset;
    }
}

static uint64_t Load(const uint8_t* place, size_t bytes)
{
    switch (bytes)
    {
    case 1:
        return place[0];
    case 2:
    {
        uint16_t value = 0;
        memcpy(&value, place, sizeof value);
        return value;
    }
    case 4:
    {
        uint32_t value = 0;
        memcpy(&value, place, sizeof value);
        return value;
    }
    default:
    {
        uint64_t value = 0;
        memcpy(&value, place, sizeof value);
        return value;
    }
    }
}

static void Store(uint8_t* place, uint64_t value, size_t bytes)
{
    switch (bytes)
    {
    case 1:
        place[0] = (uint8_t)value;
        break;
    case 2:
    {
        uint16_t narrow = (uint16_t)value;
        memcpy(place, &narrow, sizeof narrow);
        break;
    }
    case 4:
    {
        uint32_t narrow = (uint32_t)value;
        memcpy(place, &narrow, sizeof narrow);
        break;
    }
    default:
        memcpy(place, &value, sizeof value);
        break;
    }
}

// Puts a value's low bytes, little-endian.
static void Put(mvfxCodec* codec, uint64_t value, size_t bytes)
{
    if (codec->fault != mvfx_success)
    {
        return;
    }
    if (bytes > codec->end - codec->at)
    {
        mvfxCodecFault(codec, mvfx_errorCapacity, -1);
        return;
    }
    for (size_t i = 0; codec->out != nullptr && i < bytes; ++i)
    {
        codec->out[codec->at + i] = (uint8_t)(value >> (8 * i));
    }
    codec->at += bytes;
}

// Takes a little-endian value of some bytes; false past the unit's end.
static bool Take(mvfxCodec* codec, size_t bytes, uint64_t* value)
{
    if (codec->fault != mvfx_success)
    {
        return false;
    }
    if (bytes > codec->end - codec->at)
    {
        mvfxCodecFault(codec, mvfx_errorInvalid, (int64_t)codec->end);
        return false;
    }
    *value = 0;
    for (size_t i = 0; i < bytes; ++i)
    {
        *value |= (uint64_t)codec->in[codec->at + i] << (8 * i);
    }
    codec->at += bytes;
    return true;
}

// Whether there is a def to write from: a reading walk may visit without
// one, a writing walk never does.
static bool HasSource(mvfxCodec* codec, const void* from)
{
    if (from == nullptr)
    {
        mvfxCodecFault(codec, mvfx_errorInvalid, -1);
        return false;
    }
    return true;
}

static void CodeScalars(mvfxCodec* codec, mvfxFieldKind kind, uint8_t* place, size_t count)
{
    size_t bytes = s_scalarBytes[kind];
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t* item = place != nullptr ? place + i * bytes : nullptr;
        if (codec->writing)
        {
            if (!HasSource(codec, item))
            {
                return;
            }
            Put(codec, Load(item, bytes), bytes);
            continue;
        }
        size_t at = codec->at;
        uint64_t value = 0;
        if (!Take(codec, bytes, &value))
        {
            return;
        }
        if (kind == mvfx_fieldBool && value > 1)
        {
            mvfxCodecFault(codec, mvfx_errorInvalid, (int64_t)at);
            return;
        }
        if (item != nullptr)
        {
            Store(item, value, bytes);
        }
    }
}

// The asset a host object is, the first of the table; -1 for none.
static int32_t FindAsset(const mvfxCodec* codec, mvfxAssetKind kind, const mvfxEffectAsset* like)
{
    for (int32_t a = 0; a < codec->assetCount; ++a)
    {
        const mvfxEffectAsset* asset = &codec->assets[a];
        bool same = false;
        switch (kind)
        {
        case mvfx_assetGrid:
            same = asset->grid.index1 == like->grid.index1 &&
                   asset->grid.generation == like->grid.generation;
            break;
        case mvfx_assetMesh:
            same = asset->mesh.index1 == like->mesh.index1 &&
                   asset->mesh.generation == like->mesh.generation;
            break;
        default:
            same = asset->function == like->function && asset->user == like->user;
            break;
        }
        if (asset->kind == kind && same)
        {
            return a;
        }
    }
    return -1;
}

// Puts the name of an asset (-1 for none), gathering it the first time.
static void PutName(mvfxCodec* codec, mvfxAssetKind kind, int32_t asset)
{
    int32_t index = -1;
    int32_t* names = codec->names[kind];
    for (int32_t i = 0; asset >= 0 && i < codec->nameCount[kind]; ++i)
    {
        index = names[i] == asset ? i : index;
    }
    // A def refers to at most nameCap of a kind (file_fields.c checks
    // the caps against its arrays), so the names never outgrow them.
    if (asset >= 0 && index < 0)
    {
        index = codec->nameCount[kind]++;
        names[index] = asset;
    }
    Put(codec, (uint32_t)index, 4);
}

// Takes a name and returns its asset: -1 for none, or when only checking.
// A name is one the def refers to before, or the next.
static int32_t TakeName(mvfxCodec* codec, mvfxAssetKind kind)
{
    size_t at = codec->at;
    uint64_t value = 0;
    if (!Take(codec, 4, &value))
    {
        return -1;
    }
    int32_t index = (int32_t)(uint32_t)value;
    if (index == -1)
    {
        return -1;
    }
    if (index < 0 || index > codec->nameCount[kind] || index >= codec->nameCap[kind])
    {
        mvfxCodecFault(codec, mvfx_errorInvalid, (int64_t)at);
        return -1;
    }
    codec->nameCount[kind] += index == codec->nameCount[kind] ? 1 : 0;
    return codec->names[kind] != nullptr ? codec->names[kind][index] : -1;
}

// A grid's or a mesh's id at place.
static void CodeId(mvfxCodec* codec, mvfxFieldKind field, uint8_t* place)
{
    mvfxAssetKind kind = field == mvfx_fieldGrid ? mvfx_assetGrid : mvfx_assetMesh;
    mvfxEffectAsset like = {0};
    if (codec->writing && HasSource(codec, place))
    {
        memcpy(&like.grid, place, sizeof like.grid);
        like.mesh = (mvfxMeshId){like.grid.index1, like.grid.generation};
        int32_t asset = like.grid.index1 == 0 ? -1 : FindAsset(codec, kind, &like);
        if (like.grid.index1 != 0 && asset < 0)
        {
            mvfxCodecFault(codec, mvfx_errorMissing, -1);
        }
        PutName(codec, kind, asset);
        return;
    }
    int32_t asset = TakeName(codec, kind);
    if (place != nullptr && asset >= 0)
    {
        const mvfxEffectAsset* bound = &codec->assets[asset];
        memcpy(place,
               kind == mvfx_assetGrid ? (const void*)&bound->grid : (const void*)&bound->mesh,
               sizeof bound->grid);
    }
}

// A host module's function at place and its user pointer at user.
static void CodeModule(mvfxCodec* codec, uint8_t* place, uint8_t* user)
{
    mvfxEffectAsset like = {0};
    if (codec->writing && HasSource(codec, place) && HasSource(codec, user))
    {
        memcpy((void*)&like.function, place, sizeof like.function);
        memcpy((void*)&like.user, user, sizeof like.user);
        int32_t asset = like.function == nullptr ? -1 : FindAsset(codec, mvfx_assetModule, &like);
        if (like.function != nullptr && asset < 0)
        {
            mvfxCodecFault(codec, mvfx_errorMissing, -1);
        }
        PutName(codec, mvfx_assetModule, asset);
        return;
    }
    int32_t asset = TakeName(codec, mvfx_assetModule);
    if (place != nullptr && asset >= 0)
    {
        memcpy(place, (const void*)&codec->assets[asset].function, sizeof like.function);
        memcpy(user, (const void*)&codec->assets[asset].user, sizeof like.user);
    }
}

// count items of a kind (a struct's by its type) from place.
static void CodeItems(mvfxCodec* codec, mvfxFieldKind kind, const mvfxFileType* type,
                      uint8_t* place, size_t count)
{
    if (kind != mvfx_fieldStruct)
    {
        CodeScalars(codec, kind, place, count);
        return;
    }
    for (size_t i = 0; i < count && codec->fault == mvfx_success; ++i)
    {
        mvfxCodeStruct(codec, type, place != nullptr ? place + i * type->size : nullptr);
    }
}

// Lays bytes aligned to alignment in the memory, zeroed; NULL for none,
// when measuring, or past the memory's capacity: a walk measured first
// fits, but the bytes may have changed since, and are never laid past it.
static uint8_t* Lay(mvfxCodec* codec, size_t bytes, size_t alignment)
{
    if (bytes == 0)
    {
        return nullptr;
    }
    codec->used = (codec->used + alignment - 1) & ~(alignment - 1);
    uint8_t* place = codec->memory != nullptr ? codec->memory + codec->used : nullptr;
    codec->used += bytes;
    if (place == nullptr)
    {
        return nullptr;
    }
    if (codec->used > codec->capacity)
    {
        mvfxCodecFault(codec, mvfx_errorCapacity, -1);
        return nullptr;
    }
    memset(place, 0, bytes);
    return place;
}

// A pointer field and its count: the count, capped, then the items.
static void CodeArray(mvfxCodec* codec, const mvfxFileField* field, uint8_t* object)
{
    uint8_t* counted = object != nullptr ? object + field->at : nullptr;
    size_t at = codec->at;
    uint64_t value = 0;
    if (codec->writing && HasSource(codec, object))
    {
        value = Load(counted, 4);
        Put(codec, value, 4);
    }
    else if (!Take(codec, 4, &value))
    {
        return;
    }
    int32_t count = (int32_t)(uint32_t)value;
    if (count < 0 || count > (int32_t)field->cap)
    {
        mvfxCodecFault(codec, mvfx_errorInvalid, codec->writing ? -1 : (int64_t)at);
        return;
    }
    bool scalar = field->element != mvfx_fieldStruct;
    size_t size = scalar ? s_scalarBytes[field->element] : field->type->size;
    size_t items = (size_t)count * field->count;
    uint8_t* place = nullptr;
    if (codec->writing && object != nullptr)
    {
        memcpy((void*)&place, object + field->offset, sizeof place);
        if (items > 0 && place == nullptr)
        {
            mvfxCodecFault(codec, mvfx_errorInvalid, -1);
            return;
        }
    }
    else
    {
        place = Lay(codec, items * size, scalar ? size : field->type->alignment);
        if (counted != nullptr)
        {
            Store(counted, value, 4);
            memcpy(object + field->offset, (const void*)&place, sizeof place);
        }
    }
    CodeItems(codec, field->element, field->type, place, items);
}

static void CodeField(mvfxCodec* codec, const mvfxFileField* field, uint8_t* object)
{
    uint8_t* place = object != nullptr ? object + field->offset : nullptr;
    switch (field->kind)
    {
    case mvfx_fieldArray:
        CodeArray(codec, field, object);
        break;
    case mvfx_fieldGrid:
    case mvfx_fieldMesh:
        for (uint32_t i = 0; i < field->count; ++i)
        {
            CodeId(codec, field->kind,
                   place != nullptr ? place + (size_t)i * sizeof(mvfxGridId) : nullptr);
        }
        break;
    case mvfx_fieldModule:
        CodeModule(codec, place, object != nullptr ? object + field->at : nullptr);
        break;
    case mvfx_fieldCookie:
        if (!codec->writing && place != nullptr)
        {
            Store(place, field->at, 4);
        }
        break;
    default:
        CodeItems(codec, field->kind, field->type, place, field->count);
        break;
    }
}

void mvfxCodeStruct(mvfxCodec* codec, const mvfxFileType* type, uint8_t* object)
{
    for (int32_t i = 0; i < type->fieldCount && codec->fault == mvfx_success; ++i)
    {
        CodeField(codec, &type->fields[i], object);
    }
}
