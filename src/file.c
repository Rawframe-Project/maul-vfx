// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "allocator.h"
#include "context.h"
#include "effect.h"
#include "effect_api.h"
#include "effect_check.h"
#include "effect_parts.h"
#include "field_path.h"
#include "file_codec.h"
#include "fpenv.h"
#include "refusal.h"

#include <string.h>

// The header: magic, version, size, section count, then the hash of
// everything after it.
#define HEADER_BYTES 24
// A section table entry: tag, offset, length.
#define ENTRY_BYTES 12
// The sections' tags: a kind of names is its mvfxAssetKind plus 1, then
// the effect, the last.
#define EFFECT_TAG    4
#define SECTION_COUNT 4
#define MAGIC         0x5846564Du

static const int32_t s_nameCaps[MVFX_ASSET_KINDS] = {MVFX_FILE_MAX_GRIDS, MVFX_FILE_MAX_MESHES,
                                                     MVFX_FILE_MAX_MODULES};

// A section as the table places it (size 0 for one absent), and a names
// section's count.
typedef struct Section
{
    size_t offset;
    size_t size;
    uint32_t count;
} Section;

static size_t Padded(size_t at)
{
    return (at + MVFX_FILE_ALIGNMENT - 1) & ~(size_t)(MVFX_FILE_ALIGNMENT - 1);
}

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

// Whether a table of assets is valid: known kinds, names UTF-8 of at least
// 1 byte, once a kind (else mvfx_errorInvalid); names of at most
// MVFX_MAX_ASSET_NAME bytes (else mvfx_errorLimit).
static mvfxResult CheckAssets(const mvfxEffectAsset* assets, int32_t count)
{
    if (count < 0 || (assets == nullptr && count > 0))
    {
        return mvfx_errorInvalid;
    }
    for (int32_t a = 0; a < count; ++a)
    {
        const mvfxEffectAsset* asset = &assets[a];
        if (!mvfxIsKind(asset->kind, MVFX_ASSET_KINDS - 1) || asset->name == nullptr ||
            asset->nameLength < 1)
        {
            return mvfx_errorInvalid;
        }
        // The length bounds the bytes read below.
        if (asset->nameLength > MVFX_MAX_ASSET_NAME)
        {
            return mvfx_errorLimit;
        }
        if (Utf8Fault((const uint8_t*)asset->name, (size_t)asset->nameLength) !=
            (size_t)asset->nameLength)
        {
            return mvfx_errorInvalid;
        }
        for (int32_t b = 0; b < a; ++b)
        {
            if (assets[b].kind == asset->kind &&
                SameName(assets[b].name, assets[b].nameLength, asset->name, asset->nameLength))
            {
                return mvfx_errorInvalid;
            }
        }
    }
    return mvfx_success;
}

// The bytes of the section of a kind's names as gathered, 0 when it has
// none and is left out: the count, then each name's length and bytes.
static size_t NamesBytes(const mvfxCodec* codec, mvfxAssetKind kind)
{
    size_t bytes = codec->nameCount[kind] > 0 ? 4 : 0;
    for (int32_t i = 0; i < codec->nameCount[kind]; ++i)
    {
        bytes += 4 + (size_t)codec->assets[codec->names[kind][i]].nameLength;
    }
    return bytes;
}

// The sections a file of an effect section of effectBytes holds, each
// placed after the last, and the offset where the last ends.
static size_t PlaceSections(const mvfxCodec* codec, size_t effectBytes,
                            Section sections[SECTION_COUNT], uint32_t* count)
{
    *count = 1;
    for (mvfxAssetKind kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        *count += codec->nameCount[kind] > 0 ? 1 : 0;
    }
    size_t at = Padded(HEADER_BYTES + *count * ENTRY_BYTES);
    for (uint32_t s = 0; s < SECTION_COUNT; ++s)
    {
        size_t bytes = s + 1 < EFFECT_TAG ? NamesBytes(codec, (mvfxAssetKind)s) : effectBytes;
        sections[s] = (Section){at, bytes, 0};
        at = Padded(at + bytes);
    }
    return at;
}

