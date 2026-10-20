// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A smoke gas's step on the GPU (gas.wgsl): its numbers from its view and
// each step's plan, a pass of its own a dispatch (the frame graph orders
// passes, not a pass's dispatches), its levels' areas and coefficients
// made at its first step and after a clear.

#include "sim_gas.h"

#include "allocator.h"
#include "numbers.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"

#include <math.h>
#include <stdalign.h>
#include <string.h>

// The workgroups of a point pass, those of a row of its dispatch (a
// device takes 65,535 along a dimension; gas.wgsl's ROW invocations), and
// the most cells a level of the tail has.
#define GAS_GROUP  64u
#define GAS_ROW    32768u
#define TAIL_CELLS 512

// The first words of the numbers' parts (gas.wgsl's Gas).
#define WORD_FACES    16u
#define WORD_CELLS    24u
#define WORD_LEVELS   32u
#define WORD_SUBSTEPS 192u
#define WORD_SOURCES  288u
#define WORD_FORCES   416u
#define WORD_BURN     (WORD_FORCES + MVFX_FORCE_WORDS)

// The words of a substep's numbers (gas.wgsl's Substep).
#define SUBSTEP_WORDS 12u

// Whether the part runs a gas: smoke, without what later slices add.
static bool Runs(const mvfxGasView* view)
{
    const mvfxGasDef* def = &view->def;
    return def->kind == mvfx_gasSmoke && def->obstacleCount == 0 && !def->hostObstacles;
}

// Whether a gas's burn expands it.
static bool Expands(const mvfxGasDef* def)
{
    return def->burnExpansion > 0.0f && def->burnRate > 0.0f;
}

// Whether every wall a gas meets is closed: its expansion then less its
// mean (a smoke gas has no air).
static bool IsSealed(const mvfxGasDef* def)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        uint32_t both = 3u << (2 * k);
        if (def->size[k] > 1 && (def->closedWalls & both) != both)
        {
            return false;
        }
    }
    return true;
}

mvfxResult mvfxReadSimGases(mvfxGpuSim* sim, mvfxEffectId effect, int32_t* refusal)
{
    for (int32_t g = 0; g < MVFX_MAX_GASES; ++g)
    {
        SimGas* gas = &sim->gases[g];
        mvfxResult result = mvfxEffect_GetGasView(sim->context, effect, g, &gas->view);
        if (result == mvfx_errorInvalid)
        {
            break;
        }
        if (result != mvfx_success)
        {
            return result;
        }
        sim->gasCount = g + 1;
        if (!Runs(&gas->view))
        {
            *refusal = g;
            return mvfx_errorState;
        }
    }
    return mvfx_success;
}

// The cells of a level of a view.
static uint32_t LevelCells(const mvfxGasView* view, int32_t l)
{
    return (uint32_t)(view->levels[l][0] * view->levels[l][1] * view->levels[l][2]);
}

// The faces across k of a level of a view; none across an axis of one
// cell.
static uint32_t LevelFaces(const mvfxGasView* view, int32_t l, int32_t k)
{
    const int32_t* n = view->levels[l];
    if (n[k] == 1)
    {
        return 0;
    }
    uint32_t faces = 1;
    for (int32_t j = 0; j < 3; ++j)
    {
        faces *= (uint32_t)(n[j] + (j == k ? 1 : 0));
    }
    return faces;
}

static uint32_t AllFaces(const mvfxGasView* view, int32_t l)
{
    return LevelFaces(view, l, 0) + LevelFaces(view, l, 1) + LevelFaces(view, l, 2);
}

// The first level of the tail: the first of at most TAIL_CELLS cells, or
// the last.
static uint32_t TailOf(const mvfxGasView* view)
{
    for (int32_t l = 0; l < view->levelCount; ++l)
    {
        if (LevelCells(view, l) <= TAIL_CELLS)
        {
            return (uint32_t)l;
        }
    }
    return (uint32_t)(view->levelCount - 1);
}

// The dispatches of one V-cycle above the tail.
static int32_t CyclePasses(const mvfxGasView* view)
{
    return 6 * (int32_t)TailOf(view) + 1;
}

// The dispatches of a carry: one, or MacCormack's three.
static int32_t CarryPasses(const mvfxGasDef* def)
{
    return def->advection == mvfx_gasMacCormack ? 3 : 1;
}

