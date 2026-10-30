// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Refused fields recorded with their paths, and their paths written as
// text by the tables' names.

#include "field_path.h"

#include "file_fields.h"
#include "invariant.h"
#include "refusal.h"

#include <string.h>

void mvfxClearRefusal(mvfxEffectRefusal* refusal)
{
    *refusal = (mvfxEffectRefusal){.result = mvfx_success, .emitter = -1, .gas = -1, .water = -1};
}

mvfxFieldPath mvfxFieldRoot(mvfxEffectRefusal* refusal)
{
    return (mvfxFieldPath){.refusal = refusal};
}

mvfxFieldPath mvfxFieldIn(const mvfxFieldPath* at, uint32_t tag, int32_t item)
{
    MVFX_ASSERT(at->depth < MVFX_MAX_FIELD_DEPTH);
    mvfxFieldPath in = *at;
    in.tags[in.depth] = tag;
    in.items[in.depth] = item;
    in.depth += 1;
    return in;
}

mvfxResult mvfxRefuseItem(const mvfxFieldPath* at, uint32_t tag, int32_t item, mvfxResult status)
{
    mvfxEffectRefusal* refusal = at->refusal;
    if (status == mvfx_success || refusal == nullptr || refusal->result != mvfx_success)
    {
        return status;
    }
    mvfxFieldPath field = tag != 0 ? mvfxFieldIn(at, tag, item) : *at;
    mvfxClearRefusal(refusal);
    refusal->result = status;
    refusal->tagCount = field.depth;
    memcpy(refusal->tags, field.tags, (size_t)field.depth * sizeof(uint32_t));
    memcpy(refusal->items, field.items, (size_t)field.depth * sizeof(int32_t));
    if (field.depth > 0)
    {
        int32_t first = field.items[0];
        uint32_t root = field.tags[0];
        refusal->emitter = root == MVFX_TAG_EFFECT_DEF_EMITTERS ? first : -1;
        refusal->gas = root == MVFX_TAG_EFFECT_DEF_GASES ? first : -1;
        refusal->water = root == MVFX_TAG_EFFECT_DEF_WATERS ? first : -1;
    }
    return status;
}

mvfxResult mvfxRefuseVector(const mvfxFieldPath* at, uint32_t tag, const float v[3], float bound)
{
    for (int32_t c = 0; c < 3; ++c)
    {
        mvfxResult component = mvfxBounded(v[c], -bound, bound);
        if (component != mvfx_success)
        {
            return mvfxRefuseItem(at, tag, c, component);
        }
    }
    return mvfx_success;
}

mvfxResult mvfxRefuseSizes(const mvfxFieldPath* at, uint32_t tag, const float v[3], float bound)
{
    for (int32_t c = 0; c < 3; ++c)
    {
        mvfxResult component = mvfxPositiveUpTo(v[c], bound);
        if (component != mvfx_success)
        {
            return mvfxRefuseItem(at, tag, c, component);
        }
    }
    return mvfx_success;
}

// The field of a table of a tag, NULL for none; tag 0 (a cookie's) is no
// field a path names.
static const mvfxFileField* FieldOfTag(const mvfxFileType* type, uint32_t tag)
{
    for (int32_t i = 0; tag != 0 && i < type->fieldCount; ++i)
    {
        if (type->fields[i].tag == tag)
        {
            return &type->fields[i];
        }
    }
    return nullptr;
}

// The items a field holds that a path may name, 0 for none: an array's
// cap, a fixed array's count.
static int64_t ItemsOf(const mvfxFileField* field)
{
    if (field->kind == mvfx_fieldArray)
    {
        return field->cap;
    }
    return field->count > 1 ? field->count : 0;
}

// Appends bytes to the text while they fit; counts them all.
static void Put(char* text, int64_t capacity, int64_t* length, const char* bytes, size_t count)
{
    if (text != nullptr && *length + (int64_t)count <= capacity)
    {
        memcpy(text + *length, bytes, count);
    }
    *length += (int64_t)count;
}

// Appends an item as [its index] while it fits; counts it all.
static void PutItem(char* text, int64_t capacity, int64_t* length, int32_t item)
{
    char digits[16];
    int32_t count = 0;
    for (int32_t value = item; count == 0 || value > 0; value /= 10)
    {
        digits[count++] = (char)('0' + value % 10);
    }
    Put(text, capacity, length, "[", 1);
    for (int32_t d = count - 1; d >= 0; --d)
    {
        Put(text, capacity, length, &digits[d], 1);
    }
    Put(text, capacity, length, "]", 1);
}

// Writes a path's text into text while it fits, its length into length;
// false for a path that names no field.
static bool WritePath(const mvfxEffectRefusal* refusal, char* text, int64_t capacity,
                      int64_t* length)
{
    if (refusal->tagCount < 0 || refusal->tagCount > MVFX_MAX_FIELD_DEPTH)
    {
        return false;
    }
    const mvfxFileType* type = mvfxFileEffectDef;
    for (int32_t s = 0; s < refusal->tagCount; ++s)
    {
        const mvfxFileField* field = type != nullptr ? FieldOfTag(type, refusal->tags[s]) : nullptr;
        if (field == nullptr)
        {
            return false;
        }
        int32_t item = refusal->items[s];
        int64_t items = ItemsOf(field);
        bool last = s + 1 == refusal->tagCount;
        // A step into a struct of an array names its item; the last step
        // may name a whole array.
        if (item < -1 || item >= items || (item == -1 && items > 0 && !last))
        {
            return false;
        }
        s > 0 ? Put(text, capacity, length, ".", 1) : (void)0;
        Put(text, capacity, length, field->name, strlen(field->name));
        item >= 0 ? PutItem(text, capacity, length, item) : (void)0;
        bool nested = field->kind == mvfx_fieldStruct ||
                      (field->kind == mvfx_fieldArray && field->element == mvfx_fieldStruct);
        type = nested ? field->type : nullptr;
    }
    return true;
}

mvfxResult mvfxGetEffectFieldPath(const mvfxEffectRefusal* refusal, char* textOut, int64_t capacity,
                                  int64_t* lengthOut)
{
    if (lengthOut != nullptr)
    {
        *lengthOut = 0;
    }
    if (refusal == nullptr || lengthOut == nullptr || capacity < 0 ||
        (textOut == nullptr && capacity > 0))
    {
        mvfxClearSpan(textOut, capacity, 1, 1);
        return mvfx_errorInvalid;
    }
    int64_t length = 0;
    if (!WritePath(refusal, textOut, capacity, &length))
    {
        mvfxClearSpan(textOut, capacity, 1, 1);
        return mvfx_errorInvalid;
    }
    if (length > capacity)
    {
        mvfxClearSpan(textOut, capacity, 1, 1);
        *lengthOut = length;
        return mvfx_errorCapacity;
    }
    *lengthOut = length;
    return mvfx_success;
}