// Writes the header, the section table and the names sections, every byte
// between them 0.
static void WriteFrame(const mvfxCodec* codec, uint8_t* bytes, size_t size, uint32_t count,
                       const Section sections[SECTION_COUNT])
{
    memset(bytes, 0, size);
    Set32(bytes, 0, MAGIC);
    Set32(bytes, 4, MVFX_FILE_VERSION);
    Set32(bytes, 8, (uint32_t)size);
    Set32(bytes, 12, count);
    size_t entry = HEADER_BYTES;
    for (uint32_t s = 0; s < SECTION_COUNT; ++s)
    {
        if (s + 1 < EFFECT_TAG && codec->nameCount[s] == 0)
        {
            continue;
        }
        Set32(bytes, entry, s + 1);
        Set32(bytes, entry + 4, (uint32_t)sections[s].offset);
        Set32(bytes, entry + 8, (uint32_t)sections[s].size);
        entry += ENTRY_BYTES;
        if (s + 1 == EFFECT_TAG)
        {
            continue;
        }
        size_t at = sections[s].offset;
        Set32(bytes, at, (uint32_t)codec->nameCount[s]);
        at += 4;
        for (int32_t i = 0; i < codec->nameCount[s]; ++i)
        {
            const mvfxEffectAsset* asset = &codec->assets[codec->names[s][i]];
            Set32(bytes, at, (uint32_t)asset->nameLength);
            memcpy(bytes + at + 4, asset->name, (size_t)asset->nameLength);
            at += 4 + (size_t)asset->nameLength;
        }
    }
}

// The hash of everything after the header: mvfxHash64 with its words read
// little-endian, as the file's are, so it is the same on every host
// (mvfxHash64 itself reads the host's order).
static uint64_t Hash(const uint8_t* bytes, size_t size)
{
    uint64_t hash = MVFX_HASH_INIT;
    size_t i = HEADER_BYTES;
    for (; i + 8 <= size; i += 8)
    {
        uint64_t word = (uint64_t)Get32(bytes, i) | (uint64_t)Get32(bytes, i + 4) << 32;
        hash = (hash ^ word) * 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 32;
    }
    for (; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }
    return hash;
}

static mvfxFileResult WriteEffect(const mvfxEffectDef* def, const mvfxEffectAsset* assets,
                                  int32_t assetCount, void* bytes, int64_t capacity,
                                  int64_t* sizeOut, mvfxEffectRefusal* refusal)
{
    if (sizeOut == nullptr || def == nullptr || capacity < 0 || (bytes == nullptr && capacity > 0))
    {
        return Fault(mvfx_errorInvalid, -1);
    }
    mvfxFieldPath root = mvfxFieldRoot(refusal);
    mvfxResult valid = CheckAssets(assets, assetCount);
    valid = valid != mvfx_success ? valid : mvfxCheckEffectDef(&root, def);
    valid = valid != mvfx_success ? valid : mvfxCheckEffectParts(&root, def);
    if (valid != mvfx_success)
    {
        return Fault(valid, -1);
    }
    int32_t names[MVFX_FILE_MAX_GRIDS + MVFX_FILE_MAX_MESHES + MVFX_FILE_MAX_MODULES];
    const uint8_t* base = mvfxFileDefaults + mvfxFileEffectDef->defaultAt;
    mvfxCodec codec = {.end = SIZE_MAX,
                       .defaults = mvfxFileDefaults,
                       .faultAt = -1,
                       .path = root,
                       .assets = assets,
                       .assetCount = assetCount,
                       .names = {names, names + MVFX_FILE_MAX_GRIDS,
                                 names + MVFX_FILE_MAX_GRIDS + MVFX_FILE_MAX_MESHES}};
    memcpy(codec.nameCap, s_nameCaps, sizeof s_nameCaps);
    // Measured first, gathering the names; then written after them.
    mvfxWriteFields(&codec, mvfxFileEffectDef, (const uint8_t*)def, base);
    if (codec.fault != mvfx_success)
    {
        return Fault(codec.fault, -1);
    }
    Section sections[SECTION_COUNT];
    uint32_t count = 0;
    size_t size = PlaceSections(&codec, codec.at, sections, &count);
    if (size > INT32_MAX)
    {
        return Fault(mvfx_errorCapacity, -1);
    }
    *sizeOut = (int64_t)size;
    // No buffer is a capacity of 0, which a file of at least its header
    // passes.
    if ((int64_t)size > capacity || bytes == nullptr)
    {
        return Fault(mvfx_errorCapacity, -1);
    }
    uint8_t* out = bytes;
    WriteFrame(&codec, out, size, count, sections);
    codec.out = out;
    codec.at = sections[EFFECT_TAG - 1].offset;
    codec.end = size;
    mvfxWriteFields(&codec, mvfxFileEffectDef, (const uint8_t*)def, base);
    uint64_t hash = Hash(out, size);
    Set32(out, 16, (uint32_t)hash);
    Set32(out, 20, (uint32_t)(hash >> 32));
    return Fault(mvfx_success, -1);
}

