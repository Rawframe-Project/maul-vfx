// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "file_codec.h"

#include "invariant.h"

#include <string.h>

static_assert(sizeof(bool) == 1, "a bool is one byte, as the file's");
static_assert(sizeof(mvfxGridId) == sizeof(mvfxMeshId), "ids are coded alike");

// The bytes of each scalar kind.
static const uint8_t s_scalarBytes[] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8};

// A field's head: its tag and its payload's length. A list's head is its
// count and a zero word, an item's its length and a zero word.
#define HEAD_BYTES 8

void mvfxCodecFault(mvfxCodec* codec, mvfxResult fault, int64_t offset)
{
    if (codec->fault == mvfx_success)
    {
        codec->fault = fault;
        codec->faultAt = offset;
        (void)mvfxRefuseHere(&codec->path, fault);
    }
}

// Steps the walk's path into a field, and back out.
static void Enter(mvfxCodec* codec, uint32_t tag)
{
    MVFX_ASSERT(codec->path.depth < MVFX_MAX_FIELD_DEPTH);
    codec->path.tags[codec->path.depth] = tag;
    codec->path.items[codec->path.depth] = -1;
    codec->path.depth += 1;
}

static void Leave(mvfxCodec* codec)
{
    codec->path.depth -= 1;
}

// Names an item of the field the walk is in, -1 for none.
static void AtItem(mvfxCodec* codec, int32_t item)
{
    codec->path.items[codec->path.depth - 1] = item;
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

// Sets the 32-bit value put at place.
static void Patch(mvfxCodec* codec, size_t place, uint32_t value)
{
    for (size_t i = 0; codec->out != nullptr && codec->fault == mvfx_success && i < 4; ++i)
    {
        codec->out[place + i] = (uint8_t)(value >> (8 * i));
    }
}

// Puts zero bytes to the next multiple of the alignment.
static void PutPadding(mvfxCodec* codec)
{
    while (codec->at % MVFX_FILE_ALIGNMENT != 0 && codec->fault == mvfx_success)
    {
        Put(codec, 0, 1);
    }
}

// Takes a little-endian value of some bytes; false past the end.
static bool Take(mvfxCodec* codec, size_t bytes, uint64_t* value)
{
    if (codec->fault != mvfx_success)
    {
        return false;
    }
    if (bytes > codec->end - codec->at)
    {
        mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)codec->end);
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

// Takes a word that must be 0.
static void TakeZero(mvfxCodec* codec)
{
    size_t at = codec->at;
    uint64_t value = 0;
    if (Take(codec, 4, &value) && value != 0)
    {
        mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)at);
    }
}

// Takes the zero bytes to the next multiple of the alignment.
static void TakePadding(mvfxCodec* codec)
{
    while (codec->at % MVFX_FILE_ALIGNMENT != 0 && codec->fault == mvfx_success)
    {
        size_t at = codec->at;
        uint64_t value = 0;
        if (Take(codec, 1, &value) && value != 0)
        {
            mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)at);
        }
    }
}

// The bytes a field of scalars, ids or a module's function takes in its
// struct.
static size_t ValueBytes(const mvfxFileField* field)
{
    switch (field->kind)
    {
    case mvfx_fieldGrid:
    case mvfx_fieldMesh:
        return field->count * sizeof(mvfxGridId);
    case mvfx_fieldModule:
        return sizeof(mvfxModuleFunction*);
    default:
        return field->count * (size_t)s_scalarBytes[field->kind];
    }
}

static bool IsDefault(const mvfxFileType* type, const uint8_t* object, const uint8_t* base);

// Whether a field's bits are its default's: an array's when it has no
// items, as no default has; a cookie's always, as it is never stored.
static bool FieldIsDefault(const mvfxFileField* field, const uint8_t* object, const uint8_t* base)
{
    const uint8_t* place = object + field->offset;
    const uint8_t* was = base + field->offset;
    switch (field->kind)
    {
    case mvfx_fieldArray:
        MVFX_ASSERT(Load(base + field->at, 4) == 0);
        return Load(object + field->at, 4) == 0;
    case mvfx_fieldStruct:
        for (uint32_t i = 0; i < field->count; ++i)
        {
            size_t item = (size_t)i * field->type->size;
            if (!IsDefault(field->type, place + item, was + item))
            {
                return false;
            }
        }
        return true;
    case mvfx_fieldModule:
        return memcmp(place, was, ValueBytes(field)) == 0 &&
               memcmp(object + field->at, base + field->at, sizeof(void*)) == 0;
    case mvfx_fieldCookie:
        return true;
    default:
        return memcmp(place, was, ValueBytes(field)) == 0;
    }
}

