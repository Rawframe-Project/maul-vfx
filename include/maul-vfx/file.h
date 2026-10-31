// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect files: an effect def as bytes to keep and ship, written and read
// field by field, every integer and every float's bits little-endian
// whatever the host's order, checked by a hash of 64-bit words read
// little-endian, and refused with the offset of the first byte at fault. A file holds exactly a
// valid def as tagged fields, each field of each struct that differs from its default (a field
// left out reads as its default), its arrays counted, and the names of the host's objects it
// refers to (its grids, its meshes, its host modules), which the host binds through a table of
// assets when it writes and when it reads. Equal defs with equal assets give equal bytes.

#ifndef MAUL_VFX_FILE_H
#define MAUL_VFX_FILE_H

#include "maul-vfx/effect.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The format version a file is written in, the one a reader takes.
#define MVFX_FILE_VERSION 4

// The longest name of an asset, in bytes.
#define MVFX_MAX_ASSET_NAME 4096

    // What an asset is.
    typedef int32_t mvfxAssetKind;
    enum
    {
        // A grid of the context: forces' and colliders' grids.
        mvfx_assetGrid = 0,
        // A mesh of the context: shapes' meshes and rigid bodies' hulls.
        mvfx_assetMesh = 1,
        // A host module: its function and user pointer.
        mvfx_assetModule = 2,
    };

    // A host object a file names: its kind, its name, and the object of
    // that kind (the others ignored).
    typedef struct mvfxEffectAsset
    {
        mvfxAssetKind kind;
        // UTF-8, not terminated, from 1 to MVFX_MAX_ASSET_NAME bytes, each
        // name once a kind in a table.
        const char* name;
        int32_t nameLength;
        mvfxGridId grid;
        mvfxMeshId mesh;
        mvfxModuleFunction* function;
        void* user;
    } mvfxEffectAsset;

    // What a file call did: its status, and the offset of the byte at
    // fault in the file, -1 when the fault lies in no byte.
    typedef struct mvfxFileResult
    {
        mvfxResult result;
        int64_t offset;
    } mvfxFileResult;

    /// Writes an effect def as a file. Each grid, mesh and host module the
    /// def refers to is named by the first asset of the table that is it
    /// (a module by its function and user pointer both), in the order the
    /// def first refers to them.
    ///
    /// @param def         The def, from mvfxDefaultEffectDef.
    /// @param assets      The table of assets; NULL when assetCount is 0.
    /// @param assetCount  Its length, at least 0.
    /// @param bytesOut    Receives the file; NULL when capacity is 0.
    /// @param capacity    Its size in bytes, at least 0.
    /// @param sizeOut     Receives the file's size in bytes, also when the
    ///                    capacity is too small; 0 on another failure.
    /// @param refusalOut  Optional: receives why the call failed and the
    ///                    field refused (mvfxEffectRefusal), a def's field
    ///                    the validator refuses or the field that refers to
    ///                    an object the table lacks; on success,
    ///                    mvfx_success and no field.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def the def validator refuses (mvfxCreateEffect's checks
    ///         that need no context) or an invalid asset table (a kind past
    ///         the known ones, a name empty, not UTF-8 or twice a kind);
    ///         `mvfx_errorLimit` for a def past a named bound, as
    ///         mvfxCreateEffect answers, or a name longer than
    ///         MVFX_MAX_ASSET_NAME; `mvfx_errorMissing` for an object the
    ///         def refers to that the table lacks; `mvfx_errorCapacity` when
    ///         the file does not fit in bytes, or is larger than INT32_MAX
    ///         bytes.
    ///         The offset is -1.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_NODISCARD MVFX_API mvfxFileResult mvfxWriteEffect(const mvfxEffectDef* def,
                                                           const mvfxEffectAsset* assets,
                                                           int32_t assetCount, void* bytesOut,
                                                           int64_t capacity, int64_t* sizeOut,
                                                           mvfxEffectRefusal* refusalOut);

    /// Reads a file into an effect def, its arrays in caller memory. Checks
    /// in order the header (its magic, its version, its size), the hash,
    /// the section table, each names section alone, the effect's fields,
    /// the bindings of the names it holds through the asset table, and the
    /// def validator. A field the file leaves out takes its default. The bytes must not
    /// change during the call: bytes that do are refused or read as some
    /// valid def, but never read or laid past.
    ///
    /// @param bytes       The file.
    /// @param size        Its size in bytes, at least 0.
    /// @param assets      The table of assets; NULL when assetCount is 0.
    /// @param assetCount  Its length, at least 0.
    /// @param memoryOut   Receives the def's arrays, aligned to 16 bytes;
    ///                    NULL when capacity is 0. The def points into it.
    /// @param capacity    Its size in bytes, at least 0.
    /// @param neededOut   Receives the bytes of memory the def needs, also
    ///                    when the capacity is too small; 0 on another
    ///                    failure. The size is known before the def
    ///                    validator runs, which needs the def in memory, so
    ///                    a file measured may still be refused by the read
    ///                    that follows: check its result.
    /// @param defOut      Receives the def, with its cookies; zeroed on
    ///                    failure.
    /// @param refusalOut  Optional: receives why the call failed and the
    ///                    field at fault (mvfxEffectRefusal): a field of the
    ///                    effect section the reader refuses, or a def's
    ///                    field the validator refuses; on success,
    ///                    mvfx_success and no field.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument,
    ///         memory not aligned or an invalid asset table; `mvfx_errorLimit`
    ///         for a name in the table longer than MVFX_MAX_ASSET_NAME;
    ///         `mvfx_errorFormat` for a file that is not one (a wrong
    ///         magic, size or hash, a section out of place, a tag out of
    ///         order or twice, a field of the wrong length, padding not
    ///         zero, a count past its cap, a bool neither 0 nor 1, a name
    ///         not UTF-8 or twice a kind, a reference past its names or
    ///         out of their order) or holds a def the def validator
    ///         refuses; `mvfx_errorVersion` for another format version or
    ///         a section or field tag this version does not know, which
    ///         only a later one writes; `mvfx_errorMissing` for a name the
    ///         table lacks; `mvfx_errorCapacity` when the def does not fit
    ///         in memory. The offset is the first byte at fault: the
    ///         version's, a tag's, a name's, a count's; for a def refused,
    ///         the tag of the field refused, or, for a field the file
    ///         leaves out, of the nearest struct or item holding it that
    ///         the file has (the effect section's when it has none); -1
    ///         for a fault outside the file.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_NODISCARD MVFX_API mvfxFileResult mvfxReadEffect(const void* bytes, int64_t size,
                                                          const mvfxEffectAsset* assets,
                                                          int32_t assetCount, void* memoryOut,
                                                          int64_t capacity, int64_t* neededOut,
                                                          mvfxEffectDef* defOut,
                                                          mvfxEffectRefusal* refusalOut);

    /// Creates an effect from a file: reads it into memory from the
    /// context's allocator, creates the effect from the def
    /// (mvfxCreateEffect) and frees the memory.
    ///
    /// @param context     The context that owns the effect.
    /// @param bytes       The file.
    /// @param size        Its size in bytes, at least 0.
    /// @param assets      The table of assets; NULL when assetCount is 0.
    /// @param assetCount  Its length, at least 0.
    /// @param effectOut   Receives the effect's id; the null id on failure.
    /// @param refusalOut  Optional: as mvfxReadEffect's, then as
    ///                    mvfxCreateEffect's.
    /// @return As mvfxReadEffect, then as mvfxCreateEffect, with the offset
    ///         of the field refused found as mvfxReadEffect finds it (a
    ///         mesh that names no live mesh: the tag of its field), -1 for
    ///         a refusal of no field; `mvfx_errorCapacity` also when the
    ///         allocator fails. An
    ///         invalid argument (a NULL pointer, NULL bytes with a size, a
    ///         negative size, an invalid asset table or a name in it longer
    ///         than MVFX_MAX_ASSET_NAME) counts as misuse when the context is
    ///         given. What the file holds is untrusted data:
    ///         a file refused for its contents, including for what it asks
    ///         of the table's assets, counts none.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxFileResult mvfxCreateEffectFromBytes(
        mvfxContext* context, const void* bytes, int64_t size, const mvfxEffectAsset* assets,
        int32_t assetCount, mvfxEffectId* effectOut, mvfxEffectRefusal* refusalOut);

#ifdef __cplusplus
}
#endif

#endif