static MVFX_NOINLINE mvfxFileResult mvfxWriteEffectWork(const mvfxEffectDef* def,
                                                        const mvfxEffectAsset* assets,
                                                        int32_t assetCount, void* bytes,
                                                        int64_t capacity, int64_t* sizeOut,
                                                        mvfxEffectRefusal* refusalOut)
{
    if (sizeOut != nullptr)
    {
        *sizeOut = 0;
    }
    mvfxEffectRefusal refusal;
    mvfxClearRefusal(&refusal);
    mvfxFieldPath root = mvfxFieldRoot(&refusal);
    mvfxFileResult result =
        WriteEffect(def, assets, assetCount, bytes, capacity, sizeOut, &refusal);
    // A refusal no check named is of no field: a NULL argument, the asset
    // table, the capacity.
    (void)mvfxRefuseHere(&root, result.result);
    refusalOut != nullptr ? (void)(*refusalOut = refusal) : (void)0;
    return result;
}

mvfxFileResult mvfxWriteEffect(const mvfxEffectDef* def, const mvfxEffectAsset* assets,
                               int32_t assetCount, void* bytesOut, int64_t capacity,
                               int64_t* sizeOut, mvfxEffectRefusal* refusalOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxFileResult result =
        mvfxWriteEffectWork(def, assets, assetCount, bytesOut, capacity, sizeOut, refusalOut);
    mvfxFloatEnvLeave(env);
    return result;
}

// Checks the header and the hash.
static mvfxFileResult ReadHeader(const uint8_t* bytes, size_t size)
{
    if (size < 4 || Get32(bytes, 0) != MAGIC)
    {
        return Fault(mvfx_errorFormat, 0);
    }
    if (size < 8 || Get32(bytes, 4) != MVFX_FILE_VERSION)
    {
        return Fault(size < 8 ? mvfx_errorFormat : mvfx_errorVersion, 4);
    }
    if (size < HEADER_BYTES || size > INT32_MAX || size % MVFX_FILE_ALIGNMENT != 0 ||
        Get32(bytes, 8) != size)
    {
        return Fault(mvfx_errorFormat, 8);
    }
    uint32_t count = Get32(bytes, 12);
    if (count < 1 || count > (size - HEADER_BYTES) / ENTRY_BYTES)
    {
        return Fault(mvfx_errorFormat, 12);
    }
    uint64_t hash = Hash(bytes, size);
    if (Get32(bytes, 16) != (uint32_t)hash || Get32(bytes, 20) != (uint32_t)(hash >> 32))
    {
        return Fault(mvfx_errorFormat, 16);
    }
    return Fault(mvfx_success, -1);
}

// The offset of the first byte from at to end that is not 0, or -1.
static int64_t NonZero(const uint8_t* bytes, size_t at, size_t end)
{
    for (; at < end; ++at)
    {
        if (bytes[at] != 0)
        {
            return (int64_t)at;
        }
    }
    return -1;
}