int32_t mvfxSimGasPasses(const mvfxGasView* view)
{
    const mvfxGasDef* def = &view->def;
    bool solves = def->pressureCycles > 0;
    int32_t solve = solves ? 1 + def->pressureCycles * CyclePasses(view) : 0;
    int32_t confine = def->vorticity != 0.0f ? 2 : 0;
    int32_t burn = def->burnRate != 0.0f ? 1 : 0;
    int32_t mean = solves && Expands(def) && IsSealed(def) ? 1 : 0;
    int32_t substep = 2 + confine + 2 * CarryPasses(def) + burn + mean + solve + 1;
    return 1 + 2 * view->levelCount + def->substeps * substep;
}

// The fields' and the solve's floats, writing each array's first float
// into the numbers.
static void Lay(SimGas* gas, uint64_t* fields, uint64_t* solve)
{
    const mvfxGasView* view = &gas->view;
    uint32_t* w = gas->words;
    uint32_t at = 0;
    for (int32_t b = 0; b < 2; ++b)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            w[WORD_FACES + 4u * (uint32_t)b + (uint32_t)k] = at;
            at += (uint32_t)view->faces[k];
        }
    }
    for (int32_t b = 0; b < 2; ++b)
    {
        for (int32_t f = 0; f < 3; ++f)
        {
            w[WORD_CELLS + 4u * (uint32_t)b + (uint32_t)f] = at;
            at += (uint32_t)view->cells;
        }
    }
    // The work: a curl's three arrays, MacCormack's trace ahead of every
    // face or of the three scalars, or the expansion; then its mean.
    uint32_t scalars = 3u * (uint32_t)view->cells;
    uint32_t faces = AllFaces(view, 0);
    w[WORD_CELLS + 3u] = at;
    at += faces > scalars ? faces : scalars;
    w[WORD_CELLS + 7u] = at;
    at += 1u;
    *fields = at;
    at = 0;
    for (int32_t l = 0; l < view->levelCount; ++l)
    {
        uint32_t* level = w + WORD_LEVELS + 16u * (uint32_t)l;
        uint32_t cells = LevelCells(view, l);
        for (int32_t k = 0; k < 3; ++k)
        {
            level[k] = (uint32_t)view->levels[l][k];
        }
        level[3] = cells;
        for (int32_t a = 0; a < 4; ++a)
        {
            level[4 + a] = at;
            at += cells;
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            level[8 + k] = at;
            at += LevelFaces(view, l, k);
        }
        level[11] = 1u << (uint32_t)l;
        for (int32_t k = 0; k < 3; ++k)
        {
            level[12 + k] = at;
            at += LevelFaces(view, l, k);
        }
    }
    *solve = at;
}

// The numbers a step does not change: the sizes, walls, lattice and
// sources.
static void StaticNumbers(SimGas* gas)
{
    const mvfxGasView* view = &gas->view;
    const mvfxGasDef* def = &view->def;
    uint32_t* w = gas->words;
    for (int32_t k = 0; k < 3; ++k)
    {
        w[k] = (uint32_t)def->size[k];
    }
    w[3] = (uint32_t)view->levelCount;
    w[4] = def->closedWalls;
    w[5] = (uint32_t)def->pressureCycles;
    w[6] = (uint32_t)def->sourceCount;
    w[7] = gas->tail;
    // Jacobi's best smoothing weights, as the core's.
    float omega = def->size[2] > 1 ? 6.0f / 7.0f : 4.0f / 5.0f;
    mvfxFloatWords(w + 8, def->cellSize, omega, def->densityWeight, def->heatLift);
    for (int32_t s = 0; s < def->sourceCount; ++s)
    {
        const mvfxGasSource* source = &def->sources[s];
        uint32_t* at = w + WORD_SOURCES + 16u * (uint32_t)s;
        mvfxFloatWords(at, source->center[0], source->center[1], source->center[2], 0.0f);
        at[3] = (uint32_t)source->shape;
        mvfxFloatWords(at + 4, source->halfExtents[0], source->halfExtents[1],
                       source->halfExtents[2], source->radius);
        mvfxFloatWords(at + 8, source->density, source->temperature, source->fuel, 0.0f);
        mvfxFloatWords(at + 12, source->velocity[0], source->velocity[1], source->velocity[2],
                       source->setsVelocity ? 1.0f : 0.0f);
    }
    mvfxFloatWords(w + WORD_BURN, def->ignitionTemperature, def->burnHeat, def->burnSmoke,
                   def->vorticity);
    w[WORD_BURN + 4u] = def->advection == mvfx_gasMacCormack ? 1u : 0u;
    w[WORD_BURN + 5u] = Expands(def) ? 1u : 0u;
    w[WORD_BURN + 6u] = IsSealed(def) ? 1u : 0u;
    w[WORD_BURN + 7u] = 0u;
}

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mrhiCreateBuffer(sim->device, &def, out) == mrhi_success;
}

