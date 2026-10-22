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

// The most cells a level of the tail has.
#define TAIL_CELLS 512

// The first words of the numbers' parts (gas.wgsl's Gas).
#define WORD_FACES     16u
#define WORD_CELLS     24u
#define WORD_LEVELS    32u
#define WORD_SUBSTEPS  192u
#define WORD_SOURCES   288u
#define WORD_FORCES    416u
#define WORD_BURN      (WORD_FORCES + MVFX_FORCE_WORDS)
#define WORD_OBSTACLES (WORD_BURN + 8u)
// Where the obstacles before start among the obstacles' words.
#define BEFORE_WORDS (4u + 2u * MVFX_MAX_COLLIDERS * MVFX_COLLIDER_WORDS)

// The words of a substep's numbers (gas.wgsl's Substep).
#define SUBSTEP_WORDS 12u

// What gasFaces does (gas.wgsl's FACES_*): the sources and what follows,
// the sources alone, or what follows them.
#define FACES_ALL     0u
#define FACES_SOURCES 1u
#define FACES_AFTER   2u

// Whether the part runs a gas: smoke, a liquid gas and a surface.
static bool Runs(const mvfxGasView* view)
{
    return view->def.kind == mvfx_gasSmoke || view->def.kind == mvfx_gasLiquid ||
           view->def.kind == mvfx_gasSurface;
}

// Whether a gas has obstacles: its own, or the instance's colliders.
static bool HasObstacles(const mvfxGasDef* def)
{
    return def->obstacleCount > 0 || def->hostObstacles;
}

// Whether a gas's burn expands it.
static bool Expands(const mvfxGasDef* def)
{
    return def->kind == mvfx_gasLiquid || (def->burnExpansion > 0.0f && def->burnRate > 0.0f);
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

int32_t mvfxSimGasPasses(const mvfxGasView* view, int32_t splats)
{
    const mvfxGasDef* def = &view->def;
    bool solves = def->pressureCycles > 0;
    int32_t solve = solves ? 1 + def->pressureCycles * CyclePasses(view) : 0;
    int32_t confine = def->vorticity != 0.0f ? 2 : 0;
    int32_t burn = def->burnRate != 0.0f ? 1 : 0;
    int32_t mean = solves && Expands(def) && IsSealed(def) ? 1 : 0;
    int32_t solids = HasObstacles(def) ? 2 : 0;
    int32_t substep = 2 + confine + 2 * CarryPasses(def) + solids + burn + mean + solve + 1;
    int32_t mark = HasObstacles(def) ? 1 : 0;
    if (def->kind == mvfx_gasSurface)
    {
        return 1 + mvfxSimSurfaceGasPasses(view);
    }
    if (def->kind == mvfx_gasLiquid)
    {
        return 1 + mark + 2 * view->levelCount + mvfxSimLiquidGasPasses(view, mean + solve + 1);
    }
    return 1 + mark + 2 * view->levelCount + def->substeps * substep + 1 +
           mvfxSimSplatPasses(splats, def->substeps);
}

uint32_t mvfxSimGasFaces(const mvfxGasView* view)
{
    uint32_t floats = 0;
    uint32_t read = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        floats += (uint32_t)view->faces[k];
        read = view->def.size[k] > 1 ? floats : read;
    }
    return read;
}

uint32_t mvfxSimGasFacesAt(const SimGas* gas)
{
    return gas->words[WORD_FACES + 4u * gas->now];
}

