// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect files: the refusals of the calls' arguments and of asset tables;
// a file's header, unit table and names as written, names in the order
// the def first refers to them; objects the table lacks; every refusal of
// a reader at the offset of its byte, each field found by writing two
// defs that differ in it; every file cut short refused; the memory a def
// needs; and effects created from bytes. The golden test runs the whole
// scene through files.

#include "file_codec.h"
#include "file_fields.h"
#include "test_harness.h"

#include "maul-vfx/file.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/mesh.h"

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
    mvfxCollider colliders[2];
    mvfxHostModule module;
} Def;

static void MakeDef(Def* def)
{
    def->emitter = mvfxDefaultEmitterDef();
    def->emitter.capacity = 16;
    for (int32_t i = 0; i < 2; ++i)
    {
        def->colliders[i] = (mvfxCollider){.kind = mvfx_colliderHeightfield,
                                           .halfExtents = {2, 0, 2},
                                           .rotation = {0, 0, 0, 1},
                                           .grid = {2 - (uint32_t)i, 1}};
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
    uint64_t hash = mvfxHash64(MVFX_HASH_INIT, bytes + 24, (int32_t)(size - 24));
    Set32(bytes, 16, (uint32_t)hash);
    Set32(bytes, 20, (uint32_t)(hash >> 32));
}

// A file of a def with the table, in bytes of 4096.
static int64_t Write(const Def* def, uint8_t* bytes)
{
    int64_t size = 0;
    mvfxFileResult result = mvfxWriteEffect(&def->effect, s_assets, 4, bytes, 4096, &size);
    CHECK(result.result == mvfx_success && result.offset == -1, "written");
    return size;
}

// Memory for read defs, aligned to 16.
static _Alignas(16) uint8_t s_memory[1 << 16];

static mvfxFileResult Read(const uint8_t* bytes, int64_t size, mvfxEffectDef* def)
{
    int64_t needed = 0;
    return mvfxReadEffect(bytes, size, s_assets, 4, s_memory, sizeof s_memory, &needed, def);
}

// The offset of the first byte two files differ in past their hashes, -1
// for none.
static int64_t FirstDifference(const uint8_t* a, const uint8_t* b, int64_t size)
{
    for (int64_t i = 24; i < size; ++i)
    {
        if (a[i] != b[i])
        {
            return i;
        }
    }
    return -1;
}

static bool Refused(mvfxFileResult result, mvfxResult status, int64_t offset)
{
    return result.result == status && result.offset == offset;
}

static void TestArguments(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = 1;
    CHECK(Refused(mvfxWriteEffect(NULL, s_assets, 4, bytes, 4096, &size), mvfx_errorInvalid, -1) &&
              size == 0,
          "a NULL def");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, NULL), mvfx_errorInvalid,
                  -1),
          "a NULL size");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, NULL, 4096, &size), mvfx_errorInvalid,
                  -1),
          "NULL bytes with a capacity");
    CHECK(
        Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, -1, &size), mvfx_errorInvalid, -1),
        "a capacity below 0");
    CHECK(Refused(mvfxWriteEffect(&def.effect, NULL, 1, bytes, 4096, &size), mvfx_errorInvalid, -1),
          "a NULL table with a count");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, -1, bytes, 4096, &size), mvfx_errorInvalid,
                  -1),
          "a count below 0");
    def.emitter.capacity = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size), mvfx_errorInvalid,
                  -1),
          "a def the validator refuses");
    MakeDef(&def);
    def.effect.cookie = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size), mvfx_errorInvalid,
                  -1),
          "a def without its cookie");
    MakeDef(&def);
    int64_t written = Write(&def, bytes);
    int64_t needed = 1;
    mvfxEffectDef read = {.cookie = 1};
    CHECK(Refused(mvfxReadEffect(NULL, 8, s_assets, 4, s_memory, 64, &needed, &read),
                  mvfx_errorInvalid, -1) &&
              needed == 0 && read.cookie == 0,
          "NULL bytes with a size");
    CHECK(Refused(mvfxReadEffect(bytes, -1, s_assets, 4, s_memory, 64, &needed, &read),
                  mvfx_errorInvalid, -1),
          "a size below 0");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, NULL, 64, &needed, &read),
                  mvfx_errorInvalid, -1),
          "NULL memory with a capacity");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory, -1, &needed, &read),
                  mvfx_errorInvalid, -1),
          "a capacity below 0");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory + 8, 64, &needed, &read),
                  mvfx_errorInvalid, -1),
          "memory not aligned to 16");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory, 64, NULL, &read),
                  mvfx_errorInvalid, -1),
          "a NULL needed");
    CHECK(Refused(mvfxReadEffect(bytes, written, s_assets, 4, s_memory, 64, &needed, NULL),
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
        CHECK(Refused(mvfxWriteEffect(&def.effect, table, 4, bytes, 4096, &size), mvfx_errorInvalid,
                      -1),
              "the writer refuses the table");
        CHECK(Refused(mvfxReadEffect(bytes, written, table, 4, s_memory, sizeof s_memory, &needed,
                                     &read),
                      mvfx_errorInvalid, -1),
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
    CHECK(Refused(mvfxWriteEffect(&def.effect, table, 7, bytes, 4096, &size), mvfx_success, -1) &&
              size == written,
          "names allowed");
}

// The header, the unit table and the names as written: ball before ground,
// as the colliders first refer to them; the module once.
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
    CHECK(fnv == 0x5E7AFA0B90468DE3ull && size == 1592, "the bytes as committed");
    CHECK(memcmp(bytes, "MVFX", 4) == 0, "the magic");
    CHECK(Get32(bytes, 4) == MVFX_FILE_VERSION && Get32(bytes, 8) == (uint32_t)size &&
              Get32(bytes, 12) == 4,
          "the version, the size, the units");
    // Grid names: ball, ground; mesh names: none; modules: kick.
    static const uint32_t table[4][4] = {
        {0, 88, 18, 2}, {1, 106, 0, 0}, {2, 106, 8, 1}, {3, 114, 0, 1}};
    for (int32_t u = 0; u < 4; ++u)
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            uint32_t expected = u == 3 && k == 2 ? (uint32_t)size - 114 : table[u][k];
            CHECK(Get32(bytes, 24 + 16 * u + 4 * k) == expected, "the unit table");
        }
    }
    CHECK(Get32(bytes, 88) == 4 && memcmp(bytes + 92, "ball", 4) == 0 && Get32(bytes, 96) == 6 &&
              memcmp(bytes + 100, "ground", 6) == 0 && Get32(bytes, 106) == 4 &&
              memcmp(bytes + 110, "kick", 4) == 0,
          "the names in the order referred to");
    int64_t needed = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, NULL, 0, &needed), mvfx_errorCapacity,
                  -1) &&
              needed == size,
          "measured");
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, size - 1, &needed),
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
    // Without colliders and modules, no names.
    def.emitter.colliderCount = 0;
    def.emitter.hostModuleCount = 0;
    size = Write(&def, bytes);
    CHECK(Get32(bytes, 24 + 4) == 88 && Get32(bytes, 24 + 8) == 0 && Get32(bytes, 24 + 12) == 0 &&
              Get32(bytes, 72 + 4) == 88,
          "no names");
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
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size), mvfx_errorMissing,
                  -1) &&
              size == 0,
          "a grid the table lacks");
    MakeDef(&def);
    def.module.user = NULL;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &size), mvfx_errorMissing,
                  -1),
          "a module with another user");
    MakeDef(&def);
    size = Write(&def, bytes);
    int64_t needed = 0;
    mvfxEffectDef read;
    CHECK(
        Refused(mvfxReadEffect(bytes, size, s_assets, 3, s_memory, sizeof s_memory, &needed, &read),
                mvfx_errorMissing, 106),
        "the module's name missing");
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets + 1, 3, s_memory, sizeof s_memory, &needed,
                                 &read),
                  mvfx_errorMissing, 96),
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
    CHECK(Refused(Read(changed, size, &read), mvfx_errorInvalid, 0), "the magic");
    Expect(bytes, size, 4, 2, mvfx_errorVersion, 4, "another version");
    Expect(bytes, size, 8, (uint32_t)size + 1, mvfx_errorInvalid, 8, "the size");
    Expect(bytes, size, 12, 5, mvfx_errorInvalid, 12, "the unit count");
    memcpy(changed, bytes, (size_t)size);
    changed[size - 1] ^= 1;
    CHECK(Refused(Read(changed, size, &read), mvfx_errorInvalid, 16), "the hash");
    Expect(bytes, size, 24 + 16, 2, mvfx_errorInvalid, 40, "a kind out of order");
    Expect(bytes, size, 24 + 16 + 4, 105, mvfx_errorInvalid, 44, "a unit not where the last ends");
    Expect(bytes, size, 24 + 8, (uint32_t)size, mvfx_errorInvalid, 32, "a unit past the file");
    Expect(bytes, size, 24 + 12, MVFX_FILE_MAX_GRIDS + 1, mvfx_errorInvalid, 36,
           "more names than a def refers to");
    Expect(bytes, size, 24 + 16 + 12, MVFX_FILE_MAX_MESHES + 1, mvfx_errorInvalid, 52,
           "more mesh names");
    Expect(bytes, size, 24 + 32 + 12, MVFX_FILE_MAX_MODULES + 1, mvfx_errorInvalid, 68,
           "more module names");
    Expect(bytes, size, 24 + 48 + 12, 2, mvfx_errorInvalid, 84, "two effects");
    Expect(bytes, size, 24 + 48 + 8, (uint32_t)size - 115, mvfx_errorInvalid, 80,
           "an effect ending before the file");
    // Every byte cut off.
    for (int64_t cut = 0; cut < size; ++cut)
    {
        mvfxFileResult result = Read(bytes, cut, &read);
        CHECK(result.result == mvfx_errorInvalid && result.offset == (cut < 4   ? 0
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
    Expect(bytes, size, 88, 0, mvfx_errorInvalid, 88, "an empty name");
    Expect(bytes, size, 88, MVFX_MAX_ASSET_NAME + 1, mvfx_errorInvalid, 88, "a name too long");
    Expect(bytes, size, 96, 7, mvfx_errorInvalid, 96, "a name past its unit");
    // "ball" as "bal\xFF", "ground" as "ball\0\0" with the length 4 and two
    // bytes over.
    Expect(bytes, size, 92, 0xFF6C6162u, mvfx_errorInvalid, 95, "a name not UTF-8");
    uint8_t changed[4096];
    memcpy(changed, bytes, (size_t)size);
    Set32(changed, 96, 4);
    memcpy(changed + 100, "ball", 4);
    Rehash(changed, size);
    mvfxEffectDef read;
    CHECK(Refused(Read(changed, size, &read), mvfx_errorInvalid, 96), "a name twice");
    // One name counted: the unit's bytes left over.
    Expect(bytes, size, 24 + 12, 1, mvfx_errorInvalid, 96, "bytes left in a unit");
    // Three counted: the next name's length runs out of the unit.
    Expect(bytes, size, 24 + 12, 3, mvfx_errorInvalid, 106, "a name past its unit's end");
}

// The offsets of the effect unit's fields, found by the first byte two
// files differ in.
static void TestEffectUnit(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    uint8_t other[4096];
    int64_t size = Write(&def, bytes);
    int64_t unit = 114;
    // The emitters' count is the unit's first field.
    Expect(bytes, size, unit, (uint32_t)-1, mvfx_errorInvalid, unit, "a count below 0");
    Expect(bytes, size, unit, 65, mvfx_errorInvalid, unit, "a count past its cap");
    Expect(bytes, size, unit, 2, mvfx_errorInvalid, size, "a second emitter past the unit");
    // A bool neither 0 nor 1.
    Write(&def, bytes);
    def.emitter.fixedBounds = true;
    Write(&def, other);
    int64_t flag = FirstDifference(bytes, other, size);
    CHECK(flag > unit, "a bool found");
    Expect(bytes, size, flag, Get32(bytes, flag) + 2, mvfx_errorInvalid, flag, "a bool of 2");
    // The references: a collider's grid follows its rotation, whose last
    // component's sign is the last byte of its bytes. The first names
    // ball (0): as 1, before 0 is named; as -2. The second names ground
    // (1): as 0 too, ground is never referred to; as 2, past the names.
    MakeDef(&def);
    size = Write(&def, bytes);
    int64_t refs[2];
    for (int32_t i = 0; i < 2; ++i)
    {
        MakeDef(&def);
        def.colliders[i].rotation[3] = -1.0f;
        Write(&def, other);
        refs[i] = FirstDifference(bytes, other, size) + 1;
        CHECK(refs[i] > unit && Get32(bytes, refs[i]) == (uint32_t)i, "a reference found");
    }
    Expect(bytes, size, refs[0], 1, mvfx_errorInvalid, refs[0], "a name before its turn");
    Expect(bytes, size, refs[0], (uint32_t)-2, mvfx_errorInvalid, refs[0], "a name below -1");
    Expect(bytes, size, refs[1], 0, mvfx_errorInvalid, 96, "a name never referred to");
    Expect(bytes, size, refs[1], 2, mvfx_errorInvalid, refs[1], "a name past the names");
    // As -1, none: the second, 1, comes before its turn; both none, no
    // name is referred to, the first found, before the def's validator
    // would refuse heightfields without their grids.
    Expect(bytes, size, refs[0], (uint32_t)-1, mvfx_errorInvalid, refs[1], "none, then 1");
    Set32(bytes, refs[1], (uint32_t)-1);
    Expect(bytes, size, refs[0], (uint32_t)-1, mvfx_errorInvalid, 88, "no name referred to");
}

// Arrays at their caps read back; a reference to the next name when every
// name is taken; arrays of none read as NULL.
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
    uint8_t again[4096];
    int64_t size = Write(&def, bytes);
    mvfxEffectDef read;
    CHECK(Refused(Read(bytes, size, &read), mvfx_success, -1) &&
              read.emitters[0].colliderCount == MVFX_MAX_COLLIDERS &&
              read.emitters[0].colliders[MVFX_MAX_COLLIDERS - 1].center[1] == 7.0f,
          "colliders at their cap");
    int64_t rewritten = 0;
    CHECK(mvfxWriteEffect(&read, s_assets, 4, again, 4096, &rewritten).result == mvfx_success &&
              rewritten == size && memcmp(bytes, again, (size_t)size) == 0,
          "the same bytes at the cap");
    // Ball, ground, then a plane naming 2 when there are two names.
    MakeDef(&def);
    mvfxCollider three[3] = {def.colliders[0], def.colliders[1], planes[0]};
    def.emitter.colliders = three;
    def.emitter.colliderCount = 3;
    size = Write(&def, bytes);
    three[2].rotation[3] = -1.0f;
    Write(&def, again);
    int64_t plane = FirstDifference(bytes, again, size) + 1;
    CHECK(Get32(bytes, plane) == (uint32_t)-1, "the plane's reference found");
    Expect(bytes, size, plane, 2, mvfx_errorInvalid, plane, "a name past every name");
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
    CHECK(
        Refused(mvfxWriteEffect(&def.effect, table, 7, other, 4096, &written), mvfx_success, -1) &&
            written == size && memcmp(bytes, other, (size_t)size) == 0,
        "decoys passed over in writing");
    int64_t needed = 0;
    mvfxEffectDef read;
    CHECK(Refused(mvfxReadEffect(bytes, size, table, 7, s_memory, sizeof s_memory, &needed, &read),
                  mvfx_success, -1) &&
              read.emitters[0].colliders[0].grid.index1 == 2,
          "decoys passed over in reading");
    // A mesh shape: named by its whole id.
    def.emitter.shape.kind = mvfx_shapeMesh;
    def.emitter.shape.mesh = (mvfxMeshId){1, 1};
    size = Write(&def, bytes);
    CHECK(Get32(bytes, 24 + 16 + 12) == 1, "the mesh named");
    CHECK(Refused(Read(bytes, size, &read), mvfx_success, -1) &&
              read.emitters[0].shape.mesh.index1 == 1 &&
              read.emitters[0].shape.mesh.generation == 1,
          "the mesh bound");
    def.emitter.shape.mesh.generation = 2;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &written),
                  mvfx_errorMissing, -1),
          "a mesh of another generation missing");
}

