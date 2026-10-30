// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The effect file's tables of the def's structs: for each, its fields in
// ascending tag order, and the defaults an absent field takes, as
// tools/gen_file.py writes them into file_fields.c from the public headers
// and tools/file-tags.txt.

#ifndef MAUL_VFX_SRC_FILE_FIELDS_H
#define MAUL_VFX_SRC_FILE_FIELDS_H

#include <stdint.h>

// What a field is to the file.
typedef uint8_t mvfxFieldKind;

enum
{
    mvfx_fieldBool = 0,
    mvfx_fieldU8 = 1,
    mvfx_fieldI8 = 2,
    mvfx_fieldU16 = 3,
    mvfx_fieldI16 = 4,
    mvfx_fieldU32 = 5,
    mvfx_fieldI32 = 6,
    mvfx_fieldU64 = 7,
    mvfx_fieldI64 = 8,
    mvfx_fieldF32 = 9,
    mvfx_fieldF64 = 10,
    // A nested struct, by its table.
    mvfx_fieldStruct = 11,
    // A pointer and its count: its elements of a kind (a struct's by its
    // table), its count's place, the cap on the count and the elements a
    // counted item holds.
    mvfx_fieldArray = 12,
    // A context id the file names: a grid's, a mesh's.
    mvfx_fieldGrid = 13,
    mvfx_fieldMesh = 14,
    // A host module's function, its user pointer at another place, named.
    mvfx_fieldModule = 15,
    // A def's cookie, its value at at: tag 0, not stored, its default
    // gives it.
    mvfx_fieldCookie = 16,
};

typedef struct mvfxFileType mvfxFileType;

// A field: its tag, its place in its struct, its kind (an array's
// elements' too), its elements (a fixed array's count, 1 for one; an
// array's elements a counted item holds), a struct's table, at and cap:
// an array's count's place and the cap on it, a module's user pointer's
// place, a cookie's value; and its name in the headers.
typedef struct mvfxFileField
{
    uint32_t tag;
    uint32_t offset;
    mvfxFieldKind kind;
    mvfxFieldKind element;
    uint32_t count;
    const mvfxFileType* type;
    uint32_t at;
    uint32_t cap;
    const char* name;
} mvfxFileField;

// A struct: its size and alignment, its fields, and the place of its
// default in the block of defaults (MVFX_FILE_NO_DEFAULT for a struct
// only ever held whole by another, whose default holds its own).
struct mvfxFileType
{
    uint32_t size;
    uint32_t alignment;
    const mvfxFileField* fields;
    int32_t fieldCount;
    uint32_t defaultAt;
};

#define MVFX_FILE_NO_DEFAULT UINT32_MAX

// A struct T's table, its fields in s_fields_T.
#define MVFX_FILE_TYPE(T, count, defaultAt) {sizeof(T), alignof(T), s_fields_##T, count, defaultAt}

// The block of defaults, constant: the root's default and one of each
// array's items', the defs' own as mvfxDefault*Def gives them (by the
// initializers of defaults.h), zero bytes for other structs. A type's
// default is at its defaultAt.
extern const uint8_t* const mvfxFileDefaults;

// The most references of each kind a def holds (its grids, its meshes, its
// host modules), so the most names of each a file holds; file_fields.c
// checks them against the def's arrays and their caps.
#define MVFX_FILE_MAX_GRIDS   1092
#define MVFX_FILE_MAX_MESHES  128
#define MVFX_FILE_MAX_MODULES 512

// The table of mvfxEffectDef, from which every other is reached.
extern const mvfxFileType* const mvfxFileEffectDef;

#endif