// The solve's levels' floats, writing each array's first float into the
// numbers.
static uint64_t LayLevels(SimGas* gas)
{
    const mvfxGasView* view = &gas->view;
    uint32_t at = 0;
    for (int32_t l = 0; l < view->levelCount; ++l)
    {
        uint32_t* level = gas->words + WORD_LEVELS + 16u * (uint32_t)l;
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
        // A liquid's mask (gas.wgsl's solvedAt), none for smoke.
        bool liquid = view->def.kind == mvfx_gasLiquid;
        level[15] = liquid ? at : UINT32_MAX;
        at += liquid ? cells : 0u;
    }
    return at;
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
    // A surface holds its distance alone, and no work.
    bool surface = view->def.kind == mvfx_gasSurface;
    for (int32_t b = 0; b < 2; ++b)
    {
        for (int32_t f = 0; f < 3; ++f)
        {
            w[WORD_CELLS + 4u * (uint32_t)b + (uint32_t)f] = at;
            at += surface && f > 0 ? 0u : (uint32_t)view->cells;
        }
    }
    // The cells' solids, with obstacles; the work: a curl's three arrays,
    // MacCormack's trace ahead of every face or of the three scalars, or
    // the expansion; then its mean.
    w[WORD_FACES + 3u] = at;
    at += HasObstacles(&view->def) ? (uint32_t)view->cells : 0u;
    uint32_t scalars = surface ? 0u : 3u * (uint32_t)view->cells;
    uint32_t faces = AllFaces(view, 0);
    w[WORD_CELLS + 3u] = at;
    at += faces > scalars ? faces : scalars;
    w[WORD_CELLS + 7u] = at;
    at += 1u;
    *fields = at;
    *solve = LayLevels(gas);
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
    float empty = MVFX_GAS_EMPTY;
    memcpy(&w[WORD_BURN + 7u], &empty, sizeof empty);
}

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mrhiCreateBuffer(sim->device, &def, out) == mrhi_success;
}

bool mvfxCreateSimGas(mvfxGpuSim* sim, SimGas* gas, uint32_t hostWords)
{
    const mvfxGasDef* def = &gas->view.def;
    mvfxGridId forceGrids[MVFX_MAX_FORCES] = {0};
    mvfxGridId colliderGrids[MVFX_MAX_COLLIDERS] = {0};
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        forceGrids[f] = def->forces[f].grid;
    }
    for (int32_t c = 0; c < def->obstacleCount; ++c)
    {
        colliderGrids[c] = def->obstacles[c].grid;
    }
    if (!mvfxLayGrids(sim->context, forceGrids, colliderGrids, def->hostObstacles ? hostWords : 0,
                      &gas->grids))
    {
        return false;
    }
    gas->tail = TailOf(&gas->view);
    uint64_t fields = 0;
    uint64_t solve = 0;
    Lay(gas, &fields, &solve);
    StaticNumbers(gas);
    if (!mvfxCreateSimSplats(sim, gas, (int32_t)(gas - sim->gases)))
    {
        return false;
    }
    gas->capacity = mvfxSimGasPasses(&gas->view, gas->splatRoom);
    gas->dispatches = mvfxRhiAllocate(&sim->allocator, (size_t)gas->capacity * sizeof(GasDispatch),
                                      alignof(GasDispatch));
    gas->passes = mvfxRhiAllocate(&sim->allocator, (size_t)gas->capacity * sizeof(mrhiPassId),
                                  alignof(mrhiPassId));
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    mrhiBufferUsage numbers = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    bool created =
        gas->dispatches != nullptr && gas->passes != nullptr &&
        CreateBuffer(sim, 4u * MVFX_GAS_WORDS, numbers, &gas->buffers[GAS_NUMBERS]) &&
        CreateBuffer(sim, 4u * fields, written | mrhi_bufferCopySource,
                     &gas->buffers[GAS_FIELDS]) &&
        CreateBuffer(sim, 4u * (solve > 0 ? solve : 1u), written, &gas->buffers[GAS_SOLVE]) &&
        CreateBuffer(sim, 4u * (gas->grids.words > 0 ? gas->grids.words : 1u), written,
                     &gas->buffers[GAS_GRIDS]) &&
        CreateBuffer(sim, 4u * MVFX_GPU_GAS_STATISTICS_WORDS, written | mrhi_bufferCopySource,
                     &gas->buffers[GAS_STATISTICS]);
    return created && mvfxCreateSimGasReads(sim, gas) && mvfxCreateSimSurfaceMesh(sim, gas);
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
    mvfxDestroySimGasReads(sim, gas);
    mvfxDestroySimSurfaceMesh(sim, gas);
    mvfxDestroySimSplats(sim, gas);
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

