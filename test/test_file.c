// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect files: the refusals of the calls' arguments and of asset tables;
// a file's header, section table and names as written, names in the order
// the def first refers to them; objects the table lacks; fields left out
// that hold their defaults, at each level, and read back as them; every
// refusal of a reader at the offset of its byte, each field found by
// walking the file's sections, fields and items: unknown tags (a later
// version's) refused, not misread; every file cut short refused; the
// memory a def needs; and effects created from bytes. The golden test
// runs the whole scene through files, and test_file_padding writes each
// of its effects whatever their structs' padding holds.

#include "file_codec.h"
#include "file_fields.h"
#include "test_harness.h"

#include "maul-vfx/file.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/mesh.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void Kick(const mvfxModuleCall* call, void* user)
{
    (void)call;
    (void)user;
}

static int s_kickUser;

// The table: grids "ground" {1, 1} and "ball" {2, 1}, a mesh "hull"
// {1, 1}, module "kick".
static const mvfxEffectAsset s_assets[4] = {
    {.kind = mvfx_assetGrid, .name = "ground", .nameLength = 6, .grid = {1, 1}},
    {.kind = mvfx_assetGrid, .name = "ball", .nameLength = 4, .grid = {2, 1}},
    {.kind = mvfx_assetMesh, .name = "hull", .nameLength = 4, .mesh = {1, 1}},
    {.kind = mvfx_assetModule,
     .name = "kick",
     .nameLength = 4,
     .function = Kick,
     .user = &s_kickUser},
};

// A def the tests vary: an emitter of 16 particles with heightfield
// colliders on ball then ground and a host module.
typedef struct Def
{
    mvfxEffectDef effect;
    mvfxEmitterDef emitter;
    mvfxCollider colliders[3];
    mvfxHostModule module;
} Def;

static void MakeDef(Def* def)
{
    def->emitter = mvfxDefaultEmitterDef();
    def->emitter.capacity = 16;
    for (int32_t i = 0; i < 3; ++i)
    {
        def->colliders[i] = (mvfxCollider){.kind = mvfx_colliderHeightfield,
                                           .halfExtents = {2, 0, 2},
                                           .rotation = {0, 0, 0, 1},
                                           .grid = {2 - (uint32_t)(i % 2), 1}};
    }
    def->emitter.colliders = def->colliders;
    def->emitter.colliderCount = 2;
    def->module = (mvfxHostModule){.function = Kick, .user = &s_kickUser};
    def->emitter.hostModules = &def->module;
    def->emitter.hostModuleCount = 1;
    def->effect = mvfxDefaultEffectDef();
    def->effect.emitters = &def->emitter;
    def->effect.emitterCount = 1;
}

static uint32_t Get32(const uint8_t* bytes, int64_t at)
{
    return (uint32_t)bytes[at] | (uint32_t)bytes[at + 1] << 8 | (uint32_t)bytes[at + 2] << 16 |
           (uint32_t)bytes[at + 3] << 24;
}

static void Set32(uint8_t* bytes, int64_t at, uint32_t value)
{
    for (int32_t i = 0; i < 4; ++i)
    {
        bytes[at + i] = (uint8_t)(value >> (8 * i));
    }
}

// The hash set again after a change, so the change is what is read.
static void Rehash(uint8_t* bytes, int64_t size)
{
    uint64_t hash = mvfxHash64(MVFX_HASH_INIT, bytes + 24, size - 24);
    Set32(bytes, 16, (uint32_t)hash);
    Set32(bytes, 20, (uint32_t)(hash >> 32));
}

// A file of a def with the table, in bytes of 4096; its size, 0 when it
// is not written.
static int64_t Write(const Def* def, uint8_t* bytes)
{
    int64_t size = 0;
    mvfxFileResult result = mvfxWriteEffect(&def->effect, s_assets, 4, bytes, 4096, &size, NULL);
    bool written = result.result == mvfx_success && result.offset == -1;
    CHECK(written, "written");
    return written ? size : 0;
}

// Memory for read defs, aligned to 16.
static _Alignas(16) uint8_t s_memory[1 << 16];

static mvfxFileResult Read(const uint8_t* bytes, int64_t size, mvfxEffectDef* def)
{
    int64_t needed = 0;
    return mvfxReadEffect(bytes, size, s_assets, 4, s_memory, sizeof s_memory, &needed, def, NULL);
}

static bool Refused(mvfxFileResult result, mvfxResult status, int64_t offset)
{
    return result.result == status && result.offset == offset;
}

// Whether a refusal names the field of a path, as text.
static bool IsPath(const mvfxEffectRefusal* refusal, const char* path)
{
    char text[256];
    int64_t length = 0;
    return mvfxGetEffectFieldPath(refusal, text, sizeof text, &length) == mvfx_success &&
           length == (int64_t)strlen(path) && memcmp(text, path, (size_t)length) == 0;
}

// Whether a def reads from bytes and writes them again.
static bool SameBytesAgain(const uint8_t* bytes, int64_t size, mvfxEffectDef* read)
{
    static uint8_t again[4096];
    int64_t rewritten = 0;
    return Refused(Read(bytes, size, read), mvfx_success, -1) &&
           mvfxWriteEffect(read, s_assets, 4, again, sizeof again, &rewritten, NULL).result ==
               mvfx_success &&
           rewritten == size && memcmp(bytes, again, (size_t)size) == 0;
}

// The field of a table at an offset of its struct, its cookie aside.
static const mvfxFileField* FieldOf(const mvfxFileType* type, size_t offset)
{
    for (int32_t i = 0; i < type->fieldCount; ++i)
    {
        if (type->fields[i].offset == offset && type->fields[i].kind != mvfx_fieldCookie)
        {
            return &type->fields[i];
        }
    }
    CHECK(false, "a field of the table");
    return &type->fields[0];
}

static uint32_t TagOf(const mvfxFileType* type, size_t offset)
{
    return FieldOf(type, offset)->tag;
}

static const mvfxFileType* TypeOf(const mvfxFileType* type, size_t offset)
{
    return FieldOf(type, offset)->type;
}

// The tag past a table's last, which only a later version would write.
static uint32_t LaterTag(const mvfxFileType* type)
{
    return type->fields[type->fieldCount - 1].tag + 1;
}

static const mvfxFileType* EmitterType(void)
{
    return TypeOf(mvfxFileEffectDef, offsetof(mvfxEffectDef, emitters));
}

static const mvfxFileType* ColliderType(void)
{
    return TypeOf(EmitterType(), offsetof(mvfxEmitterDef, colliders));
}

// A sequence of fields in a file: from at to end.
typedef struct Span
{
    int64_t at;
    int64_t end;
} Span;

// The section of a tag, an empty span at -1 when the file has none.
static Span SectionOf(const uint8_t* bytes, uint32_t tag)
{
    for (uint32_t i = 0; i < Get32(bytes, 12); ++i)
    {
        int64_t entry = 24 + 12 * (int64_t)i;
        if (Get32(bytes, entry) == tag)
        {
            return (Span){Get32(bytes, entry + 4),
                          Get32(bytes, entry + 4) + Get32(bytes, entry + 8)};
        }
    }
    return (Span){-1, -1};
}

static Span EffectOf(const uint8_t* bytes)
{
    return SectionOf(bytes, 4);
}

static int64_t Padded(int64_t at)
{
    return (at + 7) & ~(int64_t)7;
}

// The head of the field of a tag in a sequence, -1 when it is left out.
static int64_t HeadOf(const uint8_t* bytes, Span span, uint32_t tag)
{
    for (int64_t at = span.at; at < span.end; at = Padded(at + 8 + Get32(bytes, at + 4)))
    {
        if (Get32(bytes, at) == tag)
        {
            return at;
        }
    }
    return -1;
}

// The head of the field of a tag, which the sequence holds.
static int64_t Head(const uint8_t* bytes, Span span, uint32_t tag)
{
    int64_t head = HeadOf(bytes, span, tag);
    CHECK(head >= 0, "the field written");
    return head;
}

// A struct field's payload, a sequence.
static Span Nested(const uint8_t* bytes, int64_t head)
{
    return (Span){head + 8, head + 8 + Get32(bytes, head + 4)};
}