bool mvfxCreateSimGas(mvfxGpuSim* sim, SimGas* gas)
{
    const mvfxGasDef* def = &gas->view.def;
    mvfxGridId forceGrids[MVFX_MAX_FORCES] = {0};
    const mvfxGridId colliderGrids[MVFX_MAX_COLLIDERS] = {0};
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        forceGrids[f] = def->forces[f].grid;
    }
    if (!mvfxLayGrids(sim->context, forceGrids, colliderGrids, 0, &gas->grids))
    {
        return false;
    }
    gas->tail = TailOf(&gas->view);
    uint64_t fields = 0;
    uint64_t solve = 0;
    Lay(gas, &fields, &solve);
    StaticNumbers(gas);
    gas->capacity = mvfxSimGasPasses(&gas->view);
    gas->dispatches = mvfxRhiAllocate(&sim->allocator, (size_t)gas->capacity * sizeof(GasDispatch),
                                      alignof(GasDispatch));
    gas->passes = mvfxRhiAllocate(&sim->allocator, (size_t)gas->capacity * sizeof(mrhiPassId),
                                  alignof(mrhiPassId));
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    mrhiBufferUsage numbers = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    bool created = gas->dispatches != nullptr && gas->passes != nullptr &&
                   CreateBuffer(sim, 4u * MVFX_GAS_WORDS, numbers, &gas->buffers[GAS_NUMBERS]) &&
                   CreateBuffer(sim, 4u * fields, written, &gas->buffers[GAS_FIELDS]) &&
                   CreateBuffer(sim, 4u * solve, written, &gas->buffers[GAS_SOLVE]) &&
                   CreateBuffer(sim, 4u * (gas->grids.words > 0 ? gas->grids.words : 1u), written,
                                &gas->buffers[GAS_GRIDS]);
    for (int32_t r = 0; created && r < MVFX_GPU_GAS_READS; ++r)
    {
        created = CreateBuffer(sim, 4u * GAS_READ_WORDS, numbers, &gas->reads[r].numbers);
    }
    return created;
}

void mvfxDestroySimGas(mvfxGpuSim* sim, SimGas* gas)
{
    for (int32_t b = 0; b < GAS_BUFFERS; ++b)
    {
        if (gas->buffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, gas->buffers[b]);
        }
    }
    for (int32_t r = 0; r < MVFX_GPU_GAS_READS; ++r)
    {
        if (gas->reads[r].numbers.index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, gas->reads[r].numbers);
        }
    }
    size_t count = (size_t)gas->capacity;
    mvfxRhiRelease(&sim->allocator, gas->dispatches, count * sizeof(GasDispatch),
                   alignof(GasDispatch));
    mvfxRhiRelease(&sim->allocator, gas->passes, count * sizeof(mrhiPassId), alignof(mrhiPassId));
}

static uint32_t Groups(uint32_t points)
{
    return (points + GAS_GROUP - 1u) / GAS_GROUP;
}

// Appends a dispatch to the step.
static void Push(SimGas* gas, mvfxKernel kernel, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                 uint32_t groups)
{
    gas->dispatches[gas->count++] =
        (GasDispatch){.kernel = kernel, .root = {a, b, c, d}, .groups = groups};
}

// A V-cycle's dispatches: two sweeps and a restriction a level above the
// tail, the tail, then a prolongation and two sweeps a level back up.
static void Cycle(SimGas* gas, uint32_t s, uint32_t now)
{
    const mvfxGasView* view = &gas->view;
    for (uint32_t l = 0; l < gas->tail; ++l)
    {
        uint32_t cells = Groups(LevelCells(view, (int32_t)l));
        Push(gas, mvfx_kernelGasJacobi, s, now, l, 0, cells);
        Push(gas, mvfx_kernelGasJacobi, s, now, l, 1, cells);
        Push(gas, mvfx_kernelGasRestrict, s, now, l, 0, Groups(LevelCells(view, (int32_t)l + 1)));
    }
    Push(gas, mvfx_kernelGasTail, s, now, gas->tail, 0, 1);
    for (uint32_t l = gas->tail; l-- > 0;)
    {
        uint32_t cells = Groups(LevelCells(view, (int32_t)l));
        Push(gas, mvfx_kernelGasProlong, s, now, l, 0, cells);
        Push(gas, mvfx_kernelGasJacobi, s, now, l, 0, cells);
        Push(gas, mvfx_kernelGasJacobi, s, now, l, 1, cells);
    }
}

