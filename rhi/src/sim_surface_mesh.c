// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface gas's mesh on the GPU (surface_mesh.wgsl), as the core's
// mvfxInstance_WriteSurfaceMesh writes it: the host's buffers, the box's
// map out of its frame rebased on the host's origin in binary64 and
// rounded once, and the marching tables (marching_words.c), uploaded with
// a surface's first mesh; a mesh a transfer pass writing its numbers and
// the kernel's five passes, recorded after the step and the reads.

#include "kernels.h"
#include "marching_words.h"
#include "numbers.h"
#include "sim_gas.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

// A block of cubes a workgroup (surface_mesh.wgsl's CHUNK), and the most
// workgroups a block pass dispatches (each takes the blocks a stride of
// them apart).
#define MESH_BLOCK  256u
#define MESH_GROUPS 4096u
#define NONE        0xFFFFFFFFu

// A surface's samples and cubes along its walk (none along an axis of one
// sample), and their blocks.
static uint32_t Cubes(const mvfxGasDef* def)
{
    const int32_t* n = def->size;
    uint32_t cubes = 1;
    for (int32_t k = 0; k < (n[2] > 1 ? 3 : 2); ++k)
    {
        cubes *= n[k] > 1 ? (uint32_t)(n[k] - 1) : 0u;
    }
    return cubes;
}

static uint32_t Blocks(uint32_t cubes)
{
    return (cubes + MESH_BLOCK - 1u) / MESH_BLOCK;
}

// The scratch's words: four slots a sample, four words a block (vertices,
// primitives, their offsets), the totals and the first refusal.
static uint64_t ScratchWords(const mvfxGasView* view)
{
    return 4u * (uint64_t)view->cells + 4u * Blocks(Cubes(&view->def)) + 3u;
}

static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mrhiCreateBuffer(sim->device, &def, out) == mrhi_success;
}

bool mvfxCreateSimSurfaceMesh(mvfxGpuSim* sim, SimGas* gas)
{
    if (gas->view.def.kind != mvfx_gasSurface)
    {
        return true;
    }
    mrhiBufferUsage stored = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    bool created =
        CreateBuffer(sim, 4u * ScratchWords(&gas->view), mrhi_bufferStorage, &gas->meshScratch) &&
        CreateBuffer(sim, 4u * MVFX_MARCHING_WORDS, stored, &gas->marching);
    for (int32_t m = 0; created && m < MVFX_GPU_SURFACE_MESHES; ++m)
    {
        created =
            CreateBuffer(sim, 4u * MESH_WORDS, mrhi_bufferUniform | mrhi_bufferCopyDestination,
                         &gas->meshes[m].numbers);
    }
    return created;
}

static void DestroyBuffer(mvfxGpuSim* sim, mrhiBufferId buffer)
{
    if (buffer.index1 != 0)
    {
        (void)mrhiDestroyBuffer(sim->device, buffer);
    }
}

void mvfxDestroySimSurfaceMesh(mvfxGpuSim* sim, SimGas* gas)
{
    DestroyBuffer(sim, gas->meshScratch);
    DestroyBuffer(sim, gas->marching);
    for (int32_t m = 0; m < MVFX_GPU_SURFACE_MESHES; ++m)
    {
        DestroyBuffer(sim, gas->meshes[m].numbers);
    }
}

mvfxGpuSurfaceMeshDef mvfxDefaultGpuSurfaceMeshDef(void)
{
    return (mvfxGpuSurfaceMeshDef){.cookie = MVFX_GPU_SURFACE_MESH_DEF_COOKIE};
}

// The most vertices and indices a mesh has, to which the capacities are
// held: fewer than 2^24 vertices (src/surface_mesh.c), and at most twelve
// triangles a cube (the tables' most) of MVFX_MAX_GAS_CELLS cubes, under
// 2^28 indices.
#define MOST_VERTICES (1u << 24)
#define MOST_INDICES  (1u << 28)

// A buffer's first word, when its offset is a multiple of 4 and its last
// word (words past the first) is within 2^32 words; else NONE.
static uint32_t FirstWord(uint64_t offset, uint64_t words)
{
    uint64_t first = offset / 4u;
    return offset % 4u == 0 && first + words <= UINT32_MAX ? (uint32_t)first : NONE;
}

// Whether a mesh def is in range for the simulation, its capacities held
// to the most a mesh has into caps.
static bool IsMeshValid(const mvfxGpuSim* sim, const mvfxGpuSurfaceMeshDef* def, uint32_t caps[2])
{
    bool valid = def->cookie == MVFX_GPU_SURFACE_MESH_DEF_COOKIE && def->gas >= 0 &&
                 def->gas < sim->gasCount &&
                 sim->gases[def->gas].view.def.kind == mvfx_gasSurface && isfinite(def->level) &&
                 def->positions.index1 != 0 && def->indices.index1 != 0 &&
                 def->vertexCapacity >= 0 && def->indexCapacity >= 0;
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = fabs(def->origin[k]) <= 0x1p52;
    }
    if (!valid)
    {
        return false;
    }
    caps[0] = def->vertexCapacity < MOST_VERTICES ? (uint32_t)def->vertexCapacity : MOST_VERTICES;
    caps[1] = def->indexCapacity < MOST_INDICES ? (uint32_t)def->indexCapacity : MOST_INDICES;
    return FirstWord(def->positionOffset, 3u * (uint64_t)caps[0]) != NONE &&
           (def->normals.index1 == 0 ||
            FirstWord(def->normalOffset, 3u * (uint64_t)caps[0]) != NONE) &&
           FirstWord(def->indexOffset, caps[1]) != NONE &&
           (def->counts.index1 == 0 ||
            FirstWord(def->countsOffset, MVFX_GPU_SURFACE_MESH_COUNTS) != NONE);
}

