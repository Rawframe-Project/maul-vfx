// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Output records: each live particle's values for a layout's fields, in
// the world (relative to a view's origin) and with values over life.

#ifndef MAUL_VFX_SRC_OUTPUT_H
#define MAUL_VFX_SRC_OUTPUT_H

#include "instance.h"
#include "layout.h"
#include "space.h"

#include <stdint.h>

// What a record reads of a particle: the emitter, the map from its
// particles' space into the world, and the view's origin.
typedef struct mvfxRecordSource
{
    const mvfxInstance* instance;
    int32_t emitter;
    const mvfxAttributePlan* plan;
    mvfxSpaceMap map;
    const double* viewOrigin;
} mvfxRecordSource;

// A ribbon point's own values: its unit tangent in the world, its
// normalized length, its texture coordinate along, its width's factor,
// its colour's factor and its ribbon's index in the list.
typedef struct mvfxRibbonPoint
{
    float tangent[3];
    float side[3];
    float faceNormal[3];
    float length;
    float along;
    float width;
    float color[4];
    uint32_t ribbon;
} mvfxRibbonPoint;

// The source of an emitter's records for a view's origin.
mvfxRecordSource mvfxOpenRecordSource(const mvfxInstance* instance, int32_t emitter,
                                      const double viewOrigin[3]);

// A particle's place in the world.
void mvfxWorldPlaceOf(const mvfxRecordSource* source, int32_t particle, double out[3]);

// Writes a particle's record; a ribbon point's where point is given.
void mvfxWriteRecord(const mvfxRecordSource* source, const mvfxOutputLayout* layout,
                     int32_t particle, const mvfxRibbonPoint* point, unsigned char* record);

// Writes a ribbon point's record between particles first and second (the
// same for a point on a particle), a fraction of the way: its values
// interpolated (an orientation along the shorter way, counts the nearer
// particle's), its position the given place in the world.
void mvfxWriteRibbonRecord(const mvfxRecordSource* source, const mvfxOutputLayout* layout,
                           int32_t first, int32_t second, float fraction, const double place[3],
                           const mvfxRibbonPoint* point, unsigned char* record);

// Whether a view is within its bounds.
bool mvfxIsViewValid(const mvfxView* view);

// Whether a host buffer of bytes is NULL only when empty, and aligned.
bool mvfxIsBufferValid(const void* buffer, int64_t bytes, uintptr_t alignment);

// The order records follow: indices into the pool, or the pool's own
// order of live particles, or its reverse.
typedef struct mvfxRecordOrder
{
    const uint32_t* indices;
    bool reverse;
    int32_t live;
} mvfxRecordOrder;

// Writes the records of the first count particles of an order.
void mvfxWriteRecords(const mvfxInstance* instance, int32_t emitter, const mvfxOutputLayout* layout,
                      const double viewOrigin[3], const mvfxRecordOrder* order,
                      unsigned char* records, int64_t count);

#endif