// Files cut inside their header, their effect unit or a name's bytes;
// counts and hashes off by what the first checks miss.
static void TestCuts(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    // A header alone, hashed: too short for its unit table.
    uint8_t header[4096] = {0};
    memcpy(header, bytes, 24);
    Set32(header, 8, 24);
    Rehash(header, 24);
    mvfxEffectDef read;
    CHECK(Refused(Read(header, 24, &read), mvfx_errorInvalid, 8), "a header alone");
    // The effect unit a byte short.
    uint8_t cut[4096];
    memcpy(cut, bytes, (size_t)size);
    Set32(cut, 8, (uint32_t)size - 1);
    Set32(cut, 24 + 48 + 8, Get32(cut, 24 + 48 + 8) - 1);
    Rehash(cut, size - 1);
    CHECK(Refused(Read(cut, size - 1, &read), mvfx_errorInvalid, size - 1),
          "the effect unit a byte short");
    Expect(bytes, size, 24 + 48 + 12, 0, mvfx_errorInvalid, 84, "no effect");
    memcpy(cut, bytes, (size_t)size);
    cut[20] ^= 1;
    CHECK(Refused(Read(cut, size, &read), mvfx_errorInvalid, 16), "the hash's high half");
    // A name of the most bytes, then its length one more: it would end
    // inside its unit.
    static uint8_t longFile[8192];
    static char longName[MVFX_MAX_ASSET_NAME];
    memset(longName, 'a', sizeof longName);
    mvfxEffectAsset table[4];
    memcpy(table, s_assets, sizeof table);
    table[1].name = longName;
    table[1].nameLength = MVFX_MAX_ASSET_NAME;
    int64_t longSize = 0;
    CHECK(mvfxWriteEffect(&def.effect, table, 4, longFile, sizeof longFile, &longSize).result ==
              mvfx_success,
          "a name of the most bytes written");
    int64_t needed = 0;
    CHECK(mvfxReadEffect(longFile, longSize, table, 4, s_memory, sizeof s_memory, &needed, &read)
                  .result == mvfx_success,
          "and read");
    Set32(longFile, 88, MVFX_MAX_ASSET_NAME + 1);
    Rehash(longFile, longSize);
    CHECK(Refused(mvfxReadEffect(longFile, longSize, table, 4, s_memory, sizeof s_memory, &needed,
                                 &read),
                  mvfx_errorInvalid, 88),
          "a name a byte too long");
}