// A substep's carries of the velocity and of the scalars: semi-Lagrangian,
// or MacCormack's trace back, trace ahead and correction (gas.wgsl's
// CARRY_ stages).
static void Carry(SimGas* gas, const mvfxGasDef* def, uint32_t s, uint32_t now, uint32_t faces,
                  uint32_t cells)
{
    if (def->advection != mvfx_gasMacCormack)
    {
        Push(gas, mvfx_kernelGasCarry, s, now, 0, 0, faces);
        Push(gas, mvfx_kernelGasCarryCells, s, now, 0, 0, cells);
        return;
    }
    for (uint32_t stage = 1; stage <= 3; ++stage)
    {
        Push(gas, mvfx_kernelGasCarry, s, now, 0, stage, faces);
    }
    for (uint32_t stage = 1; stage <= 3; ++stage)
    {
        Push(gas, mvfx_kernelGasCarryCells, s, now, 0, stage, cells);
    }
}

// The step's substeps' numbers and dispatches, as the core's mvfxStepGas
// splits the plan's ticks.
static void Plan(SimGas* gas, const mvfxGasPlan* plan)
{
    const mvfxGasView* view = &gas->view;
    const mvfxGasDef* def = &view->def;
    uint32_t* w = gas->words;
    mvfxFloatWords(w + 12, plan->gravity[0], plan->gravity[1], plan->gravity[2], 0.0f);
    uint32_t forceGrids[MVFX_MAX_FORCES][5];
    for (int32_t f = 0; f < MVFX_MAX_FORCES; ++f)
    {
        mvfxForceGridWords(&gas->grids, f, forceGrids[f]);
    }
    mvfxForceWords(plan->forces, plan->forceCount, 0.0f, (const uint32_t (*)[5])forceGrids,
                   w + WORD_FORCES);
    gas->count = 0;
    gas->clears = plan->cleared;
    if (plan->cleared)
    {
        gas->now = 0;
        gas->prepared = false;
    }
    for (int32_t l = 0; !gas->prepared && l < view->levelCount; ++l)
    {
        uint32_t faces = AllFaces(view, l);
        Push(gas, mvfx_kernelGasAreas, 0, 0, (uint32_t)l, 0, Groups(faces));
        Push(gas, mvfx_kernelGasCoefficients, 0, 0, (uint32_t)l, 0,
             Groups(faces + LevelCells(view, l)));
    }
    uint32_t cells = Groups((uint32_t)view->cells);
    uint32_t faces = Groups(AllFaces(view, 0));
    int64_t from = plan->start;
    int64_t to = plan->start + plan->ticks;
    uint32_t now = gas->now;
    for (int32_t s = 0; s < def->substeps; ++s)
    {
        int64_t start = from + (to - from) * s / def->substeps;
        int64_t end = from + (to - from) * (s + 1) / def->substeps;
        if (end <= start)
        {
            continue;
        }
        float h = (float)((double)(end - start) / (double)MVFX_TICKS_PER_SECOND);
        uint32_t* step = w + WORD_SUBSTEPS + SUBSTEP_WORDS * (uint32_t)s;
        float keep = (float)mvfxExpMinus((double)(def->velocityDecay * h));
        float since = (float)((double)(start - from) / (double)MVFX_TICKS_PER_SECOND);
        mvfxFloatWords(step, h, keep, start < plan->stopTick ? 1.0f : 0.0f, since);
        mvfxFloatWords(step + 4, (float)mvfxExpMinus((double)(def->densityDecay * h)),
                       (float)mvfxExpMinus((double)(def->temperatureDecay * h)),
                       (float)mvfxExpMinus((double)(def->fuelDecay * h)), 0.0f);
        // The burn's share and the divergence a unit burned asks for, as
        // the core's mvfxGasBurn makes them.
        float share = 1.0f - (float)mvfxExpMinus((double)(def->burnRate * h));
        mvfxFloatWords(step + 8, share, def->burnExpansion * def->cellSize / h, 0.0f, 0.0f);
        uint32_t u = (uint32_t)s;
        Push(gas, mvfx_kernelGasPourCells, u, now, 0, 0, cells);
        Push(gas, mvfx_kernelGasFaces, u, now, 0, 0, faces);
        if (def->vorticity != 0.0f)
        {
            Push(gas, mvfx_kernelGasCurl, u, now, 0, 0, cells);
            Push(gas, mvfx_kernelGasConfine, u, now, 0, 0, faces);
        }
        Carry(gas, def, u, now, faces, cells);
        now = 1u - now;
        if (def->burnRate != 0.0f)
        {
            Push(gas, mvfx_kernelGasBurn, u, now, 0, 0, cells);
        }
        if (def->pressureCycles > 0 && Expands(def) && IsSealed(def))
        {
            Push(gas, mvfx_kernelGasMean, u, now, 0, 0, 1);
        }
        if (def->pressureCycles > 0)
        {
            Push(gas, mvfx_kernelGasDivergence, u, now, 0, 0, cells);
        }
        for (int32_t c = 0; c < def->pressureCycles; ++c)
        {
            Cycle(gas, u, now);
        }
        Push(gas, mvfx_kernelGasCorrect, u, now, 0, 0, faces);
    }
    gas->now = now;
}

