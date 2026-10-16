// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "allocator.h"
#include "context.h"
#include "effect.h"
#include "effect_parts.h"
#include "file_codec.h"

#include <string.h>

// The header: magic, version, size, unit count, then the payload's hash.
#define HEADER_BYTES 24
// A unit table entry: kind, offset, size, count.
#define ENTRY_BYTES 16
// The units: the three kinds of names, then the effect.
#define UNIT_COUNT  4
#define EFFECT_UNIT 3
#define UNITS_START (HEADER_BYTES + UNIT_COUNT * ENTRY_BYTES)
#define MAGIC       0x5846564Du

static const int32_t s_nameCaps[MVFX_ASSET_KINDS] = {MVFX_FILE_MAX_GRIDS, MVFX_FILE_MAX_MESHES,
                                                     MVFX_FILE_MAX_MODULES};

// A unit as the table places it.
typedef struct Unit
{
    size_t offset;
    size_t size;
    uint32_t count;
} Unit;

static mvfxFileResult Fault(mvfxResult result, int64_t offset)
{
    return (mvfxFileResult){result, offset};
}

static uint32_t Get32(const uint8_t* bytes, size_t at)
{
    return (uint32_t)bytes[at] | (uint32_t)bytes[at + 1] << 8 | (uint32_t)bytes[at + 2] << 16 |
           (uint32_t)bytes[at + 3] << 24;
}

static void Set32(uint8_t* bytes, size_t at, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
    {
        bytes[at + i] = (uint8_t)(value >> (8 * i));
    }
}

// The bytes of the well-formed UTF-8 sequence at text, left bytes on (at
// least 1), or 0 with the offset of its byte at fault in bad (RFC 3629: no
// overlong forms, no surrogates, nothing past U+10FFFF).
static size_t Sequence(const uint8_t* text, size_t left, size_t* bad)
{
    uint8_t lead = text[0];
    *bad = 0;
    if (lead < 0x80)
    {
        return 1;
    }
    size_t more = lead < 0xC2 ? 4 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
    if (more == 4 || lead > 0xF4 || more >= left)
    {
        return 0;
    }
    // The second byte's range narrows after 0xE0, 0xED, 0xF0 and 0xF4.
    uint8_t low = lead == 0xE0 ? 0xA0 : lead == 0xF0 ? 0x90 : 0x80;
    uint8_t high = lead == 0xED ? 0x9F : lead == 0xF4 ? 0x8F : 0xBF;
    for (size_t k = 1; k <= more; ++k)
    {
        if (text[k] < (k == 1 ? low : 0x80) || text[k] > (k == 1 ? high : 0xBF))
        {
            *bad = k;
            return 0;
        }
    }
    return 1 + more;
}

// The offset of the first byte that is not well-formed UTF-8, or the
// length.
static size_t Utf8Fault(const uint8_t* text, size_t length)
{
    size_t i = 0;
    while (i < length)
    {
        size_t bad = 0;
        size_t bytes = Sequence(text + i, length - i, &bad);
        if (bytes == 0)
        {
            return i + bad;
        }
        i += bytes;
    }
    return length;
}

static bool SameName(const char* a, int32_t aLength, const char* b, int32_t bLength)
{
    return aLength == bLength && memcmp(a, b, (size_t)aLength) == 0;
}

// Whether a table of assets is valid: known kinds, names UTF-8 of 1 to
// MVFX_MAX_ASSET_NAME bytes, once a kind.
static bool AreAssetsValid(const mvfxEffectAsset* assets, int32_t count)
{
    if (count < 0 || (assets == nullptr && count > 0))
    {
        return false;
    }
    for (int32_t a = 0; a < count; ++a)
    {
        const mvfxEffectAsset* asset = &assets[a];
        if (asset->kind >= MVFX_ASSET_KINDS || asset->name == nullptr || asset->nameLength < 1 ||
            asset->nameLength > MVFX_MAX_ASSET_NAME ||
            Utf8Fault((const uint8_t*)asset->name, (size_t)asset->nameLength) !=
                (size_t)asset->nameLength)
        {
            return false;
        }
        for (int32_t b = 0; b < a; ++b)
        {
            if (assets[b].kind == asset->kind &&
                SameName(assets[b].name, assets[b].nameLength, asset->name, asset->nameLength))
            {
                return false;
            }
        }
    }
    return true;
}