// A substep's numbers: its span, the keeps over it, whether the sources
// pour, its start after the plan's; the burn's share and the divergence a
// unit burned asks for, as the core's mvfxGasBurn makes them.
static void SubstepNumbers(SimGas* gas, const mvfxGasPlan* plan, int32_t s, int64_t start,
                           int64_t end)
{
    const mvfxGasDef* def = &gas->view.def;
    float h = (float)((double)(end - start) / (double)MVFX_TICKS_PER_SECOND);
    uint32_t* step = gas->words + WORD_SUBSTEPS + SUBSTEP_WORDS * (uint32_t)s;
    float keep = (float)mvfxExpMinus((double)(def->velocityDecay * h));
    float since = (float)((double)(start - plan->start) / (double)MVFX_TICKS_PER_SECOND);
    mvfxFloatWords(step, h, keep, start < plan->stopTick ? 1.0f : 0.0f, since);
    mvfxFloatWords(step + 4, (float)mvfxExpMinus((double)(def->densityDecay * h)),
                   (float)mvfxExpMinus((double)(def->temperatureDecay * h)),
                   (float)mvfxExpMinus((double)(def->fuelDecay * h)), 0.0f);
    float share = 1.0f - (float)mvfxExpMinus((double)(def->burnRate * h));
    mvfxFloatWords(step + 8, share, def->burnExpansion * def->cellSize / h, 0.0f, 0.0f);
}

// A substep's dispatches from buffer now current; the buffer current after.
static uint32_t SubstepPasses(SimGas* gas, uint32_t u, uint32_t now)
{
    const mvfxGasDef* def = &gas->view.def;
    uint32_t cells = Groups((uint32_t)gas->view.cells);
    uint32_t faces = Groups(AllFaces(&gas->view, 0));
    Push(gas, mvfx_kernelGasPourCells, u, now, 0, 0, cells);
    if (gas->splatCount > 0)
    {
        // The sources' faces, the splats, then what follows the sources.
        float h;
        memcpy(&h, &gas->words[WORD_SUBSTEPS + SUBSTEP_WORDS * u], sizeof h);
        Push(gas, mvfx_kernelGasFaces, u, now, 0, FACES_SOURCES, faces);
        mvfxSimSplatSubstep(gas, u, h, now, cells, faces);
    }
    Push(gas, mvfx_kernelGasFaces, u, now, 0, gas->splatCount > 0 ? FACES_AFTER : FACES_ALL, faces);
    if (def->vorticity != 0.0f)
    {
        Push(gas, mvfx_kernelGasCurl, u, now, 0, 0, cells);
        Push(gas, mvfx_kernelGasConfine, u, now, 0, 0, faces);
    }
    Carry(gas, def, u, now, faces, cells);
    now = 1u - now;
    if (HasObstacles(def))
    {
        Push(gas, mvfx_kernelGasEmpty, u, now, 0, 0, cells);
    }
    if (def->burnRate != 0.0f)
    {
        Push(gas, mvfx_kernelGasBurn, u, now, 0, 0, cells);
    }
    if (HasObstacles(def))
    {
        Push(gas, mvfx_kernelGasMove, u, now, 0, 0, faces);
    }
    mvfxSimGasProject(gas, u, now);
    return now;
}