// The head of item i of the list in the field at head.
static int64_t ItemHead(const uint8_t* bytes, int64_t head, int32_t i)
{
    int64_t at = head + 16;
    for (int32_t k = 0; k < i; ++k)
    {
        at += 8 + Get32(bytes, at);
    }
    return at;
}

// Item i of the list in the field at head, a sequence.
static Span Item(const uint8_t* bytes, int64_t head, int32_t i)
{
    int64_t at = ItemHead(bytes, head, i);
    return (Span){at + 8, at + 8 + Get32(bytes, at)};
}

// The fields of the first emitter of a file.
static Span Emitter(const uint8_t* bytes)
{
    return Item(
        bytes,
        Head(bytes, EffectOf(bytes), TagOf(mvfxFileEffectDef, offsetof(mvfxEffectDef, emitters))),
        0);
}

// The fields of collider i of the first emitter.
static Span Collider(const uint8_t* bytes, int32_t i)
{
    return Item(
        bytes,
        Head(bytes, Emitter(bytes), TagOf(EmitterType(), offsetof(mvfxEmitterDef, colliders))), i);
}

// The payload of the field at an offset of an emitter.
static int64_t EmitterPayload(const uint8_t* bytes, size_t offset)
{
    return Head(bytes, Emitter(bytes), TagOf(EmitterType(), offset)) + 8;
}

// The head of the list of an emitter's three size curves.
static int64_t SizeCurves(const uint8_t* bytes)
{
    const mvfxFileType* overLife = TypeOf(EmitterType(), offsetof(mvfxEmitterDef, overLife));
    const mvfxFileType* size = TypeOf(overLife, offsetof(mvfxOverLife, size));
    Span span = Nested(bytes, EmitterPayload(bytes, offsetof(mvfxEmitterDef, overLife)) - 8);
    span = Nested(bytes, Head(bytes, span, TagOf(overLife, offsetof(mvfxOverLife, size))));
    return Head(bytes, span, TagOf(size, offsetof(mvfxSizeOverLife, size)));
}

// The table of the size curves' items.
static const mvfxFileType* CurveType(void)
{
    const mvfxFileType* overLife = TypeOf(EmitterType(), offsetof(mvfxEmitterDef, overLife));
    const mvfxFileType* size = TypeOf(overLife, offsetof(mvfxOverLife, size));
    return TypeOf(size, offsetof(mvfxSizeOverLife, size));
}

static void TestArguments(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = 1;
    CHECK(Refused(mvfxWriteEffect(NULL, s_assets, 4, bytes, 4096, &size, NULL), mvfx_errorInvalid,
                  -1) &&
              size == 0,
          "a NULL def");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, NULL, NULL),
                  mvfx_errorInvalid, -1),
          "a NULL size");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, NULL, 4096, &size, NULL),
                  mvfx_errorInvalid, -1),
          "NULL bytes with a capacity");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, -1, &size, NULL),
                  mvfx_errorInvalid, -1),
          "a capacity below 0");
    CHECK(Refused(mvfxWriteEffect(&def.effect, NULL, 1, bytes, 4096, &size, NULL),
                  mvfx_errorInvalid, -1),
          "a NULL table with a count");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, -1, bytes, 4096, &size, NULL),
                  mvfx_errorInvalid, -1),
          "a count below 0");
    def.emitter.capacity = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size, NULL),
                  mvfx_errorInvalid, -1),
          "a def the validator refuses");
    MakeDef(&def);
    def.effect.cookie = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size, NULL),
                  mvfx_errorInvalid, -1),
          "a def without its cookie");
    MakeDef(&def);
    int64_t written = Write(&def, bytes);
    int64_t needed = 1;
    mvfxEffectDef read = {.cookie = 1};
    CHECK(Refused(mvfxReadEffect(NULL, 8, s_assets, 4, s_memory, 64, &needed, &read, NULL),
                  mvfx_errorInvalid, -1) &&
              needed == 0 && read.cookie == 0,
          "NULL bytes with a size");
    CHECK(Refused(mvfxReadEffect(bytes, -1, s_assets, 4, s_memory, 64, &needed, &read, NULL),
                  mvfx_errorInvalid, -1),
          "a size below 0");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, NULL, 64, &needed, &read, NULL),
                  mvfx_errorInvalid, -1),
          "NULL memory with a capacity");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory, -1, &needed, &read, NULL),
                  mvfx_errorInvalid, -1),
          "a capacity below 0");
    CHECK(
        Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory + 8, 64, &needed, &read, NULL),
                mvfx_errorInvalid, -1),
        "memory not aligned to 16");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory, 64, NULL, &read, NULL),
                  mvfx_errorInvalid, -1),
          "a NULL needed");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory, 64, &needed, NULL, NULL),
                  mvfx_errorInvalid, -1),
          "a NULL def");
}

// Each table the writer and the reader refuse.
static void TestAssetTables(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t written = Write(&def, bytes);
    // Overlong, a surrogate, past U+10FFFF, overlong in three bytes, a
    // lead past 0xF4, overlong in four bytes, cut short, a lone
    // continuation, a bad third byte.
    static const struct
    {
        uint8_t bytes[4];
        int32_t length;
    } bad[] = {{{0xC0, 0x80}, 2},
               {{0xED, 0xA0, 0x80}, 3},
               {{0xF4, 0x90, 0x80, 0x80}, 4},
               {{0xE0, 0x9F, 0xBF}, 3},
               {{0xF5, 0x80, 0x80, 0x80}, 4},
               {{0xF0, 0x8F, 0xBF, 0xBF}, 4},
               {{0xC3}, 1},
               {{0x80}, 1},
               {{0xE2, 0x82, 0x41}, 3}};
    char longName[MVFX_MAX_ASSET_NAME + 1];
    memset(longName, 'a', sizeof longName);
    for (int32_t c = 0; c < 5 + (int32_t)(sizeof bad / sizeof bad[0]); ++c)
    {
        mvfxEffectAsset table[4];
        memcpy(table, s_assets, sizeof table);
        switch (c)
        {
        case 0:
            table[1].kind = 3;
            break;
        case 1:
            table[1].name = NULL;
            break;
        case 2:
            table[1].nameLength = 0;
            break;
        case 3:
            table[1].name = longName;
            table[1].nameLength = MVFX_MAX_ASSET_NAME + 1;
            break;
        case 4:
            table[1].name = "ground";
            table[1].nameLength = 6;
            break;
        default:
            table[1].name = (const char*)bad[c - 5].bytes;
            table[1].nameLength = bad[c - 5].length;
            break;
        }
        int64_t size = 0;
        int64_t needed = 0;
        mvfxEffectDef read;
        // A name past MVFX_MAX_ASSET_NAME is past a named bound.
        mvfxResult want = c == 3 ? mvfx_errorLimit : mvfx_errorInvalid;
        CHECK(Refused(mvfxWriteEffect(&def.effect, table, 4, bytes, 4096, &size, NULL), want, -1),
              "the writer refuses the table");
        CHECK(Refused(mvfxReadEffect(bytes, written, table, 4, s_memory, sizeof s_memory, &needed,
                                     &read, NULL),
                      want, -1),
              "the reader refuses the table");
    }
    // A name the longest allowed, once a kind under each kind, in UTF-8 of
    // two, three and four bytes.
    mvfxEffectAsset table[7];
    memcpy(table, s_assets, sizeof s_assets);
    table[4] = (mvfxEffectAsset){.kind = mvfx_assetMesh, .name = "ground", .nameLength = 6};
    table[5] = (mvfxEffectAsset){
        .kind = mvfx_assetGrid, .name = longName, .nameLength = MVFX_MAX_ASSET_NAME};
    table[6] = (mvfxEffectAsset){
        .kind = mvfx_assetGrid, .name = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x8C\x8A", .nameLength = 9};
    int64_t size = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, table, 7, bytes, 4096, &size, NULL), mvfx_success,
                  -1) &&
              size == written,
          "names allowed");
}