mvfxResult mvfxAddSimGasPasses(mvfxGpuSim* sim, int32_t g)
{
    SimGas* gas = &sim->gases[g];
    // The reads of a frame the host dropped go.
    gas->added = false;
    gas->readCount = 0;
    gas->imported = false;
    static mvfxGasPlan plan;
    mvfxResult result = mvfxInstance_GetGasPlan(sim->context, sim->instance, g, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    Plan(gas, &plan);
    mrhiResourceId* r = gas->resources;
    for (int32_t b = 0; b < GAS_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, gas->buffers[b], &r[b]) != mrhi_success)
        {
            return mvfx_errorState;
        }
    }
    gas->imported = true;
    const mrhiAccess written[GAS_BUFFERS] = {
        {.resource = r[GAS_NUMBERS], .kind = mrhi_accessCopyDestination},
        {.resource = r[GAS_FIELDS], .kind = mrhi_accessCopyDestination},
        {.resource = r[GAS_SOLVE], .kind = mrhi_accessCopyDestination},
        {.resource = r[GAS_GRIDS], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[GAS_BUFFERS] = {
        {.resource = r[GAS_NUMBERS], .kind = mrhi_accessUniform},
        {.resource = r[GAS_FIELDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[GAS_SOLVE], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[GAS_GRIDS], .kind = mrhi_accessStorageRead}};
    for (int32_t p = -1; p < gas->count; ++p)
    {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = p < 0 ? mrhi_passTransfer : mrhi_passGraphics;
        def.accesses = p < 0 ? written : used;
        def.accessCount = GAS_BUFFERS;
        // Its writes outlive the frame, read by the next step or the host.
        def.neverCull = true;
        if (mrhiAddPass(sim->device, &def, &gas->passes[p + 1]) != mrhi_success)
        {
            return mvfx_errorState;
        }
    }
    gas->added = true;
    return mvfx_success;
}

// Dispatches groups of the point passes' workgroups, in rows past
// GAS_ROW.
static bool Dispatch(mvfxGpuSim* sim, mrhiPassId pass, uint32_t groups)
{
    return groups == 0 || mrhiDispatch(sim->device, pass, groups < GAS_ROW ? groups : GAS_ROW,
                                       (groups + GAS_ROW - 1u) / GAS_ROW, 1) == mrhi_success;
}

// Records the pass writing the numbers, and the fields and solve emptied
// after a clear.
static bool RecordWrite(mvfxGpuSim* sim, SimGas* gas)
{
    mrhiPassId pass = gas->passes[0];
    const mrhiResourceId* r = gas->resources;
    bool recorded = mrhiBeginPass(sim->device, pass) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, pass, r[GAS_NUMBERS], 0, gas->words,
                               sizeof gas->words);
    for (int32_t b = GAS_FIELDS; recorded && gas->clears && b <= GAS_SOLVE; ++b)
    {
        recorded = mrhiClearBuffer(sim->device, pass, r[b], 0, MRHI_WHOLE_SIZE) == mrhi_success;
    }
    recorded = recorded && mvfxWriteGrids(sim->device, &sim->uploaded, pass, r[GAS_GRIDS],
                                          sim->context, &gas->grids);
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records the step's dispatches.
static bool RecordStep(mvfxGpuSim* sim, SimGas* gas)
{
    bool recorded = RecordWrite(sim, gas);
    const mrhiResourceId* r = gas->resources;
    mrhiBinding bindings[GAS_BUFFERS];
    for (int32_t b = 0; b < GAS_BUFFERS; ++b)
    {
        bindings[b] = (mrhiBinding){.slot = (uint32_t)b, .resource = r[b], .size = MRHI_WHOLE_SIZE};
    }
    for (int32_t p = 0; recorded && p < gas->count; ++p)
    {
        const GasDispatch* d = &gas->dispatches[p];
        mrhiPassId pass = gas->passes[p + 1];
        bool ran =
            mrhiBeginPass(sim->device, pass) == mrhi_success &&
            mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, d->kernel)) ==
                mrhi_success &&
            mrhiSetBindings(sim->device, pass, 0, bindings, GAS_BUFFERS) == mrhi_success &&
            mrhiSetRootBlock(sim->device, pass, 0, d->root, sizeof d->root) == mrhi_success &&
            Dispatch(sim, pass, d->groups);
        recorded = mrhiEndPass(sim->device, pass) == mrhi_success && ran;
    }
    gas->prepared = gas->prepared || recorded;
    return recorded;
}