// Checks the section table: tags ascending and known, each section where
// the last ends after its padding, within the file, the bytes between
// them 0, the last ending with the file and the effect present.
static mvfxFileResult ReadTable(const uint8_t* bytes, size_t size, Section sections[SECTION_COUNT])
{
    uint32_t count = Get32(bytes, 12);
    size_t at = HEADER_BYTES + (size_t)count * ENTRY_BYTES;
    uint32_t last = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        int64_t padding = NonZero(bytes, at, Padded(at));
        if (padding >= 0)
        {
            return Fault(mvfx_errorFormat, padding);
        }
        at = Padded(at);
        size_t entry = HEADER_BYTES + (size_t)i * ENTRY_BYTES;
        uint32_t tag = Get32(bytes, entry);
        if (tag <= last)
        {
            return Fault(mvfx_errorFormat, (int64_t)entry);
        }
        // Only a later version writes a section this one does not know.
        if (tag > SECTION_COUNT)
        {
            return Fault(mvfx_errorVersion, (int64_t)entry);
        }
        if (Get32(bytes, entry + 4) != at)
        {
            return Fault(mvfx_errorFormat, (int64_t)entry + 4);
        }
        size_t length = Get32(bytes, entry + 8);
        if (length > size - at)
        {
            return Fault(mvfx_errorFormat, (int64_t)entry + 8);
        }
        sections[tag - 1] = (Section){at, length, 0};
        at += length;
        last = tag;
    }
    int64_t padding = NonZero(bytes, at, Padded(at));
    if (padding >= 0)
    {
        return Fault(mvfx_errorFormat, padding);
    }
    if (Padded(at) != size)
    {
        return Fault(mvfx_errorFormat, (int64_t)Padded(at));
    }
    if (last != EFFECT_TAG)
    {
        return Fault(mvfx_errorFormat, 12);
    }
    return Fault(mvfx_success, -1);
}

// The length of the name at at in a section ending at end, or -1 when it
// does not fit there. Each walk after a section's check reads lengths
// through it, so none leaves the section even if the bytes changed
// meanwhile.
static int64_t NameLength(const uint8_t* bytes, size_t at, size_t end)
{
    if (end - at < 4)
    {
        return -1;
    }
    uint32_t length = Get32(bytes, at);
    return length <= end - at - 4 ? (int64_t)length : -1;
}

// Where a names section's first name is: after its count, if it is there.
static size_t FirstName(const Section* section)
{
    return section->offset + (section->size > 0 ? 4 : 0);
}

// Checks a names section alone, absent for none: its count within its
// cap, each name's length, its bytes UTF-8 and in the section, none twice,
// the section ending with the last.
static mvfxFileResult ReadNames(const uint8_t* bytes, Section* section, int32_t cap)
{
    size_t end = section->offset + section->size;
    if (section->size > 0 && (section->size < 4 || Get32(bytes, section->offset) > (uint32_t)cap))
    {
        return Fault(mvfx_errorFormat, (int64_t)section->offset);
    }
    section->count = section->size > 0 ? Get32(bytes, section->offset) : 0;
    size_t at = FirstName(section);
    for (uint32_t i = 0; i < section->count; ++i)
    {
        int64_t length = NameLength(bytes, at, end);
        if (length < 1 || length > MVFX_MAX_ASSET_NAME)
        {
            return Fault(mvfx_errorFormat, (int64_t)at);
        }
        size_t bad = Utf8Fault(bytes + at + 4, (size_t)length);
        if (bad != (size_t)length)
        {
            return Fault(mvfx_errorFormat, (int64_t)(at + 4 + bad));
        }
        for (size_t before = FirstName(section); before < at;)
        {
            int64_t earlier = NameLength(bytes, before, end);
            if (earlier < 0 || SameName((const char*)bytes + before + 4, (int32_t)earlier,
                                        (const char*)bytes + at + 4, (int32_t)length))
            {
                return Fault(mvfx_errorFormat, (int64_t)at);
            }
            before += 4 + (size_t)earlier;
        }
        at += 4 + (size_t)length;
    }
    return Fault(at == end ? mvfx_success : mvfx_errorFormat, at == end ? -1 : (int64_t)at);
}