// The header, the section table and the names as written: ball before
// ground, as the colliders first refer to them; no mesh section; the
// module once; each section at a multiple of 8.
static void TestLayout(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    // The bytes themselves, by a hash of one byte at a time (FNV-1a), the
    // same on every host: a host of another byte order is held to them.
    uint64_t fnv = 0xCBF29CE484222325ull;
    for (int64_t i = 0; i < size; ++i)
    {
        fnv = (fnv ^ bytes[i]) * 0x100000001B3ull;
    }
    CHECK(fnv == 0x6425235D3473237Bull && size == 376, "the bytes as committed");
    CHECK(memcmp(bytes, "MVFX", 4) == 0, "the magic");
    CHECK(Get32(bytes, 4) == MVFX_FILE_VERSION && Get32(bytes, 8) == (uint32_t)size &&
              Get32(bytes, 12) == 3 && size % 8 == 0,
          "the version, the size, the sections");
    // Grid names, 22 bytes from 64; module names, 12 from 88; the effect
    // from 104 to the end.
    static const uint32_t table[3][3] = {{1, 64, 22}, {3, 88, 12}, {4, 104, 0}};
    for (int32_t s = 0; s < 3; ++s)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            uint32_t expected = s == 2 && k == 2 ? (uint32_t)size - 104 : table[s][k];
            CHECK(Get32(bytes, 24 + 12 * s + 4 * k) == expected, "the section table");
        }
    }
    CHECK(Get32(bytes, 60) == 0 && bytes[86] == 0 && bytes[87] == 0 && Get32(bytes, 100) == 0,
          "zero bytes to each section");
    CHECK(Get32(bytes, 64) == 2 && Get32(bytes, 68) == 4 && memcmp(bytes + 72, "ball", 4) == 0 &&
              Get32(bytes, 76) == 6 && memcmp(bytes + 80, "ground", 6) == 0 &&
              Get32(bytes, 88) == 1 && Get32(bytes, 92) == 4 && memcmp(bytes + 96, "kick", 4) == 0,
          "the names in the order referred to");
    int64_t needed = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, NULL, 0, &needed, NULL),
                  mvfx_errorCapacity, -1) &&
              needed == size,
          "measured");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, size - 1, &needed, NULL),
                  mvfx_errorCapacity, -1) &&
              needed == size,
          "a byte short");
    // Read back: the ids bound, the module's user, the cookies.
    mvfxEffectDef read;
    CHECK(Refused(Read(bytes, size, &read), mvfx_success, -1), "read");
    CHECK(read.cookie == MVFX_EFFECT_DEF_COOKIE && read.emitterCount == 1 &&
              read.emitters[0].cookie == MVFX_EMITTER_DEF_COOKIE &&
              read.emitters[0].colliders[0].grid.index1 == 2 &&
              read.emitters[0].colliders[1].grid.index1 == 1 &&
              read.emitters[0].hostModules[0].function == Kick &&
              read.emitters[0].hostModules[0].user == &s_kickUser,
          "the def read");
    // Without colliders and modules, no names: the effect alone, at 40.
    def.emitter.colliderCount = 0;
    def.emitter.hostModuleCount = 0;
    size = Write(&def, bytes);
    CHECK(Get32(bytes, 12) == 1 && Get32(bytes, 24) == 4 && Get32(bytes, 28) == 40 &&
              Get32(bytes, 36) == 0,
          "no names");
}

// Fields that hold their defaults are left out, at each level, and read
// back as them: a def of one default emitter writes its emitters field
// alone, the emitter an empty item; with that field left out too, the def
// is the default, which the validator refuses (an effect has something to
// play); a nested struct holds only what differs; an item of a struct with
// no default function is read from zero bytes; an item of a fixed list
// that holds its default is empty.
static void TestDefaults(void)
{
    mvfxEffectDef effect = mvfxDefaultEffectDef();
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    effect.emitters = &emitter;
    effect.emitterCount = 1;
    uint8_t bytes[4096];
    int64_t size = 0;
    CHECK(mvfxWriteEffect(&effect, s_assets, 4, bytes, sizeof bytes, &size, NULL).result ==
              mvfx_success,
          "a default emitter written");
    Span span = EffectOf(bytes);
    int64_t head = Head(bytes, span, TagOf(mvfxFileEffectDef, offsetof(mvfxEffectDef, emitters)));
    CHECK(size == 64 && Get32(bytes, 12) == 1 && span.at == 40 && span.end - span.at == 24 &&
              Get32(bytes, head + 4) == 16 && Get32(bytes, head + 8) == 1 &&
              Get32(bytes, head + 16) == 0,
          "the emitters field alone, one empty item");
    mvfxEffectDef read;
    uint8_t empty[40];
    memcpy(empty, bytes, sizeof empty);
    Set32(empty, 8, 40);
    Set32(empty, 32, 0);
    Rehash(empty, 40);
    CHECK(Refused(Read(empty, 40, &read), mvfx_errorFormat, 40),
          "no effect field: the default def, refused by the validator");
    CHECK(SameBytesAgain(bytes, size, &read) &&
              read.emitters[0].cookie == MVFX_EMITTER_DEF_COOKIE &&
              read.emitters[0].capacity == 1024 && read.emitters[0].rate == 64.0f &&
              read.emitters[0].shape.rotation[3] == 1.0f && read.emitters[0].liquid.substeps == 4 &&
              read.emitters[0].ribbon.width.value == 1.0f,
          "the item read as the default emitter");

    // A shape that differs in its radius alone: the shape's one field.
    emitter.shape.radius = 2.0f;
    CHECK(mvfxWriteEffect(&effect, s_assets, 4, bytes, sizeof bytes, &size, NULL).result ==
              mvfx_success,
          "a radius written");
    const mvfxFileType* shape = TypeOf(EmitterType(), offsetof(mvfxEmitterDef, shape));
    Span item = Emitter(bytes);
    int64_t shapeHead = Head(bytes, item, TagOf(EmitterType(), offsetof(mvfxEmitterDef, shape)));
    Span fields = Nested(bytes, shapeHead);
    CHECK(item.end - item.at == 24 && fields.end - fields.at == 16 &&
              Get32(bytes, fields.at) == TagOf(shape, offsetof(mvfxShape, radius)),
          "the shape holds the radius alone");
    CHECK(SameBytesAgain(bytes, size, &read) && read.emitters[0].shape.radius == 2.0f &&
              read.emitters[0].shape.rotation[3] == 1.0f &&
              read.emitters[0].shape.arc == emitter.shape.arc &&
              read.emitters[0].shape.kind == mvfx_shapePoint,
          "the shape's other fields read as their defaults");

    // A sphere of a radius, and a plane (kind 0) of a normal: their other
    // fields from zero bytes.
    emitter.shape.radius = 0.0f;
    mvfxCollider colliders[2] = {{.kind = mvfx_colliderSphere, .radius = 1.0f},
                                 {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}}};
    emitter.colliders = colliders;
    emitter.colliderCount = 2;
    CHECK(mvfxWriteEffect(&effect, s_assets, 4, bytes, sizeof bytes, &size, NULL).result ==
              mvfx_success,
          "colliders written");
    CHECK(Collider(bytes, 0).end - Collider(bytes, 0).at == 32 &&
              Collider(bytes, 1).end - Collider(bytes, 1).at == 24,
          "a sphere's two fields, a plane's one");
    CHECK(SameBytesAgain(bytes, size, &read) && read.emitters[0].colliders[0].radius == 1.0f &&
              read.emitters[0].colliders[0].rotation[3] == 0.0f &&
              read.emitters[0].colliders[1].normal[1] == 1.0f &&
              read.emitters[0].colliders[1].radius == 0.0f &&
              read.emitters[0].colliders[1].halfExtents[0] == 0.0f,
          "the colliders' other fields read as zero");

    // A fixed list of three curves, the middle one's value alone set.
    emitter.colliderCount = 0;
    emitter.overLife.size.size[1].value = 2.0f;
    CHECK(mvfxWriteEffect(&effect, s_assets, 4, bytes, sizeof bytes, &size, NULL).result ==
              mvfx_success,
          "a size curve written");
    int64_t list = SizeCurves(bytes);
    CHECK(Get32(bytes, list + 8) == 3 && Get32(bytes, ItemHead(bytes, list, 0)) == 0 &&
              Get32(bytes, ItemHead(bytes, list, 1)) == 16 &&
              Get32(bytes, ItemHead(bytes, list, 2)) == 0,
          "the list's items, the middle one alone holding a field");
    CHECK(SameBytesAgain(bytes, size, &read) &&
              read.emitters[0].overLife.size.size[1].value == 2.0f &&
              read.emitters[0].overLife.size.size[0].value == 0.0f,
          "the list read");
}

