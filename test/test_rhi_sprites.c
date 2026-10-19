// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sprites expanded into quads on a real device (lavapipe on CI) beside
// the CPU path's mvfxInstance_ExpandSprites, compared after every step
// (rhi_harness.h) from the GPU's own places: each vertex's record and
// each index, and the counts. Covered: every billboard (facing the view,
// rotated by angles, upright on Y, fixed by orientations, directional
// with a stretch), the world's space and the instance's under a moving,
// scaled instance, youngest first and sorted back to front, and buffers
// too small for the count on either side. Skips (77) without an adapter,
// unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A vertex: its place, colour, corner, serial, size, width, velocity and
// age.
static const mvfxLayoutField s_fields[8] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
    {.field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 4, .offset = 12},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 16},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 24},
    {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 2, .offset = 28},
    {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 32},
    {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 36},
    {.field = mvfx_recordAge, .format = mvfx_formatFloat32, .components = 1, .offset = 48}};

static mvfxLayoutDef VertexLayout(void)
{
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = s_fields;
    layout.fieldCount = 8;
    layout.stride = 52;
    return layout;
}

// A sprite of drawn sizes and colours turned by a billboard.
static mvfxEmitterDef Billboarded(mvfxBillboard billboard)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.3f}, .max = {0.4f, 0.5f, 0.6f}};
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.2f, 0.2f, 1}, .max = {1, 1, 1, 1}};
    emitter.sprite = (mvfxSpriteDef){.billboard = billboard, .stretch = 0.1f};
    return emitter;
}

// Compares a sprite emitter's quads, the buffers holding vertices and
// indices for so many quads.
static void Quads(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, int64_t vertices,
                  int64_t indices, const char* what)
{
    const mvfxLayoutDef layout = VertexLayout();
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .pace = 0.1,
                   .layout = &layout,
                   .recordCapacity = 4 * vertices,
                   .ribbons = true,
                   .strips = true,
                   .sprites = true,
                   .ribbonCapacity = 6 * indices},
            what);
}

static void TestBillboards(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Billboarded(mvfx_billboardCamera);
    emitter.space = mvfx_spaceWorld;
    Quads(gpu, kernels, &emitter, 1000, 1000, "quads facing the view, in the world's space");
    emitter = Billboarded(mvfx_billboardRotated);
    emitter.angle = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {-3.0f, 3.0f}};
    emitter.spin = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {-2.0f, 2.0f}};
    emitter.space = mvfx_spaceLocal;
    Quads(gpu, kernels, &emitter, 1000, 1000, "quads rotated by angles, in the instance's space");
    emitter = Billboarded(mvfx_billboardAxisY);
    Quads(gpu, kernels, &emitter, 1000, 1000, "quads upright on Y");
    emitter = Billboarded(mvfx_billboardFixed);
    emitter.orientationMode = mvfx_orientationRandom;
    Quads(gpu, kernels, &emitter, 1000, 1000, "quads fixed by random orientations");
    emitter = Billboarded(mvfx_billboardDirectional);
    Quads(gpu, kernels, &emitter, 1000, 1000, "quads along the velocity, stretched");
}

static void TestOrderAndRoom(Gpu* gpu, mvfxGpu* kernels)
{
    // Youngest first, then sorted back to front; then room for 40 quads'
    // vertices, then for 30 quads' indices.
    mvfxEmitterDef emitter = Billboarded(mvfx_billboardCamera);
    emitter.sortMode = mvfx_sortYoungestFirst;
    Quads(gpu, kernels, &emitter, 1000, 1000, "quads youngest first");
    emitter.sortMode = mvfx_sortBackToFront;
    Quads(gpu, kernels, &emitter, 1000, 1000, "quads sorted back to front");
    emitter = Billboarded(mvfx_billboardCamera);
    Quads(gpu, kernels, &emitter, 40, 1000, "room for the vertices of 40 quads");
    Quads(gpu, kernels, &emitter, 1000, 30, "room for the indices of 30 quads");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestBillboards(gpu, kernels);
    TestOrderAndRoom(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
