// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An output layout as the library keeps it: its fields and stride, the
// checks of its def, and the writing of a field's components in its
// format by the library's own integer conversions.

#ifndef MAUL_VFX_SRC_LAYOUT_H
#define MAUL_VFX_SRC_LAYOUT_H

#include "allocator.h"

#include "maul-vfx/output.h"

typedef struct mvfxOutputLayout
{
    mvfxLayoutField fields[MVFX_MAX_LAYOUT_FIELDS];
    int32_t fieldCount;
    uint32_t stride;
} mvfxOutputLayout;

// Whether a layout def is valid.
bool mvfxIsLayoutDefValid(const mvfxLayoutDef* def);

// Builds a layout from a valid def; NULL when the allocator fails.
mvfxOutputLayout* mvfxBuildLayout(const mvfxLayoutDef* def, const mvfxAllocator* allocator);

void mvfxReleaseLayout(mvfxOutputLayout* layout, const mvfxAllocator* allocator);

// Writes a field's components into its record at the field's offset:
// values for the float formats, words for uint32.
void mvfxWriteField(const mvfxLayoutField* field, const float values[4], const uint32_t words[4],
                    unsigned char* record);

#endif