// An object the table lacks: refused on writing; a name the table lacks
// refused on reading at the name.
static void TestMissing(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = 0;
    def.colliders[0].grid.generation = 2;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size, NULL),
                  mvfx_errorMissing, -1) &&
              size == 0,
          "a grid the table lacks");
    MakeDef(&def);
    def.module.user = NULL;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size, NULL),
                  mvfx_errorMissing, -1),
          "a module with another user");
    MakeDef(&def);
    size = Write(&def, bytes);
    int64_t needed = 0;
    mvfxEffectDef read;
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets, 3, s_memory, sizeof s_memory, &needed,
                                 &read, NULL),
                  mvfx_errorMissing, 92),
          "the module's name missing");
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets + 1, 3, s_memory, sizeof s_memory, &needed,
                                 &read, NULL),
                  mvfx_errorMissing, 76),
          "ground's name missing");
}

// Refused after a change to the bytes, the hash set again.
static void Expect(const uint8_t* file, int64_t size, int64_t at, uint32_t value, mvfxResult status,
                   int64_t offset, const char* what)
{
    uint8_t bytes[4096];
    memcpy(bytes, file, (size_t)size);
    Set32(bytes, at, value);
    Rehash(bytes, size);
    mvfxEffectDef read = {.cookie = 1};
    mvfxFileResult result = Read(bytes, size, &read);
    CHECK(Refused(result, status, offset) && read.cookie == 0, what);
}

static void TestHeaderAndTable(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    uint8_t changed[4096];
    memcpy(changed, bytes, (size_t)size);
    changed[1] = 'W';
    mvfxEffectDef read;
    CHECK(Refused(Read(changed, size, &read), mvfx_errorFormat, 0), "the magic");
    Expect(bytes, size, 4, 1, mvfx_errorVersion, 4, "an earlier version");
    Expect(bytes, size, 8, (uint32_t)size + 1, mvfx_errorFormat, 8, "the size");
    Expect(bytes, size, 12, 0, mvfx_errorFormat, 12, "no sections");
    Expect(bytes, size, 12, (uint32_t)(size - 24) / 12 + 1, mvfx_errorFormat, 12,
           "a table past the file");
    memcpy(changed, bytes, (size_t)size);
    changed[size - 1] ^= 1;
    CHECK(Refused(Read(changed, size, &read), mvfx_errorFormat, 16), "the hash");
    // A size not a multiple of 8, its bytes 0.
    memcpy(changed, bytes, (size_t)size);
    Set32(changed, size, 0);
    Set32(changed, 8, (uint32_t)size + 4);
    Rehash(changed, size + 4);
    CHECK(Refused(Read(changed, size + 4, &read), mvfx_errorFormat, 8), "a size off the alignment");
    Expect(bytes, size, 24, 0, mvfx_errorFormat, 24, "a tag of 0");
    Expect(bytes, size, 36, 1, mvfx_errorFormat, 36, "a tag twice");
    Expect(bytes, size, 36, 5, mvfx_errorVersion, 36, "a section of a later version");
    Expect(bytes, size, 48, 5, mvfx_errorVersion, 48, "the last of a later version");
    Expect(bytes, size, 40, 92, mvfx_errorFormat, 40, "a section not where the last ends");
    Expect(bytes, size, 28, 60, mvfx_errorFormat, 28, "a section on the table's padding");
    Expect(bytes, size, 56, (uint32_t)size, mvfx_errorFormat, 56, "a section past the file");
    Expect(bytes, size, 32, 30, mvfx_errorFormat, 40, "a section running into the next");
    Expect(bytes, size, 60, 1, mvfx_errorFormat, 60, "a byte of the table's padding");
    Expect(bytes, size, 84, 0x01000000u | Get32(bytes, 84), mvfx_errorFormat, 87,
           "a byte of a section's padding");
    // No effect: its section named the meshes', in a file of grid names.
    MakeDef(&def);
    def.emitter.hostModuleCount = 0;
    size = Write(&def, bytes);
    CHECK(Get32(bytes, 12) == 2 && Get32(bytes, 36) == 4, "grid names and the effect");
    Expect(bytes, size, 36, 2, mvfx_errorFormat, 12, "no effect");
    // Every byte cut off.
    for (int64_t cut = 0; cut < size; ++cut)
    {
        mvfxFileResult result = Read(bytes, cut, &read);
        CHECK(result.result == mvfx_errorFormat && result.offset == (cut < 4   ? 0
                                                                     : cut < 8 ? 4
                                                                               : 8),
              "a file cut short");
    }
}

static void TestNames(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    Expect(bytes, size, 68, 0, mvfx_errorFormat, 68, "an empty name");
    Expect(bytes, size, 68, MVFX_MAX_ASSET_NAME + 1, mvfx_errorFormat, 68, "a name too long");
    Expect(bytes, size, 76, 7, mvfx_errorFormat, 76, "a name past its section");
    // "ball" as "bal\xFF", "ground" as "ball\0\0" with the length 4 and two
    // bytes over.
    Expect(bytes, size, 72, 0xFF6C6162u, mvfx_errorFormat, 75, "a name not UTF-8");
    uint8_t changed[4096];
    memcpy(changed, bytes, (size_t)size);
    Set32(changed, 76, 4);
    memcpy(changed + 80, "ball", 4);
    Rehash(changed, size);
    mvfxEffectDef read;
    CHECK(Refused(Read(changed, size, &read), mvfx_errorFormat, 76), "a name twice");
    // One name counted: the section's bytes left over.
    Expect(bytes, size, 64, 1, mvfx_errorFormat, 76, "bytes left in a section");
    // Three counted: the next name's length runs out of the section.
    Expect(bytes, size, 64, 3, mvfx_errorFormat, 86, "a name past its section's end");
    Expect(bytes, size, 64, MVFX_FILE_MAX_GRIDS + 1, mvfx_errorFormat, 64,
           "more grid names than a def refers to");
    Expect(bytes, size, 88, MVFX_FILE_MAX_MODULES + 1, mvfx_errorFormat, 88, "more module names");
    // The grid names' section taking its padding: two bytes after the names.
    Expect(bytes, size, 32, 24, mvfx_errorFormat, 86, "a section's bytes left over");
    // A grid names section of 2 bytes, too short for its count, before an
    // effect of 24.
    uint8_t shortNames[80] = {0};
    memcpy(shortNames, "MVFX", 4);
    Set32(shortNames, 4, MVFX_FILE_VERSION);
    Set32(shortNames, 8, 80);
    Set32(shortNames, 12, 2);
    static const uint32_t entries[6] = {1, 48, 2, 4, 56, 24};
    for (int32_t k = 0; k < 6; ++k)
    {
        Set32(shortNames, 24 + 4 * k, entries[k]);
    }
    Rehash(shortNames, 80);
    CHECK(Refused(Read(shortNames, 80, &read), mvfx_errorFormat, 48),
          "a names section too short for its count");
}