void mvfxSimGasProject(SimGas* gas, uint32_t u, uint32_t now)
{
    const mvfxGasDef* def = &gas->view.def;
    uint32_t cells = Groups((uint32_t)gas->view.cells);
    uint32_t faces = Groups(AllFaces(&gas->view, 0));
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

// The step's obstacles as the plan gives them: their count, those that
// moved (over a step of some length), the step's seconds; each with its
// grid where the grids' buffer holds it, and as it was before.
static void ObstacleNumbers(SimGas* gas, const mvfxGasPlan* plan)
{
    uint32_t* w = gas->words + WORD_OBSTACLES;
    memset(w, 0, (BEFORE_WORDS + 2u * MVFX_MAX_COLLIDERS * MVFX_COLLIDER_WORDS) * sizeof(uint32_t));
    w[0] = (uint32_t)plan->obstacleCount;
    float seconds = (float)((double)plan->ticks / (double)MVFX_TICKS_PER_SECOND);
    memcpy(&w[2], &seconds, sizeof seconds);
    const uint32_t none[5] = {0};
    for (int32_t c = 0; c < plan->obstacleCount; ++c)
    {
        uint32_t grid[5];
        mvfxColliderGridWords(&gas->grids, plan->obstacles, plan->ownObstacleCount, c, grid);
        mvfxColliderWords(&plan->obstacles[c], grid, w + 4u + MVFX_COLLIDER_WORDS * (uint32_t)c);
        bool moved = plan->obstacleMoved[c] && plan->ticks > 0;
        w[1] |= moved ? 1u << (uint32_t)c : 0u;
        if (moved)
        {
            mvfxColliderWords(&plan->obstaclesBefore[c], none,
                              w + BEFORE_WORDS + MVFX_COLLIDER_WORDS * (uint32_t)c);
        }
    }
}

// The step's substeps' numbers and dispatches, as the core's mvfxStepGas
// splits the plan's ticks.
static void Plan(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan)
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
        gas->prepared = false;
    }
    uint32_t now = plan->cleared ? 0u : gas->now;
    // Obstacles mark the solids each step, and the levels' areas follow.
    bool solids = HasObstacles(def);
    if (solids)
    {
        ObstacleNumbers(gas, plan);
        Push(gas, mvfx_kernelGasMark, 0, now, 0, 0, Groups((uint32_t)view->cells));
    }
    for (int32_t l = 0; (!gas->prepared || solids) && l < view->levelCount; ++l)
    {
        uint32_t faces = AllFaces(view, l);
        Push(gas, mvfx_kernelGasAreas, 0, 0, (uint32_t)l, 0, Groups(faces));
        Push(gas, mvfx_kernelGasCoefficients, 0, 0, (uint32_t)l, 0,
             Groups(faces + LevelCells(view, l)));
    }
    int64_t from = plan->start;
    int64_t to = plan->start + plan->ticks;
    if (def->kind == mvfx_gasSurface)
    {
        // Its field from its emitter's particles, into the first buffer.
        mvfxPlanSimSurface(sim, gas, plan);
        now = 0;
    }
    else if (def->kind == mvfx_gasLiquid)
    {
        // One step about the projection, as the core's (sim_liquid_gas.c);
        // its read back says whether it holds anything.
        if (to > from)
        {
            SubstepNumbers(gas, plan, 0, from, to);
            mvfxPlanSimLiquid(sim, gas, plan, now);
        }
    }
    else
    {
        mvfxPlanSimSplats(sim, gas, plan);
        for (int32_t s = 0; s < def->substeps; ++s)
        {
            int64_t start = from + (to - from) * s / def->substeps;
            int64_t end = from + (to - from) * (s + 1) / def->substeps;
            if (end <= start)
            {
                continue;
            }
            SubstepNumbers(gas, plan, s, start, end);
            now = SubstepPasses(gas, (uint32_t)s, now);
        }
        // Whether anything is held at the step's end, into the record whose
        // write starts it at nothing held at the end's tick.
        Push(gas, mvfx_kernelGasHolds, 0, now, 0, 0, Groups((uint32_t)view->cells));
    }
    uint64_t end = (uint64_t)(plan->start + plan->ticks);
    gas->record[MVFX_GPU_GAS_HOLDS] = 0;
    gas->record[MVFX_GPU_GAS_FASTEST] = 0;
    gas->record[MVFX_GPU_GAS_TICK] = (uint32_t)end;
    gas->record[MVFX_GPU_GAS_TICK + 1] = (uint32_t)(end >> 32u);
    gas->after = now;
}