// A def its parts' validator refuses: plexus links of none.
static void TestParts(void)
{
    Def def;
    MakeDef(&def);
    def.emitter.plexus = (mvfxPlexusDef){
        .enabled = true, .distance = 0.5f, .maxLinks = 1, .maxPerParticle = 1, .width = 0.01f};
    uint8_t bytes[4096];
    uint8_t other[4096];
    int64_t size = Write(&def, bytes);
    def.emitter.plexus.maxLinks = 2;
    Write(&def, other);
    int64_t links = FirstDifference(bytes, other, size);
    CHECK(links > 114 && Get32(bytes, links) == 1, "the links found");
    Expect(bytes, size, links, 0, mvfx_errorInvalid, 114, "no plexus links");
    def.emitter.plexus.maxLinks = 0;
    int64_t written = 0;
    CHECK(Refused(mvfxWriteEffect(&def.effect, s_assets, 4, bytes, 4096, &written),
                  mvfx_errorInvalid, -1),
          "the writer refuses it too");
}

// Bytes after the def in its unit, and a def the validator refuses.
static void TestTrailing(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    Set32(bytes, size, 0);
    Set32(bytes, 8, (uint32_t)size + 4);
    Set32(bytes, 24 + 48 + 8, Get32(bytes, 24 + 48 + 8) + 4);
    Rehash(bytes, size + 4);
    mvfxEffectDef read;
    CHECK(Refused(Read(bytes, size + 4, &read), mvfx_errorInvalid, size), "bytes after the def");
    // The capacity found by the difference, and set to 0.
    uint8_t other[4096];
    size = Write(&def, bytes);
    def.emitter.capacity = 17;
    Write(&def, other);
    int64_t capacity = FirstDifference(bytes, other, size);
    CHECK(capacity > 114 && Get32(bytes, capacity) == 16, "the capacity found");
    Expect(bytes, size, capacity, 0, mvfx_errorInvalid, 114, "a def the validator refuses");
}