// Records a read's passes: its numbers' write, then its words.
static bool RecordRead(mvfxGpuSim* sim, const SimGas* gas, const GasRead* read)
{
    bool recorded = mrhiBeginPass(sim->device, read->passes[0]) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, read->passes[0], read->resource, 0,
                               read->words, sizeof read->words);
    recorded = mrhiEndPass(sim->device, read->passes[0]) == mrhi_success && recorded;
    const mrhiBinding bindings[4] = {
        {.slot = 0, .resource = gas->resources[GAS_NUMBERS], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = gas->resources[GAS_FIELDS], .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = read->resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 3, .resource = read->target, .size = MRHI_WHOLE_SIZE}};
    mrhiPassId pass = read->passes[1];
    bool ran =
        recorded && mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, mvfx_kernelGasRead)) ==
            mrhi_success &&
        mrhiSetBindings(sim->device, pass, 0, bindings, 4) == mrhi_success &&
        Dispatch(sim, pass, Groups(read->written));
    return mrhiEndPass(sim->device, pass) == mrhi_success && ran;
}

bool mvfxRecordSimGas(mvfxGpuSim* sim, SimGas* gas)
{
    bool recorded = !gas->added || RecordStep(sim, gas);
    gas->added = false;
    for (int32_t r = 0; recorded && r < gas->readCount; ++r)
    {
        recorded = RecordRead(sim, gas, &gas->reads[r]);
    }
    gas->readCount = 0;
    gas->imported = false;
    return recorded;
}

mvfxGpuGasReadDef mvfxDefaultGpuGasReadDef(void)
{
    return (mvfxGpuGasReadDef){
        .cookie = MVFX_GPU_GAS_READ_DEF_COOKIE,
        .read = {.field = mvfx_gasDensity, .format = mvfx_formatFloat32, .size = {1, 1, 1}}};
}

// A format's bytes a value, 0 for none.
static uint32_t FormatBytes(mvfxFormat format)
{
    switch (format)
    {
    case mvfx_formatFloat32:
        return 4;
    case mvfx_formatFloat16:
    case mvfx_formatUnorm16:
    case mvfx_formatSnorm16:
        return 2;
    case mvfx_formatUnorm8:
    case mvfx_formatSnorm8:
        return 1;
    default:
        return 0;
    }
}

