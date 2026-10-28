// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect files whatever a def's padding holds: each effect of the golden
// scene written from copies of its def whose structs and arrays were
// filled with a byte before their fields were set, into buffers filled
// likewise, gives the same bytes as the def itself. A suite of its own, as
// the scene's frame and the file test's together pass WebAssembly's stack.

#include "file_fields.h"
#include "test_harness.h"
#include "vfx_scene.h"

#include "maul-vfx/file.h"

#include <stdlib.h>
#include <string.h>

// The bytes of a scalar field of each kind, as the file's table names
// them.
static const size_t s_scalarBytes[mvfx_fieldF64 + 1] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8};

// Storage for the copies of a def, each struct and array filled with a
// byte before its fields are set.
static _Alignas(16) uint8_t s_patterned[1 << 20];
static size_t s_patternedUsed;

static uint8_t* Patterned(size_t bytes, size_t alignment, uint8_t pattern)
{
    size_t at = (s_patternedUsed + alignment - 1) & ~(alignment - 1);
    if (at + bytes > sizeof s_patterned)
    {
        CHECK(false, "room for a patterned def");
        return NULL;
    }
    s_patternedUsed = at + bytes;
    memset(s_patterned + at, pattern, bytes);
    return s_patterned + at;
}

// Sets the fields of a struct of a table, one by one from another, its
// arrays copied likewise into patterned storage: the bytes between and
// after the fields keep the pattern.
static void SetFields(const mvfxFileType* type, const uint8_t* from, uint8_t* to, uint8_t pattern)
{
    for (int32_t i = 0; i < type->fieldCount; ++i)
    {
        const mvfxFileField* f = &type->fields[i];
        const uint8_t* source = from + f->offset;
        uint8_t* target = to + f->offset;
        switch (f->kind)
        {
        case mvfx_fieldStruct:
            for (uint32_t k = 0; k < f->count; ++k)
            {
                SetFields(f->type, source + (size_t)k * f->type->size,
                          target + (size_t)k * f->type->size, pattern);
            }
            break;
        case mvfx_fieldArray:
        {
            int32_t count = 0;
            memcpy(&count, from + f->at, sizeof count);
            memcpy(to + f->at, &count, sizeof count);
            const uint8_t* items = NULL;
            memcpy((void*)&items, source, sizeof items);
            bool scalar = f->element != mvfx_fieldStruct;
            size_t size = scalar ? s_scalarBytes[f->element] : f->type->size;
            size_t n = count > 0 ? (size_t)count * f->count : 0;
            uint8_t* copy = NULL;
            if (items != NULL && n > 0)
            {
                copy = Patterned(n * size, scalar ? size : f->type->alignment, pattern);
                for (size_t k = 0; copy != NULL && k < n; ++k)
                {
                    if (scalar)
                    {
                        memcpy(copy + k * size, items + k * size, size);
                    }
                    else
                    {
                        SetFields(f->type, items + k * size, copy + k * size, pattern);
                    }
                }
            }
            memcpy(target, (const void*)&copy, sizeof copy);
            break;
        }
        case mvfx_fieldGrid:
        case mvfx_fieldMesh:
            memcpy(target, source, f->count * sizeof(mvfxGridId));
            break;
        case mvfx_fieldModule:
            memcpy(target, source, sizeof(mvfxModuleFn*));
            memcpy(to + f->at, from + f->at, sizeof(void*));
            break;
        case mvfx_fieldCookie:
            memcpy(target, source, sizeof(uint32_t));
            break;
        default:
            memcpy(target, source, s_scalarBytes[f->kind] * f->count);
            break;
        }
    }
}

// Writes a def from a copy of it in storage filled with a pattern, into a
// buffer filled with the pattern too; its size in size.
static uint8_t* WrittenPatterned(const mvfxEffectDef* def, uint8_t pattern, int64_t* size)
{
    s_patternedUsed = 0;
    uint8_t* copy = Patterned(sizeof *def, alignof(mvfxEffectDef), pattern);
    if (copy == NULL)
    {
        return NULL;
    }
    SetFields(mvfxFileEffectDef, (const uint8_t*)def, copy, pattern);
    mvfxFileResult measured = mvfxWriteEffect((const mvfxEffectDef*)copy, s_sceneAssets,
                                              s_sceneAssetCount, NULL, 0, size);
    uint8_t* bytes = malloc((size_t)*size);
    CHECK(measured.result == mvfx_errorCapacity && bytes != NULL, "a patterned def measured");
    if (bytes == NULL)
    {
        return NULL;
    }
    memset(bytes, pattern, (size_t)*size);
    int64_t written = 0;
    mvfxFileResult result = mvfxWriteEffect((const mvfxEffectDef*)copy, s_sceneAssets,
                                            s_sceneAssetCount, bytes, *size, &written);
    CHECK(result.result == mvfx_success && written == *size, "a patterned def written");
    return bytes;
}

// The scene's defs each written from two copies, their structs and
// arrays filled with one byte and then another before their fields are
// set, into buffers filled likewise: the bytes are the same, the def's
// own included, so neither padding nor a buffer's old bytes reach a file.
static int32_t s_patternedDefs;

static mvfxResult CreatePatterned(mvfxContext* context, const mvfxEffectDef* def,
                                  mvfxEffectId* effectOut)
{
    int64_t size[3] = {0, 0, 0};
    uint8_t* bytes[3] = {WrittenPatterned(def, 0x00, &size[0]),
                         WrittenPatterned(def, 0xA5, &size[1]),
                         WrittenPatterned(def, 0xFF, &size[2])};
    CHECK(bytes[0] != NULL && bytes[1] != NULL && bytes[2] != NULL && size[0] == size[1] &&
              size[0] == size[2] && memcmp(bytes[0], bytes[1], (size_t)size[0]) == 0 &&
              memcmp(bytes[0], bytes[2], (size_t)size[0]) == 0,
          "the same bytes whatever the padding holds");
    int64_t own = 0;
    uint8_t* direct = NULL;
    if (mvfxWriteEffect(def, s_sceneAssets, s_sceneAssetCount, NULL, 0, &own).result ==
            mvfx_errorCapacity &&
        own == size[0])
    {
        direct = malloc((size_t)own);
    }
    CHECK(direct != NULL &&
              mvfxWriteEffect(def, s_sceneAssets, s_sceneAssetCount, direct, own, &own).result ==
                  mvfx_success &&
              bytes[0] != NULL && memcmp(direct, bytes[0], (size_t)own) == 0,
          "and the def's own");
    free(direct);
    for (int32_t i = 0; i < 3; ++i)
    {
        free(bytes[i]);
    }
    ++s_patternedDefs;
    return mvfxCreateEffect(context, def, effectOut);
}

static void TestPadding(void)
{
    mvfxContext* context = NULL;
    mvfxContextDef def = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    s_sceneCreate = CreatePatterned;
    BuildScene(context);
    s_sceneCreate = mvfxCreateEffect;
    CHECK(s_patternedDefs == 8, "every effect of the scene");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestPadding();
    return s_failures == 0 ? 0 : 1;
}