// A field's or an item's bytes changed: refused at the byte at fault.
static void TestFields(void)
{
    Def def;
    MakeDef(&def);
    def.emitter.fixedBounds = true;
    def.emitter.shape.radius = 2.0f;
    def.emitter.overLife.size.size[1].value = 2.0f;
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    mvfxEffectDef read;
    CHECK(SameBytesAgain(bytes, size, &read), "the def read");
    Span effect = EffectOf(bytes);
    int64_t emitters =
        Head(bytes, effect, TagOf(mvfxFileEffectDef, offsetof(mvfxEffectDef, emitters)));
    int64_t listEnd = emitters + 8 + Get32(bytes, emitters + 4);
    Expect(bytes, size, emitters + 8, (uint32_t)-1, mvfx_errorFormat, emitters + 8,
           "a count below 0");
    Expect(bytes, size, emitters + 8, MVFX_MAX_EMITTERS + 1, mvfx_errorFormat, emitters + 8,
           "a count past its cap");
    Expect(bytes, size, emitters + 8, 2, mvfx_errorFormat, listEnd,
           "a second emitter past the list");
    Expect(bytes, size, emitters + 12, 1, mvfx_errorFormat, emitters + 12, "a list's zero word");
    int64_t item = ItemHead(bytes, emitters, 0);
    Expect(bytes, size, item + 4, 1, mvfx_errorFormat, item + 4, "an item's zero word");
    Expect(bytes, size, item, 4096, mvfx_errorFormat, item, "an item past its list");
    Expect(bytes, size, emitters + 4, 4096, mvfx_errorFormat, emitters + 4,
           "a field past its struct");
    // An emitter's fields in tag order: capacity is its first.
    Span fields = Emitter(bytes);
    int64_t capacity =
        Head(bytes, fields, TagOf(EmitterType(), offsetof(mvfxEmitterDef, capacity)));
    CHECK(capacity == fields.at, "the capacity first");
    Expect(bytes, size, capacity + 4, 3, mvfx_errorFormat, capacity + 4, "a scalar of 3 bytes");
    Expect(bytes, size, capacity + 4, 8, mvfx_errorFormat, capacity + 4, "a scalar of 8 bytes");
    int64_t second = Padded(capacity + 8 + 4);
    int64_t third = Padded(second + 8 + Get32(bytes, second + 4));
    Expect(bytes, size, second, Get32(bytes, capacity), mvfx_errorFormat, second, "a tag twice");
    Expect(bytes, size, third, Get32(bytes, capacity), mvfx_errorFormat, third,
           "a tag out of order");
    Expect(bytes, size, capacity + 12, 1, mvfx_errorFormat, capacity + 12,
           "a byte of a field's padding");
    int64_t fixedBounds = EmitterPayload(bytes, offsetof(mvfxEmitterDef, fixedBounds));
    Expect(bytes, size, fixedBounds, 2, mvfx_errorFormat, fixedBounds, "a bool of 2");
    Expect(bytes, size, fixedBounds, 0x0101, mvfx_errorFormat, fixedBounds + 1, "a bool's padding");
    int64_t rotation =
        Head(bytes, Collider(bytes, 0), TagOf(ColliderType(), offsetof(mvfxCollider, rotation)));
    Expect(bytes, size, rotation + 4, 12, mvfx_errorFormat, rotation + 4,
           "a fixed array of 3 floats for 4");
    int64_t list = SizeCurves(bytes);
    Expect(bytes, size, list + 8, 2, mvfx_errorFormat, list + 8, "a fixed list of 2 for 3");
    Expect(bytes, size, list + 8, 4, mvfx_errorFormat, list + 8, "a fixed list of 4 for 3");
}

// A tag a struct does not have, at each level, as only a later version
// writes: refused with mvfx_errorVersion at the field, never misread.
static void TestLaterFields(void)
{
    Def def;
    MakeDef(&def);
    def.emitter.shape.radius = 2.0f;
    def.emitter.overLife.size.size[1].value = 2.0f;
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    Span effect = EffectOf(bytes);
    int64_t emitters =
        Head(bytes, effect, TagOf(mvfxFileEffectDef, offsetof(mvfxEffectDef, emitters)));
    CHECK(Padded(emitters + 8 + Get32(bytes, emitters + 4)) == effect.end,
          "the effect's last field");
    Expect(bytes, size, emitters, LaterTag(mvfxFileEffectDef), mvfx_errorVersion, emitters,
           "a later field of the effect");
    // The emitter's last field: its colliders, modules and shape come first.
    Span fields = Emitter(bytes);
    int64_t last = fields.at;
    while (Padded(last + 8 + Get32(bytes, last + 4)) < fields.end)
    {
        last = Padded(last + 8 + Get32(bytes, last + 4));
    }
    Expect(bytes, size, last, LaterTag(EmitterType()), mvfx_errorVersion, last,
           "a later field of an emitter");
    const mvfxFileType* shape = TypeOf(EmitterType(), offsetof(mvfxEmitterDef, shape));
    int64_t radius = EmitterPayload(bytes, offsetof(mvfxEmitterDef, shape));
    Expect(bytes, size, radius, LaterTag(shape), mvfx_errorVersion, radius,
           "a later field of a nested struct");
    int64_t grid =
        Head(bytes, Collider(bytes, 0), TagOf(ColliderType(), offsetof(mvfxCollider, grid)));
    Expect(bytes, size, grid, LaterTag(ColliderType()), mvfx_errorVersion, grid,
           "a later field of an array's item");
    int64_t list = SizeCurves(bytes);
    int64_t value = Item(bytes, list, 1).at;
    Expect(bytes, size, value, LaterTag(CurveType()), mvfx_errorVersion, value,
           "a later field of a fixed list's item");
    // A tag between the struct's own is as unknown: none is retired yet, so
    // the effect's tag 0 stands for one.
    Expect(bytes, size, emitters, 0, mvfx_errorFormat, emitters, "a tag of 0");
}

// The references: each collider's grid is its last field. The first names
// ball (0): as 1, before 0 is named; as -2. The second names ground (1):
// as 0 too, ground is never referred to; as 2, past the names.
static void TestReferences(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    int64_t refs[2];
    for (int32_t i = 0; i < 2; ++i)
    {
        refs[i] =
            Head(bytes, Collider(bytes, i), TagOf(ColliderType(), offsetof(mvfxCollider, grid))) +
            8;
        CHECK(Get32(bytes, refs[i]) == (uint32_t)i, "a reference found");
    }
    Expect(bytes, size, refs[0], 1, mvfx_errorFormat, refs[0], "a name before its turn");
    Expect(bytes, size, refs[0], (uint32_t)-2, mvfx_errorFormat, refs[0], "a name below -1");
    Expect(bytes, size, refs[1], 0, mvfx_errorFormat, 76, "a name never referred to");
    Expect(bytes, size, refs[1], 2, mvfx_errorFormat, refs[1], "a name past the names");
    // As -1, none: the second, 1, comes before its turn; both none, no
    // name is referred to, the first found, before the def's validator
    // would refuse heightfields without their grids.
    Expect(bytes, size, refs[0], (uint32_t)-1, mvfx_errorFormat, refs[1], "none, then 1");
    Set32(bytes, refs[1], (uint32_t)-1);
    Expect(bytes, size, refs[0], (uint32_t)-1, mvfx_errorFormat, 68, "no name referred to");
    // Ball, ground, then ball again; the third as 2 names past every name.
    MakeDef(&def);
    def.emitter.colliderCount = 3;
    size = Write(&def, bytes);
    int64_t third =
        Head(bytes, Collider(bytes, 2), TagOf(ColliderType(), offsetof(mvfxCollider, grid))) + 8;
    CHECK(Get32(bytes, third) == 0, "the third reference found");
    Expect(bytes, size, third, 2, mvfx_errorFormat, third, "a name past every name");
}

// Arrays at their caps read back; arrays of none read as NULL.
static void TestCaps(void)
{
    Def def;
    MakeDef(&def);
    mvfxCollider planes[MVFX_MAX_COLLIDERS];
    for (int32_t i = 0; i < MVFX_MAX_COLLIDERS; ++i)
    {
        planes[i] = (mvfxCollider){.kind = mvfx_colliderPlane,
                                   .normal = {0, 1, 0},
                                   .center = {0, (float)i, 0},
                                   .rotation = {0, 0, 0, 1}};
    }
    def.emitter.colliders = planes;
    def.emitter.colliderCount = MVFX_MAX_COLLIDERS;
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    mvfxEffectDef read;
    CHECK(SameBytesAgain(bytes, size, &read) &&
              read.emitters[0].colliderCount == MVFX_MAX_COLLIDERS &&
              read.emitters[0].colliders[MVFX_MAX_COLLIDERS - 1].center[1] == 7.0f,
          "colliders at their cap, the same bytes again");
    // No colliders, no modules: NULL arrays.
    MakeDef(&def);
    def.emitter.colliderCount = 0;
    def.emitter.hostModuleCount = 0;
    size = Write(&def, bytes);
    CHECK(Refused(Read(bytes, size, &read), mvfx_success, -1) &&
              read.emitters[0].colliders == NULL && read.emitters[0].hostModules == NULL &&
              read.parameters == NULL,
          "arrays of none are NULL");
}