static bool IsDefault(const mvfxFileType* type, const uint8_t* object, const uint8_t* base)
{
    for (int32_t i = 0; i < type->fieldCount; ++i)
    {
        if (!FieldIsDefault(&type->fields[i], object, base))
        {
            return false;
        }
    }
    return true;
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
        mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)at);
        return -1;
    }
    codec->nameCount[kind] += index == codec->nameCount[kind] ? 1 : 0;
    return codec->names[kind] != nullptr ? codec->names[kind][index] : -1;
}

static mvfxAssetKind IdKind(mvfxFieldKind field)
{
    return field == mvfx_fieldGrid ? mvfx_assetGrid : mvfx_assetMesh;
}

// Puts the name of a grid's or a mesh's id at place.
static void PutId(mvfxCodec* codec, mvfxFieldKind field, const uint8_t* place)
{
    mvfxAssetKind kind = IdKind(field);
    mvfxEffectAsset like = {0};
    memcpy(&like.grid, place, sizeof like.grid);
    like.mesh = (mvfxMeshId){like.grid.index1, like.grid.generation};
    int32_t asset = like.grid.index1 == 0 ? -1 : FindAsset(codec, kind, &like);
    if (like.grid.index1 != 0 && asset < 0)
    {
        mvfxCodecFault(codec, mvfx_errorMissing, -1);
    }
    PutName(codec, kind, asset);
}

// Takes the name of a grid or a mesh into the id at place.
static void TakeId(mvfxCodec* codec, mvfxFieldKind field, uint8_t* place)
{
    mvfxAssetKind kind = IdKind(field);
    int32_t asset = TakeName(codec, kind);
    if (place != nullptr && asset >= 0)
    {
        const mvfxEffectAsset* bound = &codec->assets[asset];
        memcpy(place,
               kind == mvfx_assetGrid ? (const void*)&bound->grid : (const void*)&bound->mesh,
               sizeof bound->grid);
    }
}

// Puts the name of the host module whose function is at place and whose
// user pointer is at user.
static void PutModule(mvfxCodec* codec, const uint8_t* place, const uint8_t* user)
{
    mvfxEffectAsset like = {0};
    memcpy((void*)&like.function, place, sizeof like.function);
    memcpy((void*)&like.user, user, sizeof like.user);
    int32_t asset = like.function == nullptr ? -1 : FindAsset(codec, mvfx_assetModule, &like);
    if (like.function != nullptr && asset < 0)
    {
        mvfxCodecFault(codec, mvfx_errorMissing, -1);
    }
    PutName(codec, mvfx_assetModule, asset);
}

// Takes a host module's name into its function at place and its user
// pointer at user.
static void TakeModule(mvfxCodec* codec, uint8_t* place, uint8_t* user)
{
    int32_t asset = TakeName(codec, mvfx_assetModule);
    if (place != nullptr && asset >= 0)
    {
        memcpy(place, (const void*)&codec->assets[asset].function,
               sizeof codec->assets[asset].function);
        memcpy(user, (const void*)&codec->assets[asset].user, sizeof codec->assets[asset].user);
    }
}

static void PutScalars(mvfxCodec* codec, mvfxFieldKind kind, const uint8_t* place, size_t count)
{
    size_t bytes = s_scalarBytes[kind];
    for (size_t i = 0; i < count; ++i)
    {
        Put(codec, Load(place + i * bytes, bytes), bytes);
    }
}

static void TakeScalars(mvfxCodec* codec, mvfxFieldKind kind, uint8_t* place, size_t count)
{
    size_t bytes = s_scalarBytes[kind];
    for (size_t i = 0; i < count; ++i)
    {
        size_t at = codec->at;
        uint64_t value = 0;
        if (!Take(codec, bytes, &value))
        {
            return;
        }
        if (kind == mvfx_fieldBool && value > 1)
        {
            mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)at);
            return;
        }
        if (place != nullptr)
        {
            Store(place + i * bytes, value, bytes);
        }
    }
}