// A read's numbers, false for one out of range for the gas or its buffer
// (as the core's IsReadValid, and its bytes within 32-bit addresses).
static bool ReadWords(const SimGas* gas, const mvfxGpuGasReadDef* def, uint32_t words[20],
                      uint32_t* written)
{
    const mvfxGasRead* read = &def->read;
    const int32_t* n = gas->view.def.size;
    uint32_t bytes = FormatBytes(read->format);
    bool normalized = read->format != mvfx_formatFloat32 && read->format != mvfx_formatFloat16;
    bool valid =
        read->field >= mvfx_gasDensity && read->field <= mvfx_gasVelocityZ && bytes > 0 &&
        (!normalized || (isfinite(read->low) && isfinite(read->high) && read->high > read->low));
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = read->first[k] >= 0 && read->size[k] >= 1 && read->size[k] <= n[k] - read->first[k];
    }
    if (!valid)
    {
        return false;
    }
    uint64_t row = (uint64_t)read->size[0] * bytes;
    uint64_t rowPitch = read->rowBytes == 0 ? row : read->rowBytes;
    uint64_t layer = rowPitch * (uint64_t)read->size[1];
    uint64_t layerPitch = read->layerBytes == 0 ? layer : read->layerBytes;
    uint64_t end =
        layerPitch * (uint64_t)(read->size[2] - 1) + rowPitch * (uint64_t)(read->size[1] - 1) + row;
    if (rowPitch < row || layerPitch < layer || def->offset % 4u != 0 || end > def->bytes ||
        def->offset + end > UINT32_MAX)
    {
        return false;
    }
    *written = (uint32_t)((def->offset + end + 3u) / 4u - def->offset / 4u);
    const uint32_t head[20] = {(uint32_t)read->field,
                               (uint32_t)read->format,
                               gas->now,
                               *written,
                               (uint32_t)read->first[0],
                               (uint32_t)read->first[1],
                               (uint32_t)read->first[2],
                               0,
                               (uint32_t)read->size[0],
                               (uint32_t)read->size[1],
                               (uint32_t)read->size[2],
                               bytes,
                               (uint32_t)rowPitch,
                               (uint32_t)layerPitch,
                               (uint32_t)def->offset,
                               (uint32_t)end};
    memcpy(words, head, sizeof head);
    mvfxFloatWords(words + 16, read->low, read->high, 0.0f, 0.0f);
    return true;
}

mvfxResult mvfxGpuSim_AddGasRead(mvfxGpuSim* sim, const mvfxGpuGasReadDef* def)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_GAS_READ_DEF_COOKIE ||
        def->gas < 0 || def->gas >= sim->gasCount || def->target.index1 == 0)
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorState;
    }
    SimGas* gas = &sim->gases[def->gas];
    if (gas->readCount == MVFX_GPU_GAS_READS)
    {
        return mvfx_errorLimit;
    }
    GasRead* read = &gas->reads[gas->readCount];
    if (!ReadWords(gas, def, read->words, &read->written))
    {
        return mvfx_errorInvalid;
    }
    read->target = def->target;
    mrhiResourceId* r = gas->resources;
    bool imported = mrhiImportBuffer(sim->device, read->numbers, &read->resource) == mrhi_success;
    for (int32_t b = 0; imported && !gas->imported && b < GAS_BUFFERS; ++b)
    {
        imported = mrhiImportBuffer(sim->device, gas->buffers[b], &r[b]) == mrhi_success;
    }
    gas->imported = imported;
    const mrhiAccess written = {.resource = read->resource, .kind = mrhi_accessCopyDestination};
    const mrhiAccess used[4] = {{.resource = r[GAS_NUMBERS], .kind = mrhi_accessUniform},
                                {.resource = r[GAS_FIELDS], .kind = mrhi_accessStorageRead},
                                {.resource = read->resource, .kind = mrhi_accessUniform},
                                {.resource = def->target, .kind = mrhi_accessStorageReadWrite}};
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.passClass = mrhi_passTransfer;
    pass.accesses = &written;
    pass.accessCount = 1;
    pass.neverCull = true;
    bool added = imported && mrhiAddPass(sim->device, &pass, &read->passes[0]) == mrhi_success;
    pass.passClass = mrhi_passGraphics;
    pass.accesses = used;
    pass.accessCount = 4;
    added = added && mrhiAddPass(sim->device, &pass, &read->passes[1]) == mrhi_success;
    if (!added)
    {
        return mvfx_errorState;
    }
    gas->readCount += 1;
    return mvfx_success;
}

int32_t mvfxGpuSim_GasPasses(const mvfxGpuSim* sim, int32_t gas)
{
    if (sim == nullptr || gas < 0 || gas >= sim->gasCount)
    {
        return 0;
    }
    return mvfxSimGasPasses(&sim->gases[gas].view);
}