// The bytes of a kind's names unit as gathered.
static size_t NamesBytes(const mvfxCodec* codec, mvfxAssetKind kind)
{
    size_t bytes = 0;
    for (int32_t i = 0; i < codec->nameCount[kind]; ++i)
    {
        bytes += 4 + (size_t)codec->assets[codec->names[kind][i]].nameLength;
    }
    return bytes;
}

// Writes the header, the unit table and the names units of a file whose
// effect unit is effectBytes long.
static void WriteFrame(const mvfxCodec* codec, uint8_t* bytes, size_t size, size_t effectBytes)
{
    Set32(bytes, 0, MAGIC);
    Set32(bytes, 4, MVFX_FILE_VERSION);
    Set32(bytes, 8, (uint32_t)size);
    Set32(bytes, 12, UNIT_COUNT);
    size_t at = UNITS_START;
    for (uint32_t kind = 0; kind < UNIT_COUNT; ++kind)
    {
        size_t unit = kind < EFFECT_UNIT ? NamesBytes(codec, (mvfxAssetKind)kind) : effectBytes;
        size_t entry = HEADER_BYTES + kind * ENTRY_BYTES;
        Set32(bytes, entry, kind);
        Set32(bytes, entry + 4, (uint32_t)at);
        Set32(bytes, entry + 8, (uint32_t)unit);
        Set32(bytes, entry + 12, kind < EFFECT_UNIT ? (uint32_t)codec->nameCount[kind] : 1);
        for (int32_t i = 0; kind < EFFECT_UNIT && i < codec->nameCount[kind]; ++i)
        {
            const mvfxEffectAsset* asset = &codec->assets[codec->names[kind][i]];
            Set32(bytes, at, (uint32_t)asset->nameLength);
            memcpy(bytes + at + 4, asset->name, (size_t)asset->nameLength);
            at += 4 + (size_t)asset->nameLength;
        }
        at += kind < EFFECT_UNIT ? 0 : unit;
    }
}

static uint64_t Hash(const uint8_t* bytes, size_t size)
{
    return mvfxHash64(MVFX_HASH_INIT, bytes + HEADER_BYTES, (int32_t)(size - HEADER_BYTES));
}

mvfxFileResult mvfxWriteEffect(const mvfxEffectDef* def, const mvfxEffectAsset* assets,
                               int32_t assetCount, void* bytes, int64_t capacity, int64_t* sizeOut)
{
    if (sizeOut != nullptr)
    {
        *sizeOut = 0;
    }
    if (sizeOut == nullptr || def == nullptr || capacity < 0 ||
        (bytes == nullptr && capacity > 0) || !AreAssetsValid(assets, assetCount) ||
        !mvfxIsEffectDefValid(def) || !mvfxAreEffectPartsValid(def))
    {
        return Fault(mvfx_errorInvalid, -1);
    }
    int32_t names[MVFX_FILE_MAX_GRIDS + MVFX_FILE_MAX_MESHES + MVFX_FILE_MAX_MODULES];
    mvfxCodec codec = {.writing = true,
                       .end = SIZE_MAX,
                       .faultAt = -1,
                       .assets = assets,
                       .assetCount = assetCount,
                       .names = {names, names + MVFX_FILE_MAX_GRIDS,
                                 names + MVFX_FILE_MAX_GRIDS + MVFX_FILE_MAX_MESHES}};
    memcpy(codec.nameCap, s_nameCaps, sizeof s_nameCaps);
    // Measured first, gathering the names; then written after them.
    mvfxCodeStruct(&codec, mvfxFileEffectDef, (uint8_t*)def);
    if (codec.fault != mvfx_success)
    {
        return Fault(codec.fault, -1);
    }
    size_t effectBytes = codec.at;
    size_t size = UNITS_START + effectBytes;
    for (mvfxAssetKind kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        size += NamesBytes(&codec, kind);
    }
    if (size > INT32_MAX)
    {
        return Fault(mvfx_errorCapacity, -1);
    }
    *sizeOut = (int64_t)size;
    if ((int64_t)size > capacity)
    {
        return Fault(mvfx_errorCapacity, -1);
    }
    uint8_t* out = bytes;
    WriteFrame(&codec, out, size, effectBytes);
    codec.out = out;
    codec.at = size - effectBytes;
    codec.end = size;
    mvfxCodeStruct(&codec, mvfxFileEffectDef, (uint8_t*)def);
    uint64_t hash = Hash(out, size);
    Set32(out, 16, (uint32_t)hash);
    Set32(out, 20, (uint32_t)(hash >> 32));
    return Fault(mvfx_success, -1);
}