// Puts a list of count structs: its count and a zero word, then each item's
// length, a zero word and its fields that differ from its base's (bases
// step baseStep bytes an item).
static void PutItems(mvfxCodec* codec, const mvfxFileType* type, const uint8_t* items,
                     const uint8_t* bases, size_t baseStep, size_t count)
{
    Put(codec, count, 4);
    Put(codec, 0, 4);
    for (size_t i = 0; i < count && codec->fault == mvfx_success; ++i)
    {
        size_t head = codec->at;
        Put(codec, 0, 4);
        Put(codec, 0, 4);
        AtItem(codec, (int32_t)i);
        mvfxWriteFields(codec, type, items + i * type->size, bases + i * baseStep);
        Patch(codec, head, (uint32_t)(codec->at - head - HEAD_BYTES));
    }
    AtItem(codec, -1);
}

// An array: its count and a zero word, then its items, scalars packed.
static void PutArray(mvfxCodec* codec, const mvfxFileField* field, const uint8_t* object)
{
    uint32_t count = (uint32_t)Load(object + field->at, 4);
    const uint8_t* items = nullptr;
    memcpy((void*)&items, object + field->offset, sizeof items);
    if (count > field->cap || (count > 0 && items == nullptr))
    {
        mvfxCodecFault(codec, mvfx_errorInvalid, -1);
        return;
    }
    if (field->element != mvfx_fieldStruct)
    {
        Put(codec, count, 4);
        Put(codec, 0, 4);
        PutScalars(codec, field->element, items, (size_t)count * field->count);
        return;
    }
    PutItems(codec, field->type, items, codec->defaults + field->type->defaultAt, 0, count);
}

void mvfxPutPayload(mvfxCodec* codec, const mvfxFileField* field, const uint8_t* object,
                    const uint8_t* base)
{
    const uint8_t* place = object + field->offset;
    Enter(codec, field->tag);
    switch (field->kind)
    {
    case mvfx_fieldArray:
        PutArray(codec, field, object);
        break;
    case mvfx_fieldStruct:
        if (field->count == 1)
        {
            mvfxWriteFields(codec, field->type, place, base + field->offset);
        }
        else
        {
            PutItems(codec, field->type, place, base + field->offset, field->type->size,
                     field->count);
        }
        break;
    case mvfx_fieldGrid:
    case mvfx_fieldMesh:
        for (uint32_t i = 0; i < field->count; ++i)
        {
            PutId(codec, field->kind, place + (size_t)i * sizeof(mvfxGridId));
        }
        break;
    case mvfx_fieldModule:
        PutModule(codec, place, object + field->at);
        break;
    default:
        PutScalars(codec, field->kind, place, field->count);
        break;
    }
    Leave(codec);
}

// A field that differs from its default: its tag, its payload's length,
// the payload and zero bytes to the alignment.
static void PutField(mvfxCodec* codec, const mvfxFileField* field, const uint8_t* object,
                     const uint8_t* base)
{
    if (FieldIsDefault(field, object, base))
    {
        return;
    }
    size_t head = codec->at;
    Put(codec, field->tag, 4);
    Put(codec, 0, 4);
    mvfxPutPayload(codec, field, object, base);
    Patch(codec, head + 4, (uint32_t)(codec->at - head - HEAD_BYTES));
    PutPadding(codec);
}

void mvfxWriteFields(mvfxCodec* codec, const mvfxFileType* type, const uint8_t* object,
                     const uint8_t* base)
{
    for (int32_t i = 0; i < type->fieldCount && codec->fault == mvfx_success; ++i)
    {
        PutField(codec, &type->fields[i], object, base);
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

// Takes a list's count and its zero word: the count, at most most (exactly
// most when exact), or -1.
static int64_t TakeCount(mvfxCodec* codec, uint32_t most, bool exact)
{
    size_t at = codec->at;
    uint64_t value = 0;
    if (!Take(codec, 4, &value))
    {
        return -1;
    }
    if (value > most || (exact && value != most))
    {
        mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)at);
        return -1;
    }
    TakeZero(codec);
    return codec->fault == mvfx_success ? (int64_t)value : -1;
}

// Takes count structs, each its length, a zero word and its fields, into
// items (NULL to check only), which hold their defaults.
static void TakeItems(mvfxCodec* codec, const mvfxFileType* type, uint8_t* items, size_t count)
{
    size_t end = codec->end;
    for (size_t i = 0; i < count && codec->fault == mvfx_success; ++i)
    {
        size_t at = codec->at;
        uint64_t length = 0;
        if (!Take(codec, 4, &length))
        {
            return;
        }
        TakeZero(codec);
        AtItem(codec, (int32_t)i);
        if (codec->fault == mvfx_success && length > end - codec->at)
        {
            mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)at);
            return;
        }
        mvfxReadFields(codec, type, items != nullptr ? items + i * type->size : nullptr,
                       codec->at + length);
    }
    AtItem(codec, -1);
}