bool mvfxImportSimGas(mvfxGpuSim* sim, SimGas* gas)
{
    for (int32_t b = 0; !gas->imported && b < GAS_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, gas->buffers[b], &gas->resources[b]) != mrhi_success)
        {
            return false;
        }
    }
    gas->imported = true;
    return true;
}

mvfxResult mvfxAddSimGasPasses(mvfxGpuSim* sim, int32_t g)
{
    SimGas* gas = &sim->gases[g];
    // The reads, samples and meshes of a frame the host dropped go.
    gas->added = false;
    gas->readCount = 0;
    gas->sampleCount = 0;
    gas->meshCount = 0;
    gas->imported = false;
    static mvfxGasPlan plan;
    mvfxResult result = mvfxSimGasPlan(sim, g, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    if (HasObstacles(&gas->view.def) &&
        !mvfxPlaceHostGrids(sim->context, plan.obstacles, plan.obstacleCount, plan.ownObstacleCount,
                            &gas->grids))
    {
        return mvfx_errorCapacity;
    }
    Plan(sim, gas, &plan);
    mrhiResourceId* r = gas->resources;
    if (!mvfxImportSimGas(sim, gas) || !mvfxImportSimSplats(sim, gas))
    {
        return mvfx_errorState;
    }
    const mrhiAccess written[GAS_BUFFERS] = {
        {.resource = r[GAS_NUMBERS], .kind = mrhi_accessCopyDestination},
        {.resource = r[GAS_FIELDS], .kind = mrhi_accessCopyDestination},
        {.resource = r[GAS_SOLVE], .kind = mrhi_accessCopyDestination},
        {.resource = r[GAS_GRIDS], .kind = mrhi_accessCopyDestination},
        {.resource = r[GAS_STATISTICS], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[GAS_BUFFERS] = {
        {.resource = r[GAS_NUMBERS], .kind = mrhi_accessUniform},
        {.resource = r[GAS_FIELDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[GAS_SOLVE], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[GAS_GRIDS], .kind = mrhi_accessStorageRead},
        {.resource = r[GAS_STATISTICS], .kind = mrhi_accessStorageReadWrite}};
    // The write pass's accesses with the splats' after the gas's.
    const mrhiAccess* writes = written;
    int32_t writeCount = GAS_BUFFERS;
    if (gas->splatCount > 0 || gas->hasLiquid)
    {
        memcpy(gas->writes, written, sizeof written);
        writeCount += mvfxSimSplatWrites(gas, gas->writes + GAS_BUFFERS);
        writes = gas->writes;
    }
    for (int32_t p = -1; p < gas->count; ++p)
    {
        mrhiAccess accesses[GAS_BUFFERS + SPLAT_BUFFERS];
        int32_t count = p < 0 ? 0 : mvfxSimSplatAccesses(gas, &gas->dispatches[p], used, accesses);
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = p < 0 ? mrhi_passTransfer : mrhi_passGraphics;
        def.accesses = p < 0 ? writes : accesses;
        def.accessCount = p < 0 ? (uint32_t)writeCount : (uint32_t)count;
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
    recorded = recorded && mvfxUpload(sim->device, &sim->uploaded, pass, r[GAS_STATISTICS], 0,
                                      gas->record, sizeof gas->record);
    recorded = recorded && mvfxWriteSimSplats(sim, pass, gas);
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records the step's dispatches.
static bool RecordStep(mvfxGpuSim* sim, SimGas* gas)
{
    bool recorded = RecordWrite(sim, gas);
    const mrhiResourceId* r = gas->resources;
    mrhiBinding own[GAS_BUFFERS];
    for (int32_t b = 0; b < GAS_BUFFERS; ++b)
    {
        own[b] = (mrhiBinding){.slot = (uint32_t)b, .resource = r[b], .size = MRHI_WHOLE_SIZE};
    }
    for (int32_t p = 0; recorded && p < gas->count; ++p)
    {
        const GasDispatch* d = &gas->dispatches[p];
        mrhiPassId pass = gas->passes[p + 1];
        mrhiBinding splat[GAS_BUFFERS + 4];
        bool owns = d->binds == GAS_BINDS_GAS;
        uint32_t count = owns ? GAS_BUFFERS : (uint32_t)mvfxSimSplatBindings(gas, d, splat);
        bool ran =
            mrhiBeginPass(sim->device, pass) == mrhi_success &&
            mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, d->kernel)) ==
                mrhi_success &&
            mrhiSetBindings(sim->device, pass, 0, owns ? own : splat, count) == mrhi_success &&
            mrhiSetRootBlock(sim->device, pass, 0, d->root, sizeof d->root) == mrhi_success &&
            Dispatch(sim, pass, d->groups);
        recorded = mrhiEndPass(sim->device, pass) == mrhi_success && ran;
    }
    gas->prepared = gas->prepared || recorded;
    return recorded;
}

bool mvfxRecordSimGas(mvfxGpuSim* sim, SimGas* gas)
{
    bool recorded = !gas->added || RecordStep(sim, gas);
    gas->now = gas->added ? gas->after : gas->now;
    gas->added = false;
    recorded = recorded && mvfxRecordSimGasReads(sim, gas) && mvfxRecordSimSurfaceMeshes(sim, gas);
    gas->readCount = 0;
    gas->sampleCount = 0;
    gas->meshCount = 0;
    gas->imported = false;
    return recorded;
}

int32_t mvfxGpuSim_GasPasses(const mvfxGpuSim* sim, int32_t gas)
{
    if (sim == nullptr || gas < 0 || gas >= sim->gasCount)
    {
        return 0;
    }
    return mvfxSimGasPasses(&sim->gases[gas].view, sim->gases[gas].splatRoom);
}

mvfxResult mvfxGpuSim_GetGas(const mvfxGpuSim* sim, int32_t gas, mvfxGpuGas* gasOut)
{
    if (gasOut != nullptr)
    {
        *gasOut = (mvfxGpuGas){0};
    }
    if (sim == nullptr || gasOut == nullptr || gas < 0 || gas >= sim->gasCount)
    {
        return mvfx_errorInvalid;
    }
    *gasOut = (mvfxGpuGas){.statistics = sim->gases[gas].buffers[GAS_STATISTICS]};
    return mvfx_success;
}

mvfxResult mvfxGpuSim_ReportGas(const mvfxGpuSim* sim, mvfxContext* context, int32_t gas,
                                const uint32_t* record)
{
    if (sim == nullptr || context == nullptr || context != sim->context || gas < 0 ||
        gas >= sim->gasCount || record == nullptr)
    {
        return mvfx_errorInvalid;
    }
    uint64_t tick = (uint64_t)record[MVFX_GPU_GAS_TICK] | (uint64_t)record[MVFX_GPU_GAS_TICK + 1]
                                                              << 32u;
    mvfxResult result = mvfxInstance_SetGasHolds(context, sim->instance, gas, (int64_t)tick,
                                                 record[MVFX_GPU_GAS_HOLDS] != 0);
    if (result != mvfx_success || sim->gases[gas].view.def.kind != mvfx_gasLiquid)
    {
        return result;
    }
    // The fastest speed from its square's bits: the core squares it again,
    // and the root of that square is this root (binary floating point's
    // correctly rounded square root and product), so its split is the
    // CPU path's from the same particles.
    float square;
    memcpy(&square, &record[MVFX_GPU_GAS_FASTEST], sizeof square);
    return mvfxInstance_SetLiquidSpeed(context, sim->instance, gas, (int64_t)tick, sqrtf(square));
}