// Checks the header and the hash.
static mvfxFileResult ReadHeader(const uint8_t* bytes, size_t size)
{
    if (size < 4 || Get32(bytes, 0) != MAGIC)
    {
        return Fault(mvfx_errorInvalid, 0);
    }
    if (size < 8 || Get32(bytes, 4) != MVFX_FILE_VERSION)
    {
        return Fault(size < 8 ? mvfx_errorInvalid : mvfx_errorVersion, 4);
    }
    if (size < UNITS_START || size > INT32_MAX || Get32(bytes, 8) != size)
    {
        return Fault(mvfx_errorInvalid, 8);
    }
    if (Get32(bytes, 12) != UNIT_COUNT)
    {
        return Fault(mvfx_errorInvalid, 12);
    }
    uint64_t hash = Hash(bytes, size);
    if (Get32(bytes, 16) != (uint32_t)hash || Get32(bytes, 20) != (uint32_t)(hash >> 32))
    {
        return Fault(mvfx_errorInvalid, 16);
    }
    return Fault(mvfx_success, -1);
}

// Checks the unit table: the kinds in order, each unit where the last
// ends, within the file, its count within its cap, the last ending with
// the file.
static mvfxFileResult ReadTable(const uint8_t* bytes, size_t size, Unit units[UNIT_COUNT])
{
    size_t at = UNITS_START;
    for (uint32_t kind = 0; kind < UNIT_COUNT; ++kind)
    {
        size_t entry = HEADER_BYTES + kind * ENTRY_BYTES;
        Unit* unit = &units[kind];
        *unit = (Unit){Get32(bytes, entry + 4), Get32(bytes, entry + 8), Get32(bytes, entry + 12)};
        uint32_t cap = kind < EFFECT_UNIT ? (uint32_t)s_nameCaps[kind] : 1;
        if (Get32(bytes, entry) != kind)
        {
            return Fault(mvfx_errorInvalid, (int64_t)entry);
        }
        if (unit->offset != at)
        {
            return Fault(mvfx_errorInvalid, (int64_t)entry + 4);
        }
        if (unit->size > size - at || (kind == EFFECT_UNIT && unit->size != size - at))
        {
            return Fault(mvfx_errorInvalid, (int64_t)entry + 8);
        }
        if (unit->count > cap || (kind == EFFECT_UNIT && unit->count != 1))
        {
            return Fault(mvfx_errorInvalid, (int64_t)entry + 12);
        }
        at += unit->size;
    }
    return Fault(mvfx_success, -1);
}

// The offset of a names unit's name after the one at at.
static size_t NextName(const uint8_t* bytes, size_t at)
{
    return at + 4 + Get32(bytes, at);
}