// Binds each name of each names section to the asset of its kind named
// so.
static mvfxFileResult Bind(const uint8_t* bytes, const Section sections[SECTION_COUNT],
                           const mvfxEffectAsset* assets, int32_t assetCount, int32_t* names)
{
    for (mvfxAssetKind kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        size_t at = FirstName(&sections[kind]);
        size_t end = sections[kind].offset + sections[kind].size;
        for (uint32_t i = 0; i < sections[kind].count; ++i)
        {
            int64_t length = NameLength(bytes, at, end);
            if (length < 0)
            {
                return Fault(mvfx_errorFormat, (int64_t)at);
            }
            int32_t found = -1;
            for (int32_t a = 0; a < assetCount && found < 0; ++a)
            {
                found =
                    assets[a].kind == kind && SameName(assets[a].name, assets[a].nameLength,
                                                       (const char*)bytes + at + 4, (int32_t)length)
                        ? a
                        : -1;
            }
            if (found < 0)
            {
                return Fault(mvfx_errorMissing, (int64_t)at);
            }
            *names++ = found;
            at += 4 + (size_t)length;
        }
    }
    return Fault(mvfx_success, -1);
}

// Walks the effect section: checking and measuring without a def, reading
// into one from its default with names bound, its arrays in memory of
// capacity bytes; a fault in a field recorded in refusal with its path.
static mvfxFileResult ReadDef(const uint8_t* bytes, const Section sections[SECTION_COUNT],
                              const mvfxEffectAsset* assets, int32_t* names, uint8_t* memory,
                              size_t capacity, mvfxEffectDef* def, size_t* used,
                              mvfxEffectRefusal* refusal)
{
    const Section* effect = &sections[EFFECT_TAG - 1];
    mvfxCodec codec = {.in = bytes,
                       .at = effect->offset,
                       .end = effect->offset + effect->size,
                       .defaults = mvfxFileDefaults,
                       .memory = memory,
                       .capacity = capacity,
                       .faultAt = -1,
                       .path = mvfxFieldRoot(refusal),
                       .assets = assets};
    size_t first = 0;
    for (int32_t kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        codec.names[kind] = names != nullptr ? names + first : nullptr;
        codec.nameCap[kind] = (int32_t)sections[kind].count;
        first += sections[kind].count;
    }
    if (def != nullptr)
    {
        memcpy(def, mvfxFileDefaults + mvfxFileEffectDef->defaultAt, sizeof *def);
    }
    mvfxReadFields(&codec, mvfxFileEffectDef, (uint8_t*)def, codec.end);
    *used = codec.used;
    if (codec.fault != mvfx_success)
    {
        return Fault(codec.fault, codec.faultAt);
    }
    if (codec.at != codec.end)
    {
        return Fault(mvfx_errorFormat, (int64_t)codec.at);
    }
    // Every name is referred to: the first one never is lies at fault.
    for (int32_t kind = 0; kind < MVFX_ASSET_KINDS; ++kind)
    {
        size_t at = FirstName(&sections[kind]);
        size_t end = sections[kind].offset + sections[kind].size;
        for (int32_t i = 0; i < codec.nameCount[kind] && NameLength(bytes, at, end) >= 0; ++i)
        {
            at += 4 + (size_t)NameLength(bytes, at, end);
        }
        if (codec.nameCount[kind] != codec.nameCap[kind])
        {
            return Fault(mvfx_errorFormat, (int64_t)at);
        }
    }
    return Fault(mvfx_success, -1);
}

