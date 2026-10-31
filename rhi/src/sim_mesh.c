// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A simulation's mesh shapes: a mesh's bytes on the GPU, each
// emitter's region laid out after its tables with the host's copy it is
// written from, readied for a step when the mesh's serial changes, and
// its upload recorded.

#include "allocator.h"
#include "mesh_image.h"
#include "sim_state.h"
#include "upload.h"

mvfxResult mvfxMesh_GetGpuBytes(const mvfxContext* context, mvfxMeshId mesh, mvfxMeshMode mode,
                                int64_t* bytesOut)
{
    if (bytesOut != nullptr)
    {
        *bytesOut = 0;
    }
    if (context == nullptr || bytesOut == nullptr || mode < 0 || mode > mvfx_meshVolume)
    {
        return mvfx_errorInvalid;
    }
    mvfxMeshView view;
    if (mvfxMesh_GetView(context, mesh, &view) != mvfx_success)
    {
        return mvfx_errorStale;
    }
    *bytesOut = mvfxMeshImageBytes(&view, mode);
    return mvfx_success;
}

// A mesh shape's region, after the tables: room for the mesh its emitter
// spawns from now and for the def's hostMeshBytes, whichever is more, and
// the host's copy to write it from.
mvfxResult mvfxLaySimMesh(mvfxGpuSim* sim, const mvfxGpuSimDef* def, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    mvfxMeshView mesh;
    if (mvfxInstance_GetEmitterMesh(def->context, def->instance, e, &mesh) != mvfx_success)
    {
        return mvfx_errorStale;
    }
    int64_t bytes = mvfxMeshImageBytes(&mesh, sim->views[e].shape.meshMode);
    bytes = def->hostMeshBytes > bytes ? def->hostMeshBytes : bytes;
    bytes = (bytes + 3) / 4 * 4;
    if (bytes > MVFX_GPU_MESH_BYTES)
    {
        return mvfx_errorCapacity;
    }
    emitter->meshImage = mvfxRhiAllocate(&sim->allocator, (size_t)bytes, 4);
    if (emitter->meshImage == nullptr)
    {
        return mvfx_errorCapacity;
    }
    emitter->meshBytes = (uint32_t)bytes;
    emitter->tables.mesh = mvfxAddRoom(&emitter->gridLayout, (uint32_t)bytes / 4u);
    return mvfx_success;
}

// Readies a mesh shape's region for the step: the mesh its emitter spawns
// from now, written into the host's copy to upload when its serial is not
// the one the region holds; mvfx_errorCapacity past the region's room.
mvfxResult mvfxPlaceSimMesh(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    emitter->meshPending = 0;
    mvfxMeshView mesh;
    if (mvfxInstance_GetEmitterMesh(sim->context, sim->instance, e, &mesh) != mvfx_success)
    {
        return mvfx_errorStale;
    }
    if (mesh.serial == emitter->meshSerial)
    {
        return mvfx_success;
    }
    mvfxMeshMode mode = sim->views[e].shape.meshMode;
    int64_t bytes = mvfxMeshImageBytes(&mesh, mode);
    if (bytes > emitter->meshBytes)
    {
        return mvfx_errorCapacity;
    }
    mvfxMeshImage(&mesh, mode, emitter->meshImage);
    emitter->meshPending = mesh.serial;
    emitter->meshPendingBytes = (uint32_t)bytes;
    return mvfx_success;
}

// Records the upload of a mesh shape's region readied for the step.
bool mvfxWriteSimMesh(mvfxGpuSim* sim, mrhiPassId pass, Emitter* emitter)
{
    if (emitter->meshPending == 0)
    {
        return true;
    }
    bool written = mvfxUpload(sim->device, &sim->uploaded, pass, emitter->resources[BUFFER_GRIDS],
                              4u * (uint64_t)emitter->tables.mesh, emitter->meshImage,
                              emitter->meshPendingBytes);
    if (written)
    {
        emitter->meshSerial = emitter->meshPending;
        emitter->meshPending = 0;
    }
    return written;
}

mvfxResult mvfxGpuSim_SetEmitterMesh(mvfxGpuSim* sim, mvfxContext* context, int32_t emitter,
                                     mvfxMeshId mesh)
{
    if (sim == nullptr || context == nullptr || context != sim->context || emitter < 0 ||
        emitter >= sim->emitterCount || sim->emitters[emitter].meshImage == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorBusy;
    }
    mvfxMeshView view;
    if (mvfxMesh_GetView(context, mesh, &view) != mvfx_success)
    {
        return mvfx_errorStale;
    }
    if (mvfxMeshImageBytes(&view, sim->views[emitter].shape.meshMode) >
        sim->emitters[emitter].meshBytes)
    {
        return mvfx_errorCapacity;
    }
    return mvfxInstance_SetEmitterMesh(context, sim->instance, emitter, mesh);
}
