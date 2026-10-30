// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The field a validator refuses (mvfxEffectRefusal): where in an effect
// def a check is, as the path of file tags from mvfxEffectDef down to it,
// and the first refusal of a walk recorded with its field. A check takes
// the path of the struct it checks and names each field it refuses by
// its tag (file_tags.h), so the paths cost nothing until a check refuses.

#ifndef MAUL_VFX_SRC_FIELD_PATH_H
#define MAUL_VFX_SRC_FIELD_PATH_H

#include "file_tags.h"

#include "maul-vfx/effect.h"

// Where a check is: the refusal it records into (NULL to record none),
// and the steps from mvfxEffectDef down to the struct it checks, each a
// field's tag and its item (-1 for none).
typedef struct mvfxFieldPath
{
    mvfxEffectRefusal* refusal;
    int32_t depth;
    uint32_t tags[MVFX_MAX_FIELD_DEPTH];
    int32_t items[MVFX_MAX_FIELD_DEPTH];
} mvfxFieldPath;

// Sets a refusal to none: mvfx_success, no field, -1 for the emitter, gas
// and water.
void mvfxClearRefusal(mvfxEffectRefusal* refusal);

// The path at mvfxEffectDef, recording into refusal; NULL records none,
// for a check outside an effect def.
mvfxFieldPath mvfxFieldRoot(mvfxEffectRefusal* refusal);

// The path one step down: into the field of a tag, at an item of it (-1
// for none).
mvfxFieldPath mvfxFieldIn(const mvfxFieldPath* at, uint32_t tag, int32_t item);

// Records a refusing status at the field of a tag below the path (0 for
// the struct at the path itself) and an item of it (-1 for none), unless
// the walk has recorded one; returns the status, so a check returns it.
mvfxResult mvfxRefuseItem(const mvfxFieldPath* at, uint32_t tag, int32_t item, mvfxResult status);

// mvfxRefuseItem at a field that names no item.
static inline mvfxResult mvfxRefuseField(const mvfxFieldPath* at, uint32_t tag, mvfxResult status)
{
    return status == mvfx_success ? status : mvfxRefuseItem(at, tag, -1, status);
}

// mvfxRefuseItem at the struct at the path: a refusal of no field below.
static inline mvfxResult mvfxRefuseHere(const mvfxFieldPath* at, mvfxResult status)
{
    return status == mvfx_success ? status : mvfxRefuseItem(at, 0, -1, status);
}

// mvfxBoundedVector of a three-component field of a tag below the path,
// naming the component refused as its item.
mvfxResult mvfxRefuseVector(const mvfxFieldPath* at, uint32_t tag, const float v[3], float bound);

// mvfxPositiveUpTo of each component of a three-component field of a tag
// below the path, naming the component refused as its item.
mvfxResult mvfxRefuseSizes(const mvfxFieldPath* at, uint32_t tag, const float v[3], float bound);

#endif