// Assets of other kinds that match an object's other fields or share its
// name are passed over; a mesh is named by its id whole.
static void TestDecoys(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    uint8_t other[4096];
    int64_t size = Write(&def, bytes);
    mvfxEffectAsset table[7] = {
        {.kind = mvfx_assetMesh, .name = "decoy", .nameLength = 5, .grid = {2, 1}, .mesh = {7, 7}},
        {.kind = mvfx_assetMesh, .name = "ball", .nameLength = 4, .mesh = {8, 8}},
        {.kind = mvfx_assetGrid, .name = "rock", .nameLength = 4, .grid = {3, 1}},
    };
    memcpy(table + 3, s_assets, sizeof s_assets);
    int64_t written = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, table, 7, other, 4096, &written, NULL), mvfx_success,
                  -1) &&
              written == size && memcmp(bytes, other, (size_t)size) == 0,
          "decoys passed over in writing");
    int64_t needed = 0;
    mvfxEffectDef read;
    CHECK(Refused(mvfxReadEffect(bytes, size, table, 7, s_memory, sizeof s_memory, &needed, &read,
                                 NULL),
                  mvfx_success, -1) &&
              read.emitters[0].colliders[0].grid.index1 == 2,
          "decoys passed over in reading");
    // A mesh shape: named by its whole id.
    def.emitter.shape.kind = mvfx_shapeMesh;
    def.emitter.shape.mesh = (mvfxMeshId){1, 1};
    size = Write(&def, bytes);
    Span meshes = SectionOf(bytes, 2);
    CHECK(meshes.at > 0 && Get32(bytes, meshes.at) == 1, "the mesh named");
    CHECK(Refused(Read(bytes, size, &read), mvfx_success, -1) &&
              read.emitters[0].shape.mesh.index1 == 1 &&
              read.emitters[0].shape.mesh.generation == 1,
          "the mesh bound");
    def.emitter.shape.mesh.generation = 2;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &written, NULL),
                  mvfx_errorMissing, -1),
          "a mesh of another generation missing");
}

// Files cut inside their header, their effect section or a name's bytes;
// counts and hashes off by what the first checks miss.
static void TestCuts(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    // A header alone, hashed: too short for its section table.
    uint8_t header[4096] = {0};
    memcpy(header, bytes, 24);
    Set32(header, 8, 24);
    Rehash(header, 24);
    mvfxEffectDef read;
    CHECK(Refused(Read(header, 24, &read), mvfx_errorFormat, 12), "a header alone");
    // The effect section 8 bytes short: its one field runs past it.
    uint8_t cut[4096];
    memcpy(cut, bytes, (size_t)size);
    Set32(cut, 8, (uint32_t)size - 8);
    Set32(cut, 24 + 24 + 8, Get32(cut, 24 + 24 + 8) - 8);
    Rehash(cut, size - 8);
    CHECK(Refused(Read(cut, size - 8, &read), mvfx_errorFormat, 104 + 4),
          "the effect section cut short");
    memcpy(cut, bytes, (size_t)size);
    cut[20] ^= 1;
    CHECK(Refused(Read(cut, size, &read), mvfx_errorFormat, 16), "the hash's high half");
    // A name of the most bytes, then its length one more: it would end
    // inside its section.
    static uint8_t longFile[8192];
    static char longName[MVFX_MAX_ASSET_NAME];
    memset(longName, 'a', sizeof longName);
    mvfxEffectAsset table[4];
    memcpy(table, s_assets, sizeof table);
    table[1].name = longName;
    table[1].nameLength = MVFX_MAX_ASSET_NAME;
    int64_t longSize = 0;
    CHECK(
        mvfxWriteEffect(&def.effect, table, 4, longFile, sizeof longFile, &longSize, NULL).result ==
            mvfx_success,
        "a name of the most bytes written");
    int64_t needed = 0;
    CHECK(mvfxReadEffect(longFile, longSize, table, 4, s_memory, sizeof s_memory, &needed, &read,
                         NULL)
                  .result == mvfx_success,
          "and read");
    Set32(longFile, 68, MVFX_MAX_ASSET_NAME + 1);
    Rehash(longFile, longSize);
    CHECK(Refused(mvfxReadEffect(longFile, longSize, table, 4, s_memory, sizeof s_memory, &needed,
                                 &read, NULL),
                  mvfx_errorFormat, 68),
          "a name a byte too long");
}

// A def its parts' validator refuses: plexus links of none.
static void TestParts(void)
{
    Def def;
    MakeDef(&def);
    def.emitter.plexus = (mvfxPlexusDef){
        .enable = true, .distance = 0.5f, .maxLinks = 1, .maxPerParticle = 1, .width = 0.01f};
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    const mvfxFileType* plexus = TypeOf(EmitterType(), offsetof(mvfxEmitterDef, plexus));
    int64_t links =
        Head(bytes, Nested(bytes, EmitterPayload(bytes, offsetof(mvfxEmitterDef, plexus)) - 8),
             TagOf(plexus, offsetof(mvfxPlexusDef, maxLinks))) +
        8;
    CHECK(Get32(bytes, links) == 1, "the links found");
    Expect(bytes, size, links, 0, mvfx_errorFormat, links - 8, "no plexus links");
    def.emitter.plexus.maxLinks = 0;
    int64_t written = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &written, NULL),
                  mvfx_errorInvalid, -1),
          "the writer refuses it too");
}

// Bytes after the def in its section and after the last section, and defs
// the validator refuses.
static void TestTrailing(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    uint8_t longer[4096];
    memcpy(longer, bytes, (size_t)size);
    memset(longer + size, 0, 8);
    Set32(longer, 8, (uint32_t)size + 8);
    Rehash(longer, size + 8);
    mvfxEffectDef read;
    CHECK(Refused(Read(longer, size + 8, &read), mvfx_errorFormat, size),
          "bytes after the last section");
    Set32(longer, 24 + 24 + 8, Get32(longer, 24 + 24 + 8) + 8);
    Rehash(longer, size + 8);
    CHECK(Refused(Read(longer, size + 8, &read), mvfx_errorFormat, size), "bytes after the def");
    // The capacity set to 0 and past its most.
    int64_t capacity = EmitterPayload(bytes, offsetof(mvfxEmitterDef, capacity));
    CHECK(Get32(bytes, capacity) == 16, "the capacity found");
    // At the tag of the field refused.
    Expect(bytes, size, capacity, 0, mvfx_errorFormat, capacity - 8, "a def the validator refuses");
    // Past a named bound too: what a file holds is malformed, whatever check
    // it fails.
    Expect(bytes, size, capacity, MVFX_MAX_EMITTER_CAPACITY + 1, mvfx_errorFormat, capacity - 8,
           "a def past a bound");
}

// The memory a def needs: measured, a byte short refused, read in exactly.
// Reads bytes after a change at at, the hash set again; the result and
// the refusal.
static mvfxFileResult ReadChanged(const uint8_t* file, int64_t size, int64_t at, uint32_t value,
                                  mvfxEffectRefusal* refusal)
{
    static uint8_t bytes[4096];
    memcpy(bytes, file, (size_t)size);
    Set32(bytes, at, value);
    Rehash(bytes, size);
    mvfxEffectDef read;
    int64_t needed = 0;
    return mvfxReadEffect(bytes, size, s_assets, 4, s_memory, sizeof s_memory, &needed, &read,
                          refusal);
}