// Checks a file up to the effect section's walk, measuring its memory and
// binding its names; a fault in a field recorded in refusal.
static mvfxFileResult CheckFile(const uint8_t* bytes, size_t size, const mvfxEffectAsset* assets,
                                int32_t assetCount, Section sections[SECTION_COUNT], int32_t* names,
                                size_t* needed, mvfxEffectRefusal* refusal)
{
    mvfxFileResult result = ReadHeader(bytes, size);
    if (result.result == mvfx_success)
    {
        result = ReadTable(bytes, size, sections);
    }
    for (int32_t kind = 0; kind < MVFX_ASSET_KINDS && result.result == mvfx_success; ++kind)
    {
        result = ReadNames(bytes, &sections[kind], s_nameCaps[kind]);
    }
    if (result.result == mvfx_success)
    {
        result = ReadDef(bytes, sections, assets, nullptr, nullptr, 0, nullptr, needed, refusal);
    }
    if (result.result == mvfx_success)
    {
        result = Bind(bytes, sections, assets, assetCount, names);
    }
    return result;
}

// The effect section of a file the reader accepted, from its section
// table; false when the bytes no longer hold one in the file.
static bool EffectSection(const void* file, int64_t size, Section* effect)
{
    const uint8_t* bytes = file;
    if (size < HEADER_BYTES)
    {
        return false;
    }
    uint32_t count = Get32(bytes, 12);
    if (count < 1 || count > ((size_t)size - HEADER_BYTES) / ENTRY_BYTES)
    {
        return false;
    }
    size_t entry = HEADER_BYTES + (size_t)(count - 1) * ENTRY_BYTES;
    size_t at = Get32(bytes, entry + 4);
    size_t length = Get32(bytes, entry + 8);
    if (Get32(bytes, entry) != EFFECT_TAG || at > (size_t)size || length > (size_t)size - at)
    {
        return false;
    }
    *effect = (Section){at, length, 0};
    return true;
}

// The field a head at at, in a sequence of fields ending at end, of a tag;
// SIZE_MAX when the sequence lacks it or does not fit.
static size_t FindField(const uint8_t* bytes, size_t at, size_t end, uint32_t tag)
{
    while (at <= end && end - at >= 8)
    {
        uint32_t found = Get32(bytes, at);
        size_t length = Get32(bytes, at + 4);
        if (found == tag)
        {
            return at;
        }
        if (found > tag || length > end - at - 8)
        {
            return SIZE_MAX;
        }
        at = Padded(at + 8 + length);
    }
    return SIZE_MAX;
}

// The offset in the effect section of the tag of the field a refusal
// names, or, for a field the file leaves out, of the head of the nearest
// struct or item holding it that the file has (the section's own when it
// has none, as for a refusal of the def as a whole). The walk keeps
// within the section and each length it reads, so bytes that changed
// since the read find some offset there.
static int64_t FieldOffset(const uint8_t* bytes, const Section* effect,
                           const mvfxEffectRefusal* refusal)
{
    size_t at = effect->offset;
    size_t end = effect->offset + effect->size;
    size_t found = effect->offset;
    for (int32_t s = 0; s < refusal->tagCount; ++s)
    {
        size_t head = FindField(bytes, at, end, refusal->tags[s]);
        if (head == SIZE_MAX)
        {
            break;
        }
        found = head;
        at = head + 8;
        end = at + Get32(bytes, head + 4);
        int32_t item = refusal->items[s];
        if (s + 1 == refusal->tagCount || item < 0)
        {
            continue;
        }
        // An item: past the list's count and zero word, each item its
        // length, a zero word and its fields.
        if (end - at < 8 || (uint32_t)item >= Get32(bytes, at))
        {
            break;
        }
        at += 8;
        size_t itemAt = SIZE_MAX;
        for (int32_t i = 0; itemAt == SIZE_MAX && end - at >= 8; ++i)
        {
            size_t length = Get32(bytes, at);
            if (length > end - at - 8)
            {
                break;
            }
            itemAt = i == item ? at : SIZE_MAX;
            at += i == item ? 0 : 8 + length;
        }
        if (itemAt == SIZE_MAX)
        {
            break;
        }
        found = itemAt;
        end = itemAt + 8 + Get32(bytes, itemAt);
        at = itemAt + 8;
    }
    return (int64_t)found;
}