// Checks a names unit alone: each name's length, its bytes UTF-8 and in
// the unit, none twice, the unit ending with the last.
static mvfxFileResult ReadNames(const uint8_t* bytes, const Unit* unit)
{
    size_t end = unit->offset + unit->size;
    size_t at = unit->offset;
    for (uint32_t i = 0; i < unit->count; ++i)
    {
        if (end - at < 4)
        {
            return Fault(mvfx_errorInvalid, (int64_t)at);
        }
        uint32_t length = Get32(bytes, at);
        if (length < 1 || length > MVFX_MAX_ASSET_NAME || length > end - at - 4)
        {
            return Fault(mvfx_errorInvalid, (int64_t)at);
        }
        size_t bad = Utf8Fault(bytes + at + 4, length);
        if (bad != length)
        {
            return Fault(mvfx_errorInvalid, (int64_t)(at + 4 + bad));
        }
        for (size_t before = unit->offset; before < at; before = NextName(bytes, before))
        {
            if (SameName((const char*)bytes + before + 4, (int32_t)Get32(bytes, before),
                         (const char*)bytes + at + 4, (int32_t)length))
            {
                return Fault(mvfx_errorInvalid, (int64_t)at);
            }
        }
        at += 4 + length;
    }
    return Fault(at == end ? mvfx_success : mvfx_errorInvalid, at == end ? -1 : (int64_t)at);
}

// Binds each name of each names unit to the asset of its kind named so.
static mvfxFileResult Bind(const uint8_t* bytes, const Unit units[UNIT_COUNT],
                           const mvfxEffectAsset* assets, int32_t assetCount, int32_t* names)
{
    for (mvfxAssetKind kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        size_t at = units[kind].offset;
        for (uint32_t i = 0; i < units[kind].count; ++i)
        {
            int32_t found = -1;
            for (int32_t a = 0; a < assetCount && found < 0; ++a)
            {
                found = assets[a].kind == kind &&
                                SameName(assets[a].name, assets[a].nameLength,
                                         (const char*)bytes + at + 4, (int32_t)Get32(bytes, at))
                            ? a
                            : -1;
            }
            if (found < 0)
            {
                return Fault(mvfx_errorMissing, (int64_t)at);
            }
            *names++ = found;
            at = NextName(bytes, at);
        }
    }
    return Fault(mvfx_success, -1);
}

// Walks the effect unit: checking and measuring without a def, reading
// into one with names bound, its arrays in memory of the bytes measured.
static mvfxFileResult ReadDef(const uint8_t* bytes, const Unit units[UNIT_COUNT],
                              const mvfxEffectAsset* assets, int32_t* names, uint8_t* memory,
                              mvfxEffectDef* def, size_t* used)
{
    mvfxCodec codec = {.in = bytes,
                       .at = units[EFFECT_UNIT].offset,
                       .end = units[EFFECT_UNIT].offset + units[EFFECT_UNIT].size,
                       .memory = memory,
                       .faultAt = -1,
                       .assets = assets};
    size_t first = 0;
    for (int32_t kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        codec.names[kind] = names != nullptr ? names + first : nullptr;
        codec.nameCap[kind] = (int32_t)units[kind].count;
        first += units[kind].count;
    }
    mvfxCodeStruct(&codec, mvfxFileEffectDef, (uint8_t*)def);
    *used = codec.used;
    if (codec.fault != mvfx_success)
    {
        return Fault(codec.fault, codec.faultAt);
    }
    if (codec.at != codec.end)
    {
        return Fault(mvfx_errorInvalid, (int64_t)codec.at);
    }
    // Every name is referred to: the first one never is lies at fault.
    for (int32_t kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        size_t at = units[kind].offset;
        for (int32_t i = 0; i < codec.nameCount[kind]; ++i)
        {
            at = NextName(bytes, at);
        }
        if (codec.nameCount[kind] != codec.nameCap[kind])
        {
            return Fault(mvfx_errorInvalid, (int64_t)at);
        }
    }
    return Fault(mvfx_success, -1);
}

