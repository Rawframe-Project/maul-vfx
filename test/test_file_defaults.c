// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The defaults an effect file leaves out, as tools/file-tags.txt pins
// them: every field that is not a struct, at each place a def's default
// or an array item's holds it, is written by the library's own field
// writer from the default functions' values and from the block of
// defaults the reader lays absent fields from, and compared with the
// bytes the registry pins (test/file_pins.h, which tools/gen_file.py
// writes from it). A default that changes changes what every file that
// leaves its field out means, so its field takes a new tag: this suite
// fails until the registry says so.

#include "file_codec.h"
#include "file_fields.h"
#include "file_pins.h"
#include "test_harness.h"

#include <stdio.h>
#include <string.h>

// The field of a table of a tag, NULL for none.
static const mvfxFileField* FieldOfTag(const mvfxFileType* type, uint32_t tag)
{
    for (int32_t i = 0; i < type->fieldCount; ++i)
    {
        if (type->fields[i].tag == tag)
        {
            return &type->fields[i];
        }
    }
    return NULL;
}

// A root's table, reached from mvfxEffectDef's by its tags; NULL when a
// tag misses.
static const mvfxFileType* RootType(const FilePinRoot* root)
{
    const mvfxFileType* type = mvfxFileEffectDef;
    for (int32_t i = 0; type != NULL && i < root->tagCount; ++i)
    {
        const mvfxFileField* field = FieldOfTag(type, root->tags[i]);
        type = field != NULL ? field->type : NULL;
    }
    return type;
}

// The bytes the writer puts for a pinned field's payload in a root's
// default at object, into out; their count, or -1 when a step misses or
// the writer refuses.
static int64_t Encode(const FilePin* pin, const mvfxFileType* type, const uint8_t* object,
                      uint8_t* out, size_t capacity)
{
    const mvfxFileField* field = NULL;
    for (int32_t s = 0; s < pin->stepCount; ++s)
    {
        field = FieldOfTag(type, pin->steps[s].tag);
        if (field == NULL)
        {
            return -1;
        }
        if (s + 1 < pin->stepCount)
        {
            int32_t item = pin->steps[s].item;
            object += field->offset + (item >= 0 ? (size_t)item * field->type->size : 0);
            type = field->type;
        }
    }
    if (field == NULL)
    {
        return -1;
    }
    mvfxCodec codec = {.out = out, .end = capacity, .defaults = mvfxFileDefaults, .faultAt = -1};
    mvfxPutPayload(&codec, field, object, object);
    return codec.fault == mvfx_success ? (int64_t)codec.at : -1;
}

static void PrintHex(const uint8_t* bytes, int64_t count)
{
    for (int64_t i = 0; i < count; ++i)
    {
        printf("%02x", bytes[i]);
    }
}

// Whether the bytes are the pin's; when not, says which tag to retire.
static bool AsPinned(const FilePin* pin, const uint8_t* bytes, int64_t count, const char* from)
{
    if (count == pin->byteCount && memcmp(bytes, pin->bytes, (size_t)count) == 0)
    {
        return true;
    }
    printf("tools/file-tags.txt: %s's default at %s from %s is ", pin->field, pin->place, from);
    PrintHex(bytes, count);
    printf(", pinned ");
    PrintHex((const uint8_t*)pin->bytes, pin->byteCount);
    printf(": a changed default gets a new tag, so retire tag %u and give the field a new one\n",
           (unsigned)pin->tag);
    return false;
}

// A root's default, laid by its default function or as zero bytes.
static _Alignas(16) uint8_t s_root[8192];

static void TestPins(void)
{
    int32_t count = (int32_t)(sizeof s_filePins / sizeof s_filePins[0]);
    int32_t same = 0;
    for (int32_t p = 0; p < count; ++p)
    {
        const FilePin* pin = &s_filePins[p];
        const FilePinRoot* root = &s_filePinRoots[pin->root];
        const mvfxFileType* type = RootType(root);
        CHECK(type != NULL && root->size <= sizeof s_root &&
                  type->defaultAt != MVFX_FILE_NO_DEFAULT,
              "the root's table and its default's place");
        if (type == NULL || root->size > sizeof s_root)
        {
            continue;
        }
        root->fill(s_root);
        uint8_t bytes[256];
        int64_t made = Encode(pin, type, s_root, bytes, sizeof bytes);
        CHECK(made >= 0, "the pinned field written");
        bool function = made >= 0 && AsPinned(pin, bytes, made, root->name);
        // The block the reader lays an absent field from holds the same.
        int64_t laid = Encode(pin, type, mvfxFileDefaults + type->defaultAt, bytes, sizeof bytes);
        bool block = laid >= 0 && AsPinned(pin, bytes, laid, "the block of defaults");
        CHECK(function && block, "a default as the registry pins it");
        same += function && block ? 1 : 0;
    }
    CHECK(count > 0 && same == count, "every pinned default");
}

int main(void)
{
    TestPins();
    return s_failures == 0 ? 0 : 1;
}
