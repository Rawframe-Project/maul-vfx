// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A simulation's baked tables: each emitter's sources and values over
// life, its records', its ribbons', its pair force's and its rigid hull's,
// laid out after its grids, and the hull's faces held as words for theirs.

#include "allocator.h"
#include "grids.h"
#include "sim_state.h"

static_assert(MVFX_GPU_TABLES >=
                  2 * MVFX_GPU_SOURCES + MVFX_GPU_LIVES + 1 + MVFX_RECORD_LIVES + 2 + 1 + 2,
              "room for every table");

// Places the baked tables of an emitter's sources and values over life,
// its step's and its records', after its grids.
void mvfxLaySimTables(const mvfxEmitterView* view, Emitter* emitter)
{
    mvfxGpuGrids* layout = &emitter->gridLayout;
    mvfxSourceView sources[MVFX_GPU_SOURCES];
    mvfxSourcesOf(view, sources);
    for (int32_t s = 0; s < MVFX_GPU_SOURCES; ++s)
    {
        emitter->tables.sources[s][0] =
            mvfxAddTable(layout, sources[s].curve, MVFX_CURVE_SAMPLES + 2);
        emitter->tables.sources[s][1] =
            mvfxAddTable(layout, sources[s].gradient, 4 * MVFX_CURVE_SAMPLES);
    }
    mvfxLifeView lives[MVFX_GPU_LIVES];
    mvfxLivesOf(view, lives);
    for (int32_t k = 0; k < MVFX_GPU_LIVES; ++k)
    {
        emitter->tables.lives[k] = mvfxAddTable(layout, lives[k].curve, MVFX_CURVE_SAMPLES + 2);
    }
    emitter->tables.gradient =
        mvfxAddTable(layout, view->overLife.gradient, 4 * MVFX_CURVE_SAMPLES);
    mvfxLifeView recordLives[MVFX_RECORD_LIVES];
    mvfxRecordLivesOf(view, recordLives);
    for (int32_t k = 0; k < MVFX_RECORD_LIVES; ++k)
    {
        emitter->tables.recordLives[k] =
            mvfxAddTable(layout, recordLives[k].curve, MVFX_CURVE_SAMPLES + 2);
    }
    emitter->tables.mesh = 0xFFFFFFFFu;
    bool ribbons = view->ribbon.ids != mvfx_ribbonNone;
    emitter->tables.ribbonWidth =
        mvfxAddTable(layout, ribbons ? view->ribbon.width.curve : nullptr, MVFX_CURVE_SAMPLES + 2);
    emitter->tables.ribbonColor =
        mvfxAddTable(layout, ribbons ? view->ribbon.color : nullptr, 4 * MVFX_CURVE_SAMPLES);
    emitter->tables.pairStrength =
        mvfxAddTable(layout, view->pairRadius > 0.0f ? view->pairStrength.curve : nullptr,
                     MVFX_CURVE_SAMPLES + 2);
    emitter->tables.hull =
        mvfxAddTable(layout, view->hullVertices, 3u * (uint32_t)view->hullVertexCount);
    emitter->tables.hullFaces = mvfxAddTable(layout, emitter->hullFaces, emitter->hullFaceWords);
}

// Holds a rigid hull's faces as words for its table: the starts, then the
// corners' vertices.
mvfxResult mvfxHoldSimHullFaces(mvfxGpuSim* sim, const mvfxEmitterView* view, Emitter* emitter)
{
    if (view->hullFaceCount == 0 || !emitter->rigid)
    {
        return mvfx_success;
    }
    uint32_t starts = (uint32_t)view->hullFaceCount + 1u;
    uint32_t corners = view->hullFaceStart[view->hullFaceCount];
    emitter->hullFaces = mvfxRhiAllocate(&sim->allocator, (starts + corners) * 4u, 4);
    if (emitter->hullFaces == nullptr)
    {
        return mvfx_errorCapacity;
    }
    emitter->hullFaceWords = starts + corners;
    for (uint32_t f = 0; f < starts; ++f)
    {
        emitter->hullFaces[f] = view->hullFaceStart[f];
    }
    for (uint32_t j = 0; j < corners; ++j)
    {
        emitter->hullFaces[starts + j] = view->hullLoop[j];
    }
    return mvfx_success;
}
