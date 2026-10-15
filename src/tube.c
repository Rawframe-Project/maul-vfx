// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "tube.h"

#include "elementary.h"

#include <math.h>

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// v less its part along a unit t, normalized; false when nothing is left.
static bool Across(const float v[3], const float t[3], float out[3])
{
    float along = Dot(v, t);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = v[k] - along * t[k];
    }
    float length = sqrtf(Dot(out, out));
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = length > 0.0f ? out[k] / length : 0.0f;
    }
    return length > 0.0f;
}

// The normal across a unit tangent from the axis least along it.
static void FirstNormal(const float t[3], float out[3])
{
    int32_t least = 0;
    for (int32_t k = 1; k < 3; ++k)
    {
        least = fabsf(t[k]) < fabsf(t[least]) ? k : least;
    }
    float axis[3] = {0.0f, 0.0f, 0.0f};
    axis[least] = 1.0f;
    (void)Across(axis, t, out);
}

// Reflects v in the plane across d (of squared length c).
static void Reflect(const float d[3], float c, const float v[3], float out[3])
{
    float share = 2.0f * Dot(d, v) / c;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = v[k] - share * d[k];
    }
}

void mvfxAdvanceTubeFrame(mvfxTubeFrame* frame, const double here[3], const float tangent[3])
{
    if (!frame->started)
    {
        FirstNormal(tangent, frame->normal);
    }
    else
    {
        float d[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            d[k] = (float)(here[k] - frame->place[k]);
        }
        float c1 = Dot(d, d);
        float normal[3] = {frame->normal[0], frame->normal[1], frame->normal[2]};
        float bent[3] = {frame->tangent[0], frame->tangent[1], frame->tangent[2]};
        if (c1 > 0.0f)
        {
            // The first reflection, in the plane between the two places.
            Reflect(d, c1, frame->normal, normal);
            Reflect(d, c1, frame->tangent, bent);
        }
        float e[3] = {tangent[0] - bent[0], tangent[1] - bent[1], tangent[2] - bent[2]};
        float c2 = Dot(e, e);
        float second[3] = {normal[0], normal[1], normal[2]};
        if (c2 > 0.0f)
        {
            // The second, taking the reflected tangent onto the new one.
            Reflect(e, c2, normal, second);
        }
        // Held across the new tangent against rounding; a turn back on
        // itself starts afresh.
        if (!Across(second, tangent, frame->normal))
        {
            FirstNormal(tangent, frame->normal);
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        frame->place[k] = here[k];
        frame->tangent[k] = tangent[k];
    }
    frame->started = true;
}

// The unit way out at angle 2 pi k / sides, and the way around there.
static void WayAt(const mvfxTubeFrame* frame, int32_t sides, int32_t k, float out[3],
                  float around[3])
{
    float sine = 0.0f;
    float cosine = 1.0f;
    mvfxSinCos(6.2831853f * (float)(k % sides) / (float)sides, &sine, &cosine);
    float binormal[3];
    Cross(frame->tangent, frame->normal, binormal);
    for (int32_t j = 0; j < 3; ++j)
    {
        out[j] = cosine * frame->normal[j] + sine * binormal[j];
        around[j] = -sine * frame->normal[j] + cosine * binormal[j];
    }
}

static void WriteVertex(const mvfxTubeSink* sink, const mvfxTubePoint* point, const double place[3],
                        unsigned char* record)
{
    mvfxWriteRibbonRecord(sink->source, sink->layout, point->first, point->second, point->fraction,
                          place, point->values, record);
}

void mvfxWriteTubeRing(const mvfxTubeSink* sink, const mvfxTubeFrame* frame,
                       const mvfxTubePoint* point, unsigned char* records)
{
    int64_t stride = (int64_t)sink->layout->stride;
    mvfxRibbonPoint* values = point->values;
    for (int32_t k = 0; k <= sink->sides; ++k)
    {
        float out[3];
        WayAt(frame, sink->sides, k, out, values->side);
        double place[3];
        for (int32_t j = 0; j < 3; ++j)
        {
            place[j] = point->here[j] + point->half * (double)out[j];
            values->faceNormal[j] = out[j];
        }
        values->corner[0] = values->along;
        values->corner[1] = (float)k / (float)sink->sides;
        WriteVertex(sink, point, place, records + k * stride);
    }
}

void mvfxWriteTubeCap(const mvfxTubeSink* sink, const mvfxTubeFrame* frame,
                      const mvfxTubePoint* point, bool end, unsigned char* records)
{
    int64_t stride = (int64_t)sink->layout->stride;
    mvfxRibbonPoint* values = point->values;
    float facing = end ? 1.0f : -1.0f;
    for (int32_t j = 0; j < 3; ++j)
    {
        values->faceNormal[j] = facing * frame->tangent[j];
    }
    values->corner[0] = 0.5f;
    values->corner[1] = 0.5f;
    WriteVertex(sink, point, point->here, records);
    for (int32_t k = 0; k <= sink->sides; ++k)
    {
        float out[3];
        WayAt(frame, sink->sides, k, out, values->side);
        double place[3];
        for (int32_t j = 0; j < 3; ++j)
        {
            place[j] = point->here[j] + point->half * (double)out[j];
        }
        float c = Dot(out, frame->normal);
        float binormal[3];
        Cross(frame->tangent, frame->normal, binormal);
        float s = Dot(out, binormal);
        values->corner[0] = 0.5f + 0.5f * c;
        values->corner[1] = 0.5f + 0.5f * s;
        WriteVertex(sink, point, place, records + (1 + k) * stride);
    }
}

void mvfxTubeSideIndices(int32_t sides, uint32_t ring, uint32_t next, uint32_t* out)
{
    for (int32_t k = 0; k < sides; ++k)
    {
        uint32_t a = ring + (uint32_t)k;
        uint32_t c = next + (uint32_t)k;
        const uint32_t quad[6] = {a, a + 1u, c, c, a + 1u, c + 1u};
        for (int32_t j = 0; j < 6; ++j)
        {
            out[6 * k + j] = quad[j];
        }
    }
}

void mvfxTubeCapIndices(int32_t sides, uint32_t cap, bool end, uint32_t* out)
{
    for (int32_t k = 0; k < sides; ++k)
    {
        uint32_t a = cap + 1u + (uint32_t)k;
        out[3 * k] = cap;
        out[3 * k + 1] = end ? a : a + 1u;
        out[3 * k + 2] = end ? a + 1u : a;
    }
}
