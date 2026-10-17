// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Collision with a depth image the host gives: the image checked once,
// its projection inverted, and a particle's step sampled against it in
// the image, nearest pixel, a surface within a thickness behind.

#ifndef MAUL_VFX_SRC_DEPTH_H
#define MAUL_VFX_SRC_DEPTH_H

#include "force.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

// A checked depth image with its projection and inverse in binary64.
typedef struct mvfxDepthView
{
    mvfxDepthImage image;
    double projection[16];
    double inverse[16];
} mvfxDepthView;

// A depth view as a step of an emitter takes it: the map from the world
// into its particles' space, and how its particles meet the image.
typedef struct mvfxResolvedDepth
{
    const mvfxDepthView* view;
    mvfxSpaceMap map;
    float radius;
    float thickness;
    int32_t samples;
} mvfxResolvedDepth;

// Checks an image and prepares its view; false for one out of range. A
// planned instance's image may come without pixels or normals (its
// simulation reads them on the GPU).
bool mvfxPrepareDepthView(const mvfxDepthImage* image, bool planned, mvfxDepthView* out);

// A resolved depth as a plan hands it over, for an emitter's collision.
void mvfxPlanDepth(const mvfxResolvedDepth* depth, const mvfxCollision* collision,
                   mvfxPlannedDepth* out);

// Whether a place in the particles' space is within the image, in front
// of the camera.
bool mvfxDepthSees(const mvfxResolvedDepth* depth, const float place[3]);

// The first sample of the segment s to e, in the particles' space, that
// is behind the image's surface by less than the thickness: its share of
// the segment, the surface point grown by the radius and its normal
// toward the camera, in the particles' space.
bool mvfxDepthHit(const mvfxResolvedDepth* depth, const float s[3], const float e[3], float* t,
                  float point[3], float normal[3]);

#endif