// An array: its count, a zero word and its items, laid in the memory, each
// struct item from its default.
static void TakeArray(mvfxCodec* codec, const mvfxFileField* field, uint8_t* object,
                      size_t lengthAt)
{
    int64_t count = TakeCount(codec, field->cap, false);
    if (count < 0)
    {
        return;
    }
    bool scalar = field->element != mvfx_fieldStruct;
    size_t size = scalar ? s_scalarBytes[field->element] : field->type->size;
    size_t items = (size_t)count * field->count;
    if (scalar && codec->end - codec->at != items * size)
    {
        mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)lengthAt);
        return;
    }
    uint8_t* place = Lay(codec, items * size, scalar ? size : field->type->alignment);
    if (object != nullptr)
    {
        Store(object + field->at, (uint64_t)count, 4);
        memcpy(object + field->offset, (const void*)&place, sizeof place);
    }
    if (scalar)
    {
        TakeScalars(codec, field->element, place, items);
        return;
    }
    for (size_t i = 0; place != nullptr && i < items; ++i)
    {
        memcpy(place + i * size, codec->defaults + field->type->defaultAt, size);
    }
    TakeItems(codec, field->type, place, items);
}

// A field's payload, which ends at the codec's end; its length is at
// lengthAt.
static void TakePayload(mvfxCodec* codec, const mvfxFileField* field, uint8_t* object,
                        size_t lengthAt)
{
    uint8_t* place = object != nullptr ? object + field->offset : nullptr;
    size_t length = codec->end - codec->at;
    switch (field->kind)
    {
    case mvfx_fieldArray:
        TakeArray(codec, field, object, lengthAt);
        return;
    case mvfx_fieldStruct:
        if (field->count == 1)
        {
            mvfxReadFields(codec, field->type, place, codec->end);
            return;
        }
        if (TakeCount(codec, field->count, true) >= 0)
        {
            TakeItems(codec, field->type, place, field->count);
        }
        return;
    default:
        break;
    }
    bool id = field->kind == mvfx_fieldGrid || field->kind == mvfx_fieldMesh;
    size_t item = id || field->kind == mvfx_fieldModule ? 4 : s_scalarBytes[field->kind];
    if (length != field->count * item)
    {
        mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)lengthAt);
        return;
    }
    if (field->kind == mvfx_fieldModule)
    {
        TakeModule(codec, place, object != nullptr ? object + field->at : nullptr);
        return;
    }
    for (uint32_t i = 0; id && i < field->count; ++i)
    {
        TakeId(codec, field->kind,
               place != nullptr ? place + (size_t)i * sizeof(mvfxGridId) : nullptr);
    }
    if (!id)
    {
        TakeScalars(codec, field->kind, place, field->count);
    }
}

// TakePayload, the walk's path in the field.
static void TakeField(mvfxCodec* codec, const mvfxFileField* field, uint8_t* object,
                      size_t lengthAt)
{
    Enter(codec, field->tag);
    TakePayload(codec, field, object, lengthAt);
    Leave(codec);
}

void mvfxReadFields(mvfxCodec* codec, const mvfxFileType* type, uint8_t* object, size_t end)
{
    size_t outer = codec->end;
    codec->end = end;
    uint64_t last = 0;
    int32_t next = 0;
    while (codec->at < end && codec->fault == mvfx_success)
    {
        size_t head = codec->at;
        uint64_t tag = 0;
        uint64_t length = 0;
        if (!Take(codec, 4, &tag) || !Take(codec, 4, &length))
        {
            break;
        }
        if (tag <= last)
        {
            mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)head);
            break;
        }
        while (next < type->fieldCount && type->fields[next].tag < tag)
        {
            ++next;
        }
        // Only a later version writes a tag this one does not know.
        if (next == type->fieldCount || type->fields[next].tag != tag)
        {
            mvfxCodecFault(codec, mvfx_errorVersion, (int64_t)head);
            break;
        }
        if (length > end - codec->at)
        {
            mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)head + 4);
            break;
        }
        size_t payload = codec->at + length;
        codec->end = payload;
        TakeField(codec, &type->fields[next], object, head + 4);
        if (codec->fault == mvfx_success && codec->at != payload)
        {
            mvfxCodecFault(codec, mvfx_errorFormat, (int64_t)codec->at);
        }
        codec->end = end;
        TakePadding(codec);
        last = tag;
    }
    codec->end = outer;
}