// The field each refusal of a file names, and its offset: a fault of the
// effect section's walk at the field the walk is in, a def refused at its
// field's tag or, for a field the file leaves out, at the nearest struct
// or item holding it that the file has; the writer's at the field that
// refers to an asset the table lacks.
static void TestRefusedFields(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    mvfxEffectRefusal refusal;
    // A count past its cap: in the array.
    int64_t colliders =
        Head(bytes, Emitter(bytes), TagOf(EmitterType(), offsetof(mvfxEmitterDef, colliders)));
    mvfxFileResult read = ReadChanged(bytes, size, colliders + 8, MVFX_MAX_COLLIDERS + 1, &refusal);
    CHECK(Refused(read, mvfx_errorFormat, colliders + 8) && refusal.result == mvfx_errorFormat &&
              IsPath(&refusal, "emitters[0].colliders") && refusal.emitter == 0,
          "a count past its cap: the array");
    // A tag this version does not know, in an item: the item.
    int64_t grid =
        Head(bytes, Collider(bytes, 1), TagOf(ColliderType(), offsetof(mvfxCollider, grid)));
    read = ReadChanged(bytes, size, grid, LaterTag(ColliderType()), &refusal);
    CHECK(Refused(read, mvfx_errorVersion, grid) && refusal.result == mvfx_errorVersion &&
              IsPath(&refusal, "emitters[0].colliders[1]"),
          "an unknown tag: the item it is in");
    // Bad header bytes: no field.
    read = ReadChanged(bytes, size, 0, 0, &refusal);
    CHECK(Refused(read, mvfx_errorFormat, 0) && refusal.tagCount == 0 && IsPath(&refusal, ""),
          "the magic: no field");

    // Emitter 1 spawns at emitter 0's births (source 0 and kind 0, their
    // defaults, left out); emitter 0 writing none refuses the source, at
    // the event spawn.
    mvfxEmitterDef two[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    two[0].events = mvfx_eventMaskBirth;
    two[0].eventCapacity = 4;
    two[1].eventSpawn =
        (mvfxEventSpawn){.enable = true, .source = 0, .minCount = 1, .maxCount = 1, .capacity = 4};
    mvfxEffectDef spawning = mvfxDefaultEffectDef();
    spawning.emitters = two;
    spawning.emitterCount = 2;
    size = 0;
    CHECK(mvfxWriteEffect(&spawning, NULL, 0, bytes, sizeof bytes, &size, &refusal).result ==
                  mvfx_success &&
              refusal.result == mvfx_success && refusal.tagCount == 0,
          "the spawning effect written");
    int64_t list =
        Head(bytes, EffectOf(bytes), TagOf(mvfxFileEffectDef, offsetof(mvfxEffectDef, emitters)));
    int64_t events =
        Head(bytes, Item(bytes, list, 0), TagOf(EmitterType(), offsetof(mvfxEmitterDef, events)));
    int64_t spawn = Head(bytes, Item(bytes, list, 1),
                         TagOf(EmitterType(), offsetof(mvfxEmitterDef, eventSpawn)));
    read = ReadChanged(bytes, size, events + 8, 0, &refusal);
    CHECK(Refused(read, mvfx_errorFormat, spawn) && refusal.result == mvfx_errorFormat &&
              IsPath(&refusal, "emitters[1].eventSpawn.source") && refusal.emitter == 1,
          "a field left out: at the struct holding it");

    // The writer: the field that refers to an object the table lacks.
    MakeDef(&def);
    size = 0;
    read = mvfxWriteEffect(&def.effect, s_assets, 3, bytes, sizeof bytes, &size, &refusal);
    CHECK(Refused(read, mvfx_errorMissing, -1) && refusal.result == mvfx_errorMissing &&
              IsPath(&refusal, "emitters[0].hostModules[0].function"),
          "the writer: a module the table lacks");
}

static void TestMemory(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    int64_t needed = 0;
    mvfxEffectDef read;
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets, 4, NULL, 0, &needed, &read, NULL),
                  mvfx_errorCapacity, -1),
          "measured");
    CHECK(needed >=
              (int64_t)(sizeof(mvfxEmitterDef) + 2 * sizeof(mvfxCollider) + sizeof(mvfxHostModule)),
          "the arrays' bytes");
    int64_t again = 0;
    CHECK(
        Refused(mvfxReadEffect(bytes, size, s_assets, 4, s_memory, needed - 1, &again, &read, NULL),
                mvfx_errorCapacity, -1) &&
            again == needed,
        "a byte short");
    memset(s_memory, 0xAB, sizeof s_memory);
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets, 4, s_memory, needed, &again, &read, NULL),
                  mvfx_success, -1) &&
              again == needed,
          "read in exactly");
    CHECK((const uint8_t*)read.emitters == s_memory && s_memory[needed] == 0xAB,
          "the memory from its start, not past its end");
    uint8_t written[4096];
    int64_t rewritten = 0;
    CHECK(Refused(mvfxWriteEffect(&read, s_assets, 4, written, 4096, &rewritten, NULL),
                  mvfx_success, -1) &&
              rewritten == size && memcmp(bytes, written, (size_t)size) == 0,
          "the same bytes again");
}

// A walk given less memory than it lays, as when the bytes change between
// a read's measuring walk and its reading walk: refused, never laid past.
static void TestMemoryGuard(void)
{
    Def def;
    MakeDef(&def);
    def.emitter.colliderCount = 0;
    def.emitter.hostModuleCount = 0;
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    int64_t needed = 0;
    mvfxEffectDef read;
    CHECK(mvfxReadEffect(bytes, size, s_assets, 4, NULL, 0, &needed, &read, NULL).result ==
              mvfx_errorCapacity,
          "measured");
    memset(s_memory, 0xAB, sizeof s_memory);
    Span effect = EffectOf(bytes);
    mvfxCodec codec = {.in = bytes,
                       .at = (size_t)effect.at,
                       .end = (size_t)effect.end,
                       .defaults = mvfxFileDefaults,
                       .memory = s_memory,
                       .capacity = (size_t)needed - 1,
                       .faultAt = -1};
    mvfxEffectDef def2 = {0};
    mvfxReadFields(&codec, mvfxFileEffectDef, (uint8_t*)&def2, (size_t)effect.end);
    CHECK(codec.fault == mvfx_errorCapacity && s_memory[needed - 1] == 0xAB,
          "nothing laid past the memory");
}

// Allocations of a test allocator, failing when told to.
static bool s_failAllocations;
static int32_t s_live;

static void* Allocate(size_t size, size_t alignment, void* user)
{
    (void)user;
    if (s_failAllocations)
    {
        return NULL;
    }
    void* block = malloc(size + alignment + sizeof(void*));
    if (block == NULL)
    {
        return NULL;
    }
    uintptr_t start =
        ((uintptr_t)block + sizeof(void*) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    memcpy((void*)(start - sizeof(void*)), &block, sizeof block);
    s_live += 1;
    return (void*)start;
}

static void Free(void* memory, size_t size, size_t alignment, void* user)
{
    (void)user;
    (void)size;
    (void)alignment;
    if (memory != NULL)
    {
        void* block = NULL;
        memcpy(&block, (uint8_t*)memory - sizeof(void*), sizeof block);
        free(block);
        s_live -= 1;
    }
}

static void TestCreate(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){.alloc = Allocate, .free = Free};
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    float heights[4] = {0};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 1;
    gridDef.size[2] = 2;
    gridDef.values = heights;
    mvfxEffectAsset table[4];
    memcpy(table, s_assets, sizeof table);
    CHECK(mvfxCreateGrid(context, &gridDef, &table[0].grid) == mvfx_success &&
              mvfxCreateGrid(context, &gridDef, &table[1].grid) == mvfx_success,
          "grids");
    Def def;
    MakeDef(&def);
    def.colliders[0].grid = table[1].grid;
    def.colliders[1].grid = table[0].grid;
    uint8_t bytes[4096];
    int64_t size = 0;
    CHECK(mvfxWriteEffect(&def.effect, table, 4, bytes, 4096, &size, NULL).result == mvfx_success,
          "written");
    int32_t live = s_live;
    mvfxEffectId effect = {0};
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect, NULL),
                  mvfx_success, -1) &&
              mvfxEffect_IsValid(context, effect),
          "created from bytes");
    CHECK(s_live == live + 1, "the effect's block alone kept");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "destroyed");
    s_failAllocations = true;
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect, NULL),
                  mvfx_errorCapacity, -1) &&
              effect.index1 == 0,
          "the allocator failing");
    s_failAllocations = false;
    bytes[size - 1] ^= 1;
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect, NULL),
                  mvfx_errorFormat, 16),
          "a file refused at its offset");
    bytes[size - 1] ^= 1;
    CHECK(mvfxDestroyGrid(context, table[0].grid) == mvfx_success, "grid destroyed");
    // At the tag of the field that names it.
    mvfxEffectRefusal refusal;
    mvfxFileResult stale =
        mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect, &refusal);
    CHECK(stale.result == mvfx_errorStale && refusal.result == mvfx_errorStale &&
              stale.offset > 0 && Get32(bytes, stale.offset) == 9 &&
              IsPath(&refusal, "emitters[0].colliders[1].grid"),
          "a grid destroyed, refused by the effect at its field");
    CHECK(s_live == live - 1, "the read memory freed");
    uint64_t misuse = mvfxContext_GetMisuse(context);
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, NULL, NULL),
                  mvfx_errorInvalid, -1) &&
              mvfxContext_GetMisuse(context) == misuse + 1,
          "a NULL out counted as misuse");
    effect = (mvfxEffectId){1, 1};
    CHECK(Refused(mvfxCreateEffectFromBytes(NULL, bytes, size, table, 4, &effect, NULL),
                  mvfx_errorInvalid, -1) &&
              effect.index1 == 0 && effect.generation == 0,
          "a NULL context, the null id given");
    mvfxDestroyContext(context);
    CHECK(s_live == 0, "everything freed");
}