// The memory a def needs: measured, a byte short refused, read in exactly.
static void TestMemory(void)
{
    Def def;
    MakeDef(&def);
    uint8_t bytes[4096];
    int64_t size = Write(&def, bytes);
    int64_t needed = 0;
    mvfxEffectDef read;
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets, 4, NULL, 0, &needed, &read),
                  mvfx_errorCapacity, -1),
          "measured");
    CHECK(needed >=
              (int64_t)(sizeof(mvfxEmitterDef) + 2 * sizeof(mvfxCollider) + sizeof(mvfxHostModule)),
          "the arrays' bytes");
    int64_t again = 0;
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets, 4, s_memory, needed - 1, &again, &read),
                  mvfx_errorCapacity, -1) &&
              again == needed,
          "a byte short");
    memset(s_memory, 0xAB, sizeof s_memory);
    CHECK(Refused(mvfxReadEffect(bytes, size, s_assets, 4, s_memory, needed, &again, &read),
                  mvfx_success, -1) &&
              again == needed,
          "read in exactly");
    CHECK((const uint8_t*)read.emitters == s_memory && s_memory[needed] == 0xAB,
          "the memory from its start, not past its end");
    uint8_t written[4096];
    int64_t rewritten = 0;
    CHECK(
        Refused(mvfxWriteEffect(&read, s_assets, 4, written, 4096, &rewritten), mvfx_success, -1) &&
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
    CHECK(mvfxReadEffect(bytes, size, s_assets, 4, NULL, 0, &needed, &read).result ==
              mvfx_errorCapacity,
          "measured");
    memset(s_memory, 0xAB, sizeof s_memory);
    mvfxCodec codec = {.in = bytes,
                       .at = 88,
                       .end = (size_t)size,
                       .memory = s_memory,
                       .capacity = (size_t)needed - 1,
                       .faultAt = -1};
    mvfxEffectDef def2 = {0};
    mvfxCodeStruct(&codec, mvfxFileEffectDef, (uint8_t*)&def2);
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
    CHECK(mvfxWriteEffect(&def.effect, table, 4, bytes, 4096, &size).result == mvfx_success,
          "written");
    int32_t live = s_live;
    mvfxEffectId effect = {0};
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect), mvfx_success,
                  -1) &&
              mvfxEffect_IsValid(context, effect),
          "created from bytes");
    CHECK(s_live == live + 1, "the effect's block alone kept");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "destroyed");
    s_failAllocations = true;
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect),
                  mvfx_errorCapacity, -1) &&
              effect.index1 == 0,
          "the allocator failing");
    s_failAllocations = false;
    bytes[size - 1] ^= 1;
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect),
                  mvfx_errorInvalid, 16),
          "a file refused at its offset");
    bytes[size - 1] ^= 1;
    CHECK(mvfxDestroyGrid(context, table[0].grid) == mvfx_success, "grid destroyed");
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, &effect),
                  mvfx_errorStale, -1),
          "a grid destroyed, refused by the effect");
    CHECK(s_live == live - 1, "the read memory freed");
    uint64_t misuse = mvfxGetContextMisuse(context);
    CHECK(Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, 4, NULL),
                  mvfx_errorInvalid, -1) &&
              mvfxGetContextMisuse(context) == misuse + 1,
          "a NULL out counted as misuse");
    CHECK(Refused(mvfxCreateEffectFromBytes(NULL, bytes, size, table, 4, &effect),
                  mvfx_errorInvalid, -1),
          "a NULL context");
    mvfxDestroyContext(context);
    CHECK(s_live == 0, "everything freed");
}