// Checks a file up to the effect unit's walk, measuring its memory and
// binding its names.
static mvfxFileResult CheckFile(const uint8_t* bytes, size_t size, const mvfxEffectAsset* assets,
                                int32_t assetCount, Unit units[UNIT_COUNT], int32_t* names,
                                size_t* needed)
{
    mvfxFileResult result = ReadHeader(bytes, size);
    if (result.result == mvfx_success)
    {
        result = ReadTable(bytes, size, units);
    }
    for (int32_t kind = 0; kind < MVFX_ASSET_KINDS && result.result == mvfx_success; ++kind)
    {
        result = ReadNames(bytes, &units[kind]);
    }
    if (result.result == mvfx_success)
    {
        result = ReadDef(bytes, units, assets, nullptr, nullptr, nullptr, needed);
    }
    if (result.result == mvfx_success)
    {
        result = Bind(bytes, units, assets, assetCount, names);
    }
    return result;
}

mvfxFileResult mvfxReadEffect(const void* bytes, int64_t size, const mvfxEffectAsset* assets,
                              int32_t assetCount, void* memory, int64_t capacity,
                              int64_t* neededOut, mvfxEffectDef* defOut)
{
    if (neededOut != nullptr)
    {
        *neededOut = 0;
    }
    if (defOut != nullptr)
    {
        *defOut = (mvfxEffectDef){0};
    }
    if (neededOut == nullptr || defOut == nullptr || size < 0 || (bytes == nullptr && size > 0) ||
        capacity < 0 || (memory == nullptr && capacity > 0) || (uintptr_t)memory % 16 != 0 ||
        !AreAssetsValid(assets, assetCount))
    {
        return Fault(mvfx_errorInvalid, -1);
    }
    Unit units[UNIT_COUNT] = {0};
    int32_t names[MVFX_FILE_MAX_GRIDS + MVFX_FILE_MAX_MESHES + MVFX_FILE_MAX_MODULES];
    size_t needed = 0;
    mvfxFileResult result =
        CheckFile(bytes, (size_t)size, assets, assetCount, units, names, &needed);
    if (result.result != mvfx_success)
    {
        return result;
    }
    *neededOut = (int64_t)needed;
    if ((int64_t)needed > capacity)
    {
        return Fault(mvfx_errorCapacity, -1);
    }
    result = ReadDef(bytes, units, assets, names, memory, defOut, &needed);
    if (result.result == mvfx_success &&
        (!mvfxIsEffectDefValid(defOut) || !mvfxAreEffectPartsValid(defOut)))
    {
        result = Fault(mvfx_errorInvalid, (int64_t)units[EFFECT_UNIT].offset);
    }
    if (result.result != mvfx_success)
    {
        *defOut = (mvfxEffectDef){0};
    }
    return result;
}

mvfxFileResult mvfxCreateEffectFromBytes(mvfxContext* context, const void* bytes, int64_t size,
                                         const mvfxEffectAsset* assets, int32_t assetCount,
                                         mvfxEffectId* effectOut)
{
    if (context == nullptr || effectOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return Fault(mvfx_errorInvalid, -1);
    }
    *effectOut = (mvfxEffectId){0};
    mvfxEffectDef def;
    int64_t needed = 0;
    mvfxFileResult result =
        mvfxReadEffect(bytes, size, assets, assetCount, nullptr, 0, &needed, &def);
    void* memory = nullptr;
    if (result.result == mvfx_errorCapacity)
    {
        memory = mvfxAllocate(&context->allocator, (size_t)needed, 16);
        result = memory == nullptr ? Fault(mvfx_errorCapacity, -1)
                                   : mvfxReadEffect(bytes, size, assets, assetCount, memory, needed,
                                                    &needed, &def);
    }
    if (result.result == mvfx_success)
    {
        result = Fault(mvfxCreateEffect(context, &def, effectOut), -1);
    }
    mvfxRelease(&context->allocator, memory, (size_t)needed, 16);
    return result;
}