// A def of one emitter, written with the table and created from its
// bytes: what a file asks of the program's assets that they do not hold is
// refused with mvfx_errorInvalid as untrusted data, counting no misuse.
// The same def passed by the program itself counts one when counted is
// true: a correct program does not pass it.
static bool RefusedFromFile(mvfxContext* context, const mvfxEmitterDef* emitter,
                            const mvfxEffectAsset* table, int32_t count, bool counted,
                            const char* path)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    static uint8_t bytes[4096];
    int64_t size = 0;
    bool written = mvfxWriteEffect(&def, table, count, bytes, sizeof bytes, &size, NULL).result ==
                   mvfx_success;
    uint64_t misuse = mvfxContext_GetMisuse(context);
    mvfxEffectId effect = {1, 1};
    mvfxEffectRefusal refusal;
    mvfxFileResult result =
        mvfxCreateEffectFromBytes(context, bytes, size, table, count, &effect, &refusal);
    // Refused at the field that names the asset.
    bool file = result.result == mvfx_errorInvalid && result.offset > 0 && IsPath(&refusal, path) &&
                effect.index1 == 0 && mvfxContext_GetMisuse(context) == misuse;
    bool program = mvfxCreateEffect(context, &def, &effect, &refusal) == mvfx_errorInvalid &&
                   IsPath(&refusal, path) &&
                   mvfxContext_GetMisuse(context) == misuse + (counted ? 1 : 0);
    return written && file && program;
}

static void TestUntrustedAssets(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    // A grid of one component two points tall, which is no heightfield and
    // no vector field; points with no triangles; a flat square.
    float values[8] = {0};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.values = values;
    static const float points[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    static const float flat[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = points;
    meshDef.vertexCount = 4;
    mvfxEffectAsset table[3] = {
        {.kind = mvfx_assetGrid, .name = "tall", .nameLength = 4},
        {.kind = mvfx_assetMesh, .name = "points", .nameLength = 6},
        {.kind = mvfx_assetMesh, .name = "flat", .nameLength = 4},
    };
    bool made = mvfxCreateGrid(context, &gridDef, &table[0].grid) == mvfx_success &&
                mvfxCreateMesh(context, &meshDef, &table[1].mesh) == mvfx_success;
    meshDef.positions = flat;
    made = made && mvfxCreateMesh(context, &meshDef, &table[2].mesh) == mvfx_success;
    CHECK(made, "the assets");

    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    const mvfxCollider heights = {.kind = mvfx_colliderHeightfield,
                                  .halfExtents = {2, 1, 2},
                                  .rotation = {0, 0, 0, 1},
                                  .grid = table[0].grid};
    emitter.collision.enable = true;
    emitter.colliders = &heights;
    emitter.colliderCount = 1;
    CHECK(RefusedFromFile(context, &emitter, table, 3, true, "emitters[0].colliders[0].grid"),
          "a heightfield of a tall grid");

    emitter = mvfxDefaultEmitterDef();
    const mvfxForce field = {.kind = mvfx_forceVectorField,
                             .strength = {.kind = mvfx_sourceRange, .range = {1.0f, 1.0f}},
                             .grid = table[0].grid,
                             .boxSize = {1, 1, 1},
                             .boxRotation = {0, 0, 0, 1}};
    emitter.forces = &field;
    emitter.forceCount = 1;
    CHECK(RefusedFromFile(context, &emitter, table, 3, true, "emitters[0].forces[0].grid"),
          "a vector field over a grid of one component");

    emitter = mvfxDefaultEmitterDef();
    emitter.shape.kind = mvfx_shapeMesh;
    emitter.shape.mesh = table[1].mesh;
    emitter.shape.meshMode = mvfx_meshSurface;
    CHECK(RefusedFromFile(context, &emitter, table, 3, true, "emitters[0].shape.mesh"),
          "a surface of a mesh without one");

    emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.rigid = (mvfxRigidDef){.enable = true,
                                   .shape = mvfx_rigidHull,
                                   .density = 1.0f,
                                   .substeps = 8,
                                   .staticFriction = 0.6f,
                                   .dynamicFriction = 0.4f,
                                   .hull = table[2].mesh};
    CHECK(RefusedFromFile(context, &emitter, table, 3, false, "emitters[0].rigid.hull"),
          "a hull of a flat mesh, misuse from neither");
    mvfxDestroyContext(context);
}

// The bytes' pointer and size and the asset table are the program's own
// arguments: each invalid one is refused with mvfx_errorInvalid at offset
// -1 and counts one misuse. What the bytes hold counts none: a damaged
// file, or one naming an asset the table lacks.
static void TestArgumentMisuse(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    Def def;
    MakeDef(&def);
    static uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    static const char bad[1] = {(char)0xFF};
    const mvfxEffectAsset tables[6][2] = {
        {{.kind = 250, .name = "a", .nameLength = 1}},
        {{.kind = mvfx_assetGrid, .name = NULL, .nameLength = 1}},
        {{.kind = mvfx_assetGrid, .name = "a", .nameLength = 0}},
        {{.kind = mvfx_assetGrid, .name = "a", .nameLength = MVFX_MAX_ASSET_NAME + 1}},
        {{.kind = mvfx_assetGrid, .name = bad, .nameLength = 1}},
        {{.kind = mvfx_assetGrid, .name = "a", .nameLength = 1},
         {.kind = mvfx_assetGrid, .name = "a", .nameLength = 1}},
    };
    mvfxEffectId effect = {0};
    uint64_t misuse = mvfxContext_GetMisuse(context);
    int32_t counted = 0;
    counted += Refused(mvfxCreateEffectFromBytes(context, NULL, size, s_assets, 4, &effect, NULL),
                       mvfx_errorInvalid, -1);
    counted += Refused(mvfxCreateEffectFromBytes(context, bytes, -1, s_assets, 4, &effect, NULL),
                       mvfx_errorInvalid, -1);
    counted += Refused(mvfxCreateEffectFromBytes(context, bytes, size, NULL, 4, &effect, NULL),
                       mvfx_errorInvalid, -1);
    counted += Refused(mvfxCreateEffectFromBytes(context, bytes, size, s_assets, -1, &effect, NULL),
                       mvfx_errorInvalid, -1);
    for (int32_t t = 0; t < 6; ++t)
    {
        // A name past MVFX_MAX_ASSET_NAME is past a named bound, and as a
        // direct argument still misuse.
        counted += Refused(mvfxCreateEffectFromBytes(context, bytes, size, tables[t],
                                                     t == 5 ? 2 : 1, &effect, NULL),
                           t == 3 ? mvfx_errorLimit : mvfx_errorInvalid, -1);
    }
    CHECK(counted == 10 && mvfxContext_GetMisuse(context) == misuse + 10,
          "each invalid argument refused and counted as misuse");
    misuse = mvfxContext_GetMisuse(context);
    bytes[size - 1] ^= 1;
    bool damaged =
        Refused(mvfxCreateEffectFromBytes(context, bytes, size, s_assets, 4, &effect, NULL),
                mvfx_errorFormat, 16);
    bytes[size - 1] ^= 1;
    bool missing =
        mvfxCreateEffectFromBytes(context, bytes, size, s_assets, 3, &effect, NULL).result ==
        mvfx_errorMissing;
    CHECK(damaged && missing && mvfxContext_GetMisuse(context) == misuse,
          "a damaged file and one naming an asset the table lacks counted as nothing");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestArguments();
    TestAssetTables();
    TestLayout();
    TestDefaults();
    TestMissing();
    TestHeaderAndTable();
    TestNames();
    TestFields();
    TestLaterFields();
    TestReferences();
    TestTrailing();
    TestCaps();
    TestDecoys();
    TestCuts();
    TestParts();
    TestRefusedFields();
    TestMemory();
    TestMemoryGuard();
    TestCreate();
    TestUntrustedAssets();
    TestArgumentMisuse();
    return s_failures == 0 ? 0 : 1;
}