// A mesh's numbers (surface_mesh.wgsl's Mesh): the buffer current, the
// axes, the cubes and blocks; the capacities and first words; the box's
// map out of its frame (its turn, its center less the origin, its scale)
// and the level.
static void MeshWords(const SimGas* gas, const mvfxGpuSurfaceMeshDef* def, const uint32_t caps[2],
                      const mvfxGasInfo* info, SurfaceMesh* mesh)
{
    uint32_t cubes = Cubes(&gas->view.def);
    uint32_t blocks = Blocks(cubes);
    mesh->groups = blocks == 0 ? 1u : (blocks < MESH_GROUPS ? blocks : MESH_GROUPS);
    uint32_t* w = mesh->words;
    const uint32_t head[12] = {gas->added ? gas->after : gas->now,
                               gas->view.def.size[2] > 1 ? 3u : 2u,
                               cubes,
                               blocks,
                               caps[0],
                               caps[1],
                               FirstWord(def->positionOffset, 0),
                               def->normals.index1 != 0 ? FirstWord(def->normalOffset, 0) : NONE,
                               FirstWord(def->indexOffset, 0),
                               def->counts.index1 != 0 ? FirstWord(def->countsOffset, 0) : NONE,
                               mesh->groups,
                               4u * (uint32_t)gas->view.cells};
    memcpy(w, head, sizeof head);
    memcpy(w + 12, info->rotation, 4u * sizeof(float));
    mvfxFloatWords(w + 16, (float)(info->center[0] - def->origin[0]),
                   (float)(info->center[1] - def->origin[1]),
                   (float)(info->center[2] - def->origin[2]), info->scale);
    mvfxFloatWords(w + 20, def->level, 0.0f, 0.0f, 0.0f);
}

// Imports a mesh's buffers into the frame open: its numbers, the gas's,
// its scratch and tables, the host's (the simulation's own words for
// normals and counts it gives none of).
static bool ImportMesh(mvfxGpuSim* sim, SimGas* gas, const mvfxGpuSurfaceMeshDef* def,
                       SurfaceMesh* mesh, mrhiResourceId own[2])
{
    mesh->out[0] = def->positions;
    mesh->out[1] = def->normals;
    mesh->out[2] = def->indices;
    mesh->out[3] = def->counts;
    bool imported = mrhiImportBuffer(sim->device, mesh->numbers, &mesh->resource) == mrhi_success &&
                    mvfxImportSimGas(sim, gas) &&
                    mrhiImportBuffer(sim->device, gas->meshScratch, &own[0]) == mrhi_success &&
                    mrhiImportBuffer(sim->device, gas->marching, &own[1]) == mrhi_success;
    if (imported && def->normals.index1 == 0)
    {
        imported = mrhiImportBuffer(sim->device, sim->ownEvents, &mesh->out[1]) == mrhi_success;
    }
    if (imported && def->counts.index1 == 0)
    {
        imported = mrhiImportBuffer(sim->device, sim->ownCount, &mesh->out[3]) == mrhi_success;
    }
    return imported;
}

// Adds a mesh's passes: its numbers' (and the tables') write, then the
// kernel's five over its accesses, each resource once.
static bool AddMeshPasses(mvfxGpuSim* sim, const SimGas* gas, SurfaceMesh* mesh,
                          const mrhiResourceId own[2])
{
    const mrhiResourceId* r = gas->resources;
    const mrhiAccess written[2] = {{.resource = mesh->resource, .kind = mrhi_accessCopyDestination},
                                   {.resource = own[1], .kind = mrhi_accessCopyDestination}};
    mrhiAccess used[9] = {{.resource = r[GAS_NUMBERS], .kind = mrhi_accessUniform},
                          {.resource = r[GAS_FIELDS], .kind = mrhi_accessStorageRead},
                          {.resource = mesh->resource, .kind = mrhi_accessUniform},
                          {.resource = own[1], .kind = mrhi_accessStorageRead},
                          {.resource = own[0], .kind = mrhi_accessStorageReadWrite}};
    uint32_t count = 5;
    for (int32_t k = 0; k < 4; ++k)
    {
        bool again = false;
        for (int32_t j = 0; j < k; ++j)
        {
            again = again || mesh->out[j].index1 == mesh->out[k].index1;
        }
        if (!again)
        {
            used[count++] =
                (mrhiAccess){.resource = mesh->out[k], .kind = mrhi_accessStorageReadWrite};
        }
    }
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.passClass = mrhi_passTransfer;
    pass.accesses = written;
    pass.accessCount = mesh->uploads ? 2u : 1u;
    pass.neverCull = true;
    bool added = mrhiAddPass(sim->device, &pass, &mesh->passes[0]) == mrhi_success;
    pass.passClass = mrhi_passGraphics;
    pass.accesses = used;
    pass.accessCount = count;
    for (int32_t p = 1; added && p < MESH_PASSES; ++p)
    {
        added = mrhiAddPass(sim->device, &pass, &mesh->passes[p]) == mrhi_success;
    }
    return added;
}

