// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbons drawn round: a ring of vertices about each point, turned along
// the ribbon by rotation-minimizing frames, closed by optional caps.

#ifndef MAUL_VFX_SRC_TUBE_H
#define MAUL_VFX_SRC_TUBE_H

#include "output.h"

// The most sides of a tube is the public MVFX_MAX_TUBE_SIDES.

// A tube's frame at its last point: the place, the unit tangent and the
// unit normal its rings start from; started once it has one.
typedef struct mvfxTubeFrame
{
    double place[3];
    float tangent[3];
    float normal[3];
    bool started;
} mvfxTubeFrame;

// Carries a frame to the next point by double reflection (Wang, Juttler,
// Zheng and Liu, "Computation of Rotation Minimizing Frames", ACM TOG
// 2008); the first point's normal is across its tangent from the axis
// least along it.
void mvfxAdvanceTubeFrame(mvfxTubeFrame* frame, const double here[3], const float tangent[3]);

// Where a tube's vertices go: the records' source and layout, and the
// tube's sides.
typedef struct mvfxTubeSink
{
    const mvfxRecordSource* source;
    const mvfxOutputLayout* layout;
    int32_t sides;
} mvfxTubeSink;

// A point of a tube: the particles and the fraction between them it
// takes its values from, its place, its half width and its own values.
typedef struct mvfxTubePoint
{
    int32_t first;
    int32_t second;
    float fraction;
    const double* here;
    double half;
    mvfxRibbonPoint* values;
} mvfxTubePoint;

// Writes a point's ring of sides + 1 vertices at records: vertex k at
// angle 2 pi k / sides from the frame's normal toward the tangent crossed
// with it, its face normal outward, its corner the coordinate along and
// k / sides.
void mvfxWriteTubeRing(const mvfxTubeSink* sink, const mvfxTubeFrame* frame,
                       const mvfxTubePoint* point, unsigned char* records);

// Writes a cap of sides + 2 vertices at records (its center, then a ring),
// facing back along the tangent at the start or along it at the end, its
// corners a disc's.
void mvfxWriteTubeCap(const mvfxTubeSink* sink, const mvfxTubeFrame* frame,
                      const mvfxTubePoint* point, bool end, unsigned char* records);

// The indices of the quads between a ring at a vertex and the next ring
// (sides * 6), counterclockwise seen from outside.
void mvfxTubeSideIndices(int32_t sides, uint32_t ring, uint32_t next, uint32_t* out);

// The indices of a cap at a vertex (sides * 3), counterclockwise seen
// from the side it faces.
void mvfxTubeCapIndices(int32_t sides, uint32_t cap, bool end, uint32_t* out);

#endif