static mvfxFileResult ReadEffect(const void* bytes, int64_t size, const mvfxEffectAsset* assets,
                                 int32_t assetCount, void* memory, int64_t capacity,
                                 int64_t* neededOut, mvfxEffectDef* defOut,
                                 mvfxEffectRefusal* refusal)
{
    if (neededOut == nullptr || defOut == nullptr || size < 0 || (bytes == nullptr && size > 0) ||
        capacity < 0 || (memory == nullptr && capacity > 0) || (uintptr_t)memory % 16 != 0)
    {
        return Fault(mvfx_errorInvalid, -1);
    }
    // The def is laid in memory while the bytes are read: memory over the
    // bytes would change them under the read.
    uintptr_t from = (uintptr_t)bytes;
    uintptr_t into = (uintptr_t)memory;
    if (size > 0 && capacity > 0 && into < from + (uintptr_t)size &&
        from < into + (uintptr_t)capacity)
    {
        return Fault(mvfx_errorInvalid, -1);
    }
    mvfxResult assetsValid = CheckAssets(assets, assetCount);
    if (assetsValid != mvfx_success)
    {
        return Fault(assetsValid, -1);
    }
    Section sections[SECTION_COUNT] = {0};
    int32_t names[MVFX_FILE_MAX_GRIDS + MVFX_FILE_MAX_MESHES + MVFX_FILE_MAX_MODULES];
    size_t needed = 0;
    mvfxFileResult result =
        CheckFile(bytes, (size_t)size, assets, assetCount, sections, names, &needed, refusal);
    if (result.result != mvfx_success)
    {
        return result;
    }
    *neededOut = (int64_t)needed;
    if ((int64_t)needed > capacity)
    {
        return Fault(mvfx_errorCapacity, -1);
    }
    result =
        ReadDef(bytes, sections, assets, names, memory, (size_t)capacity, defOut, &needed, refusal);
    if (result.result != mvfx_success)
    {
        return result;
    }
    // A def the file holds is the file's bytes: whatever check it fails,
    // a bound's included, the file is malformed, at the field refused.
    mvfxFieldPath root = mvfxFieldRoot(refusal);
    mvfxResult valid = mvfxCheckEffectDef(&root, defOut);
    valid = valid != mvfx_success ? valid : mvfxCheckEffectParts(&root, defOut);
    if (valid != mvfx_success)
    {
        refusal->result = mvfx_errorFormat;
        return Fault(mvfx_errorFormat, FieldOffset(bytes, &sections[EFFECT_TAG - 1], refusal));
    }
    return result;
}

static MVFX_NOINLINE mvfxFileResult mvfxReadEffectWork(const void* bytes, int64_t size,
                                                       const mvfxEffectAsset* assets,
                                                       int32_t assetCount, void* memory,
                                                       int64_t capacity, int64_t* neededOut,
                                                       mvfxEffectDef* defOut,
                                                       mvfxEffectRefusal* refusalOut)
{
    if (neededOut != nullptr)
    {
        *neededOut = 0;
    }
    if (defOut != nullptr)
    {
        *defOut = (mvfxEffectDef){0};
    }
    mvfxEffectRefusal refusal;
    mvfxClearRefusal(&refusal);
    mvfxFieldPath root = mvfxFieldRoot(&refusal);
    mvfxFileResult result =
        ReadEffect(bytes, size, assets, assetCount, memory, capacity, neededOut, defOut, &refusal);
    // A refusal no check named is of no field: a NULL argument, the asset
    // table, the header, the section table, a names section, the memory.
    (void)mvfxRefuseHere(&root, result.result);
    if (result.result != mvfx_success && defOut != nullptr)
    {
        *defOut = (mvfxEffectDef){0};
    }
    refusalOut != nullptr ? (void)(*refusalOut = refusal) : (void)0;
    return result;
}

mvfxFileResult mvfxReadEffect(const void* bytes, int64_t size, const mvfxEffectAsset* assets,
                              int32_t assetCount, void* memoryOut, int64_t capacity,
                              int64_t* neededOut, mvfxEffectDef* defOut,
                              mvfxEffectRefusal* refusalOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxFileResult result = mvfxReadEffectWork(bytes, size, assets, assetCount, memoryOut, capacity,
                                               neededOut, defOut, refusalOut);
    mvfxFloatEnvLeave(env);
    return result;
}