// A def of one emitter, written with the table and created from its
// bytes: what a file asks of the program's assets that they do not hold is
// refused with mvfx_errorInvalid as untrusted data, counting no misuse.
// The same def passed by the program itself counts one when counted is
// true: a correct program does not pass it.
static bool RefusedFromFile(mvfxContext* context, const mvfxEmitterDef* emitter,
                            const mvfxEffectAsset* table, int32_t count, bool counted)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    static uint8_t bytes[4096];
    int64_t size = 0;
    bool written =
        mvfxWriteEffect(&def, table, count, bytes, sizeof bytes, &size).result == mvfx_success;
    uint64_t misuse = mvfxGetContextMisuse(context);
    mvfxEffectId effect = {1, 1};
    bool file = Refused(mvfxCreateEffectFromBytes(context, bytes, size, table, count, &effect),
                        mvfx_errorInvalid, -1) &&
                effect.index1 == 0 && mvfxGetContextMisuse(context) == misuse;
    bool program = mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid &&
                   mvfxGetContextMisuse(context) == misuse + (counted ? 1 : 0);
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
    emitter.collision.enabled = true;
    emitter.colliders = &heights;
    emitter.colliderCount = 1;
    CHECK(RefusedFromFile(context, &emitter, table, 3, true), "a heightfield of a tall grid");

    emitter = mvfxDefaultEmitterDef();
    const mvfxForce field = {.kind = mvfx_forceVectorField,
                             .strength = 1.0f,
                             .grid = table[0].grid,
                             .boxSize = {1, 1, 1},
                             .boxRotation = {0, 0, 0, 1}};
    emitter.forces = &field;
    emitter.forceCount = 1;
    CHECK(RefusedFromFile(context, &emitter, table, 3, true),
          "a vector field over a grid of one component");

    emitter = mvfxDefaultEmitterDef();
    emitter.shape.kind = mvfx_shapeMesh;
    emitter.shape.mesh = table[1].mesh;
    emitter.shape.meshMode = mvfx_meshSurface;
    CHECK(RefusedFromFile(context, &emitter, table, 3, true), "a surface of a mesh without one");

    emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.rigid = (mvfxRigidDef){.enabled = true,
                                   .shape = mvfx_rigidHull,
                                   .density = 1.0f,
                                   .substeps = 8,
                                   .staticFriction = 0.6f,
                                   .dynamicFriction = 0.4f,
                                   .hull = table[2].mesh};
    CHECK(RefusedFromFile(context, &emitter, table, 3, false),
          "a hull of a flat mesh, misuse from neither");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestArguments();
    TestAssetTables();
    TestLayout();
    TestMissing();
    TestHeaderAndTable();
    TestNames();
    TestEffectUnit();
    TestTrailing();
    TestCaps();
    TestDecoys();
    TestCuts();
    TestParts();
    TestMemory();
    TestMemoryGuard();
    TestCreate();
    TestUntrustedAssets();
    return s_failures == 0 ? 0 : 1;
}