mvfxResult mvfxGpuSim_AddSurfaceMesh(mvfxGpuSim* sim, const mvfxGpuSurfaceMeshDef* def)
{
    uint32_t caps[2] = {0, 0};
    if (sim == nullptr || def == nullptr || !IsMeshValid(sim, def, caps))
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorBusy;
    }
    SimGas* gas = &sim->gases[def->gas];
    if (gas->meshCount == MVFX_GPU_SURFACE_MESHES)
    {
        return mvfx_errorLimit;
    }
    mvfxGasInfo info;
    if (mvfxInstance_GetGasInfo(sim->context, sim->instance, def->gas, &info) != mvfx_success)
    {
        return mvfx_errorState;
    }
    SurfaceMesh* mesh = &gas->meshes[gas->meshCount];
    MeshWords(gas, def, caps, &info, mesh);
    // The tables go up with the frame's first mesh until a frame records.
    mesh->uploads = !gas->marchingWritten && gas->meshCount == 0;
    mrhiResourceId own[2] = {{0}, {0}};
    if (!ImportMesh(sim, gas, def, mesh, own) || !AddMeshPasses(sim, gas, mesh, own))
    {
        return mvfx_errorState;
    }
    gas->meshCount += 1;
    return mvfx_success;
}

// Records a mesh's passes: its numbers' write (and the tables'), then the
// kernel's five.
static bool RecordMesh(mvfxGpuSim* sim, SimGas* gas, const SurfaceMesh* mesh)
{
    mrhiResourceId scratch = {0};
    mrhiResourceId marching = {0};
    bool recorded =
        mrhiImportBuffer(sim->device, gas->meshScratch, &scratch) == mrhi_success &&
        mrhiImportBuffer(sim->device, gas->marching, &marching) == mrhi_success &&
        mrhiBeginPass(sim->device, mesh->passes[0]) == mrhi_success &&
        mvfxUpload(sim->device, &sim->uploaded, mesh->passes[0], mesh->resource, 0, mesh->words,
                   sizeof mesh->words) &&
        (!mesh->uploads || mvfxUpload(sim->device, &sim->uploaded, mesh->passes[0], marching, 0,
                                      mvfxMarchingWords, sizeof mvfxMarchingWords));
    recorded = mrhiEndPass(sim->device, mesh->passes[0]) == mrhi_success && recorded;
    const mrhiBinding bindings[9] = {
        {.slot = 0, .resource = gas->resources[GAS_NUMBERS], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = gas->resources[GAS_FIELDS], .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = mesh->resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 3, .resource = marching, .size = MRHI_WHOLE_SIZE},
        {.slot = 4, .resource = scratch, .size = MRHI_WHOLE_SIZE},
        {.slot = 5, .resource = mesh->out[0], .size = MRHI_WHOLE_SIZE},
        {.slot = 6, .resource = mesh->out[1], .size = MRHI_WHOLE_SIZE},
        {.slot = 7, .resource = mesh->out[2], .size = MRHI_WHOLE_SIZE},
        {.slot = 8, .resource = mesh->out[3], .size = MRHI_WHOLE_SIZE}};
    const mvfxKernel kernels[MESH_PASSES - 1] = {mvfx_kernelMeshCount, mvfx_kernelMeshOffsets,
                                                 mvfx_kernelMeshVertices, mvfx_kernelMeshRefuse,
                                                 mvfx_kernelMeshIndices};
    for (int32_t p = 1; recorded && p < MESH_PASSES; ++p)
    {
        mrhiPassId pass = mesh->passes[p];
        uint32_t groups = kernels[p - 1] == mvfx_kernelMeshOffsets ? 1u : mesh->groups;
        bool ran =
            mrhiBeginPass(sim->device, pass) == mrhi_success &&
            mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, kernels[p - 1])) ==
                mrhi_success &&
            mrhiSetBindings(sim->device, pass, 0, bindings, 9) == mrhi_success &&
            mrhiDispatch(sim->device, pass, groups, 1, 1) == mrhi_success;
        recorded = mrhiEndPass(sim->device, pass) == mrhi_success && ran;
    }
    return recorded;
}

bool mvfxRecordSimSurfaceMeshes(mvfxGpuSim* sim, SimGas* gas)
{
    bool recorded = true;
    for (int32_t m = 0; recorded && m < gas->meshCount; ++m)
    {
        recorded = RecordMesh(sim, gas, &gas->meshes[m]);
        gas->marchingWritten = gas->marchingWritten || (recorded && gas->meshes[m].uploads);
    }
    return recorded;
}