// Reads a file into memory from the context's allocator and creates its
// effect, the refusal recorded in refusal.
static mvfxFileResult CreateFromBytes(mvfxContext* context, const void* bytes, int64_t size,
                                      const mvfxEffectAsset* assets, int32_t assetCount,
                                      mvfxEffectId* effectOut, mvfxEffectRefusal* refusal)
{
    // The bytes' pointer and size and the asset table are the program's own
    // arguments: a correct program never passes them invalid, so that
    // counts as misuse. What the bytes hold is untrusted data and counts
    // nothing.
    mvfxResult valid = size < 0 || (bytes == nullptr && size > 0) ? mvfx_errorInvalid
                                                                  : CheckAssets(assets, assetCount);
    if (valid != mvfx_success)
    {
        mvfxCountMisuse(context);
        return Fault(valid, -1);
    }
    mvfxEffectDef def;
    int64_t needed = 0;
    mvfxFileResult result =
        mvfxReadEffect(bytes, size, assets, assetCount, nullptr, 0, &needed, &def, refusal);
    void* memory = nullptr;
    if (result.result == mvfx_errorCapacity)
    {
        memory = mvfxAllocate(&context->allocator, (size_t)needed, 16);
        result = memory == nullptr ? Fault(mvfx_errorCapacity, -1)
                                   : mvfxReadEffect(bytes, size, assets, assetCount, memory, needed,
                                                    &needed, &def, refusal);
    }
    if (result.result == mvfx_success)
    {
        // A file's def is untrusted data: what it asks of the program's
        // assets is refused with its status, and counts no misuse; the
        // field refused is found in the file as the reader finds it.
        bool misuse = false;
        mvfxResult created = mvfxCreateEffectOf(context, &def, effectOut, refusal, &misuse);
        Section effect = {0};
        bool field = created != mvfx_success && refusal->tagCount > 0;
        int64_t at = field && EffectSection(bytes, size, &effect)
                         ? FieldOffset(bytes, &effect, refusal)
                         : -1;
        result = Fault(created, at);
    }
    mvfxRelease(&context->allocator, memory, (size_t)needed, 16);
    return result;
}

static MVFX_NOINLINE mvfxFileResult mvfxCreateEffectFromBytesWork(
    mvfxContext* context, const void* bytes, int64_t size, const mvfxEffectAsset* assets,
    int32_t assetCount, mvfxEffectId* effectOut, mvfxEffectRefusal* refusalOut)
{
    if (effectOut != nullptr)
    {
        *effectOut = (mvfxEffectId){0};
    }
    mvfxEffectRefusal refusal;
    mvfxClearRefusal(&refusal);
    if (context == nullptr || effectOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        refusal.result = mvfx_errorInvalid;
        refusalOut != nullptr ? (void)(*refusalOut = refusal) : (void)0;
        return Fault(mvfx_errorInvalid, -1);
    }
    // Busy, it writes no output but the id.
    if (mvfxRefusesHostCall(context))
    {
        return Fault(mvfx_errorBusy, -1);
    }
    mvfxFileResult result =
        CreateFromBytes(context, bytes, size, assets, assetCount, effectOut, &refusal);
    mvfxFieldPath root = mvfxFieldRoot(&refusal);
    (void)mvfxRefuseHere(&root, result.result);
    refusalOut != nullptr ? (void)(*refusalOut = refusal) : (void)0;
    return result;
}

mvfxFileResult mvfxCreateEffectFromBytes(mvfxContext* context, const void* bytes, int64_t size,
                                         const mvfxEffectAsset* assets, int32_t assetCount,
                                         mvfxEffectId* effectOut, mvfxEffectRefusal* refusalOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxFileResult result = mvfxCreateEffectFromBytesWork(context, bytes, size, assets, assetCount,
                                                          effectOut, refusalOut);
    mvfxFloatEnvLeave(env);
    return result;
}
