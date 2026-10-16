// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A liquid gas's step (APIC: Jiang, Schroeder, Selle, Teran and
// Stomakhin, "The Affine Particle-In-Cell Method", SIGGRAPH 2015). The
// particles, as their emitter's step left them, are mapped into the box
// and sorted by cell; cells holding one are liquid. Each face gathers the
// momentum of the particles about it, v + C (x_face - x_p) weighted
// trilinearly, over the weights; the velocity is projected with air cells
// at pressure 0 (Bridson 2015, chapter 5), extended two layers into the
// air, each a gather of the faces already set (chapter 4), and each
// particle reads back its velocity and, as its affine matrix, the
// gradient of the trilinear interpolant (Jiang et al., section 5.3).

#include "gas_liquid.h"

#include "direction.h"
#include "gas.h"
#include "gas_api.h"
#include "gas_lattice.h"
#include "gas_pressure.h"
#include "gas_solid.h"
#include "space.h"

#include <math.h>
#include <string.h>

// The least particles or rows a task's range takes.
#define LIQUID_RANGE 64
#define ROW_RANGE    8

// The layers the velocity is extended into the air.
#define EXTEND_LAYERS 2

// A liquid gas's step: the gas and its room, its lattice, the emitter's
// particles and the map from their space into the box.
typedef struct Step
{
    const mvfxCompiledGas* gas;
    mvfxGasState* state;
    mvfxLiquidGasRoom* room;
    mvfxGasLattice lattice;
    float* x[3];
    float* v[3];
    float* affine;
    size_t stride;
    int32_t count;
    mvfxSpaceMap map;
    // The faces' arrays now, and each axis's first face in the validity
    // buffers.
    float* faces[3];
    int64_t validAt[3];
} Step;

// A room array of the particles: place (0 to 2), velocity (3 to 5) and
// affine matrix (6 to 14).
static float* Local(const Step* s, int32_t a)
{
    return s->room->local + (size_t)a * (size_t)s->room->capacity;
}

// A matrix turned by q: q M q^-1, column by column.
static void Conjugate(const float q[4], const float m[9], float out[9])
{
    const float back[4] = {-q[0], -q[1], -q[2], q[3]};
    for (int32_t j = 0; j < 3; ++j)
    {
        const float axis[3] = {j == 0 ? 1.0f : 0.0f, j == 1 ? 1.0f : 0.0f, j == 2 ? 1.0f : 0.0f};
        float in[3];
        mvfxRotate(back, axis, in);
        const float column[3] = {m[0] * in[0] + m[1] * in[1] + m[2] * in[2],
                                 m[3] * in[0] + m[4] * in[1] + m[5] * in[2],
                                 m[6] * in[0] + m[7] * in[1] + m[8] * in[2]};
        float turned[3];
        mvfxRotate(q, column, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            out[3 * k + j] = turned[k];
        }
    }
}

// Particle i's cell, -1 outside the box (a 2D gas's Z not judged).
static int64_t CellOf(const Step* s, int32_t i)
{
    const int32_t* n = s->lattice.n;
    int64_t cell = 0;
    for (int32_t k = 2; k >= 0; --k)
    {
        float p = Local(s, k)[i];
        if (n[k] > 1 && !(p >= 0.0f && p < (float)n[k]))
        {
            return -1;
        }
        int32_t c = n[k] > 1 ? (int32_t)p : 0;
        cell = cell * n[k] + c;
    }
    return cell;
}

static void MapRange(int32_t start, int32_t end, void* context)
{
    const Step* s = context;
    const mvfxGasLattice* l = &s->lattice;
    for (int32_t i = start; i < end; ++i)
    {
        const float x[3] = {s->x[0][i], s->x[1][i], s->x[2][i]};
        const float u[3] = {s->v[0][i], s->v[1][i], s->v[2][i]};
        float local[3];
        float velocity[3];
        float affine[9];
        mvfxMapPoint(&s->map, x, local);
        mvfxMapVector(&s->map, u, s->map.sigma, velocity);
        Conjugate(s->map.rotation, s->affine + (size_t)9 * (size_t)i, affine);
        for (int32_t k = 0; k < 3; ++k)
        {
            Local(s, k)[i] = local[k] / l->cell + 0.5f * (float)l->n[k];
            Local(s, 3 + k)[i] = velocity[k];
        }
        for (int32_t a = 0; a < 9; ++a)
        {
            Local(s, 6 + a)[i] = affine[a];
        }
    }
}

// The particles in the box by cell, in their order within a cell.
static void Sort(const Step* s)
{
    int64_t cells = s->gas->cells;
    int32_t* start = s->room->cellStart;
    memset(start, 0, (size_t)(cells + 1) * sizeof(int32_t));
    for (int32_t i = 0; i < s->count; ++i)
    {
        int64_t c = CellOf(s, i);
        start[c >= 0 ? c : cells] += c >= 0 ? 1 : 0;
    }
    // Each cell's end, then filled from the back: each cell's start.
    int32_t total = 0;
    for (int64_t c = 0; c < cells; ++c)
    {
        total += start[c];
        start[c] = total;
    }
    start[cells] = total;
    for (int32_t i = s->count - 1; i >= 0; --i)
    {
        int64_t c = CellOf(s, i);
        if (c >= 0)
        {
            s->room->order[--start[c]] = i;
        }
    }
}

// The liquid cells of each level and each cell's filled share into the
// density, the other scalars 0; volume is a particle's share of a cell.
// A cell filled past full asks the projection to grow it back to full
// over the gas's fill time, or over the step if longer, into expansion
// (Losasso et al. 2008 correct a liquid's density through the divergence
// the same way): particles bunch where the flow converges, and without it
// a dam break's cells lost a quarter of their filled volume in a second.
// The particles carry the growth through the next step, so a share of
// the step alone would fling them after a short one (a spawning tick).
// A coarse cell is liquid only when all its children are: counted liquid
// when any child was, a coarse cell solved for air its children did not
// hold, and the cycles diverged within ten steps of a dam break.
static void Mark(const Step* s, float volume, float* expansion)
{
    const mvfxCompiledGas* gas = s->gas;
    const int32_t* start = s->room->cellStart;
    uint8_t* liquid = s->room->liquid[0];
    int32_t now = s->state->current;
    for (int64_t c = 0; c < gas->cells; ++c)
    {
        int32_t held = start[c + 1] - start[c];
        bool solid = s->state->solid != nullptr && s->state->solid[c] != 0;
        liquid[c] = held > 0 && !solid ? 1 : 0;
        float fill = (float)held * volume;
        s->state->scalars[0][now][c] = fminf(fill, 1.0f);
        float growth = fmaxf(s->lattice.h, gas->def.fillTime);
        expansion[c] =
            liquid[c] != 0 && fill > 1.0f ? (fill - 1.0f) * s->lattice.cell / growth : 0.0f;
        s->state->scalars[1][now][c] = 0.0f;
        s->state->scalars[2][now][c] = 0.0f;
    }
    for (int32_t l = 1; l < gas->levelCount; ++l)
    {
        const int32_t* fine = gas->levels[l - 1];
        const int32_t* n = gas->levels[l];
        memset(s->room->liquid[l], 1, (size_t)mvfxGasLevelCells(gas, l));
        int64_t i = 0;
        for (int32_t z = 0; z < fine[2]; ++z)
        {
            for (int32_t y = 0; y < fine[1]; ++y)
            {
                for (int32_t x = 0; x < fine[0]; ++x, ++i)
                {
                    int64_t c = ((int64_t)(z / 2) * n[1] + y / 2) * n[0] + x / 2;
                    s->room->liquid[l][c] &= s->room->liquid[l - 1][i];
                }
            }
        }
    }
}

// Particle i's trilinear weight at a point (in cells), over the axes the
// gas has; 0 a cell away or more.
static float WeightAt(const Step* s, int32_t i, const float q[3])
{
    float w = 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = fabsf(Local(s, k)[i] - q[k]);
        w *= s->lattice.n[k] > 1 ? fmaxf(1.0f - d, 0.0f) : 1.0f;
    }
    return w;
}

// The momentum and weight the particles of cell c give a face of axis k
// at q, added into sums.
static void GatherCell(const Step* s, int64_t c, int32_t k, const float q[3], float sums[2])
{
    const int32_t* start = s->room->cellStart;
    for (int32_t e = start[c]; e < start[c + 1]; ++e)
    {
        int32_t i = s->room->order[e];
        float w = WeightAt(s, i, q);
        if (w > 0.0f)
        {
            float u = Local(s, 3 + k)[i];
            for (int32_t j = 0; j < 3; ++j)
            {
                u += Local(s, 6 + 3 * k + j)[i] * (q[j] - Local(s, j)[i]) * s->lattice.cell;
            }
            sums[0] += w * u;
            sums[1] += w;
        }
    }
}

// The face of axis k at at: its velocity gathered from the cells about it
// (two across k, three along the others, held in the box), in a fixed
// order; whether any particle reached it.
static bool GatherFace(const Step* s, int32_t k, const int32_t at[3], float* out)
{
    const int32_t* n = s->lattice.n;
    float q[3];
    int32_t low[3];
    int32_t high[3];
    for (int32_t j = 0; j < 3; ++j)
    {
        q[j] = (float)at[j] + (j == k ? 0.0f : 0.5f);
        low[j] = at[j] - 1;
        high[j] = j == k ? at[j] : at[j] + 1;
        low[j] = low[j] < 0 ? 0 : low[j];
        high[j] = high[j] > n[j] - 1 ? n[j] - 1 : high[j];
    }
    float sums[2] = {0.0f, 0.0f};
    for (int32_t z = low[2]; z <= high[2]; ++z)
    {
        for (int32_t y = low[1]; y <= high[1]; ++y)
        {
            for (int32_t x = low[0]; x <= high[0]; ++x)
            {
                GatherCell(s, ((int64_t)z * n[1] + y) * n[0] + x, k, q, sums);
            }
        }
    }
    *out = sums[1] > 0.0f ? sums[0] / sums[1] : 0.0f;
    return sums[1] > 0.0f;
}

// A rows pass over one axis's faces.
typedef struct Rows
{
    const Step* step;
    int32_t k;
    int32_t m[3];
} Rows;

static void GatherRows(int32_t start, int32_t end, void* context)
{
    const Rows* r = context;
    const Step* s = r->step;
    uint8_t* valid = s->room->valid[0] + s->validAt[r->k];
    for (int32_t row = start; row < end; ++row)
    {
        int64_t f = (int64_t)row * r->m[0];
        for (int32_t x = 0; x < r->m[0]; ++x, ++f)
        {
            const int32_t at[3] = {x, row % r->m[1], row / r->m[1]};
            valid[f] = GatherFace(s, r->k, at, &s->faces[r->k][f]) ? 1 : 0;
        }
    }
}

// Runs a rows pass over each axis's faces.
static void EachAxis(const Step* s, mvfxTaskFn* pass, const mvfxTasks* tasks)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        Rows rows = {.step = s, .k = k};
        mvfxGasPoints(&s->lattice, k, rows.m);
        if (s->gas->faces[k] > 0)
        {
            mvfxRunTasks(tasks, pass, rows.m[1] * rows.m[2], ROW_RANGE, &rows);
        }
    }
}

// Whether the cell at (cell coordinates) is liquid; false outside the box.
static bool IsLiquidAt(const Step* s, const int32_t at[3])
{
    const int32_t* n = s->lattice.n;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (at[k] < 0 || at[k] >= n[k])
        {
            return false;
        }
    }
    return s->room->liquid[0][((int64_t)at[2] * n[1] + at[1]) * n[0] + at[0]] != 0;
}

static void LiquidFaceRows(int32_t start, int32_t end, void* context)
{
    const Rows* r = context;
    const Step* s = r->step;
    uint8_t* valid = s->room->valid[0] + s->validAt[r->k];
    for (int32_t row = start; row < end; ++row)
    {
        int64_t f = (int64_t)row * r->m[0];
        for (int32_t x = 0; x < r->m[0]; ++x, ++f)
        {
            int32_t at[3] = {x, row % r->m[1], row / r->m[1]};
            bool above = IsLiquidAt(s, at);
            at[r->k] -= 1;
            bool set = above || IsLiquidAt(s, at);
            valid[f] = set ? 1 : 0;
            s->faces[r->k][f] = set ? s->faces[r->k][f] : 0.0f;
        }
    }
}

// The mean of the set faces beside face f of axis k (along each axis the
// gas has), into out; whether any was set.
static bool MeanAround(const Rows* r, const uint8_t* valid, const int32_t at[3], float* out)
{
    const Step* s = r->step;
    const int64_t step[3] = {1, r->m[0], (int64_t)r->m[0] * r->m[1]};
    int64_t f = ((int64_t)at[2] * r->m[1] + at[1]) * r->m[0] + at[0];
    float sum = 0.0f;
    int32_t count = 0;
    for (int32_t j = 0; j < 3; ++j)
    {
        for (int32_t side = -1; side <= 1; side += 2)
        {
            int32_t next = at[j] + side;
            if (next < 0 || next >= r->m[j] || s->lattice.n[j] == 1)
            {
                continue;
            }
            int64_t g = f + side * step[j];
            sum += valid[g] != 0 ? s->faces[r->k][g] : 0.0f;
            count += valid[g] != 0 ? 1 : 0;
        }
    }
    *out = count > 0 ? sum / (float)count : 0.0f;
    return count > 0;
}

static void ExtendRows(int32_t start, int32_t end, void* context)
{
    const Rows* r = context;
    const Step* s = r->step;
    const uint8_t* valid = s->room->valid[0] + s->validAt[r->k];
    uint8_t* next = s->room->valid[1] + s->validAt[r->k];
    float* out = s->state->velocity[r->k][1 - s->state->current];
    for (int32_t row = start; row < end; ++row)
    {
        int64_t f = (int64_t)row * r->m[0];
        for (int32_t x = 0; x < r->m[0]; ++x, ++f)
        {
            const int32_t at[3] = {x, row % r->m[1], row / r->m[1]};
            float mean = 0.0f;
            bool set = valid[f] == 0 && MeanAround(r, valid, at, &mean);
            out[f] = valid[f] != 0 ? s->faces[r->k][f] : mean;
            next[f] = valid[f] != 0 || set ? 1 : 0;
        }
    }
}

// The velocity extended EXTEND_LAYERS layers into the air, each layer a
// gather of the faces set before it, through the other buffer.
static void Extend(Step* s, const mvfxTasks* tasks)
{
    int64_t faces = s->gas->faces[0] + s->gas->faces[1] + s->gas->faces[2];
    for (int32_t layer = 0; layer < EXTEND_LAYERS; ++layer)
    {
        EachAxis(s, ExtendRows, tasks);
        for (int32_t k = 0; k < 3; ++k)
        {
            memcpy(s->faces[k], s->state->velocity[k][1 - s->state->current],
                   (size_t)s->gas->faces[k] * sizeof(float));
        }
        memcpy(s->room->valid[0], s->room->valid[1], (size_t)faces);
    }
    mvfxCloseGasWalls(s->gas, s->faces);
}

// The trilinear interpolant of axis k's faces at p (in cells): its value,
// and its gradient in the box's lengths into gradient.
static float Interpolate(const Step* s, int32_t k, const float p[3], float gradient[3])
{
    const mvfxGasLattice* l = &s->lattice;
    int32_t m[3];
    mvfxGasPoints(l, k, m);
    int32_t low[3];
    int32_t high[3];
    float t[3];
    bool inner[3];
    for (int32_t j = 0; j < 3; ++j)
    {
        float c = p[j] - (j == k ? 0.0f : 0.5f);
        float held = fminf(fmaxf(c, 0.0f), (float)(m[j] - 1));
        low[j] = (int32_t)held;
        high[j] = low[j] + 1 < m[j] ? low[j] + 1 : low[j];
        t[j] = held - (float)low[j];
        inner[j] = held == c && high[j] > low[j] && l->n[j] > 1;
    }
    const float* f = s->faces[k];
    float value = 0.0f;
    gradient[0] = gradient[1] = gradient[2] = 0.0f;
    for (int32_t corner = 0; corner < 8; ++corner)
    {
        const int32_t bit[3] = {corner & 1, corner >> 1 & 1, corner >> 2};
        int32_t at[3];
        float w[3];
        for (int32_t j = 0; j < 3; ++j)
        {
            at[j] = bit[j] != 0 ? high[j] : low[j];
            w[j] = bit[j] != 0 ? t[j] : 1.0f - t[j];
        }
        float u = f[((int64_t)at[2] * m[1] + at[1]) * m[0] + at[0]];
        value += w[0] * w[1] * w[2] * u;
        for (int32_t j = 0; j < 3; ++j)
        {
            float slope = bit[j] != 0 ? 1.0f : -1.0f;
            float others = w[(j + 1) % 3] * w[(j + 2) % 3];
            gradient[j] += inner[j] ? slope * others * u / l->cell : 0.0f;
        }
    }
    return value;
}

static void ReadRange(int32_t start, int32_t end, void* context)
{
    const Step* s = context;
    for (int32_t i = start; i < end; ++i)
    {
        if (CellOf(s, i) < 0)
        {
            continue;
        }
        const float p[3] = {Local(s, 0)[i], Local(s, 1)[i], Local(s, 2)[i]};
        float velocity[3];
        float affine[9];
        for (int32_t k = 0; k < 3; ++k)
        {
            velocity[k] = Interpolate(s, k, p, &affine[3 * k]);
        }
        float u[3];
        mvfxUnmapVector(&s->map, velocity, u);
        const float back[4] = {-s->map.rotation[0], -s->map.rotation[1], -s->map.rotation[2],
                               s->map.rotation[3]};
        Conjugate(back, affine, s->affine + (size_t)9 * (size_t)i);
        for (int32_t k = 0; k < 3; ++k)
        {
            s->v[k][i] = u[k];
        }
    }
}

// The step's view of liquid gas g and its emitter's particles.
static Step StepOf(mvfxInstance* instance, int32_t g, double seconds)
{
    const mvfxCompiledGas* gas = &instance->effect->gases[g];
    int32_t e = gas->emitter;
    mvfxEmitterState* emitterState = &instance->emitters[e];
    mvfxGasState* state = &instance->gases[g];
    Step s = {.gas = gas,
              .state = state,
              .room = &state->room,
              .lattice = {.n = {gas->def.size[0], gas->def.size[1], gas->def.size[2]},
                          .cell = gas->def.cellSize,
                          .h = (float)seconds},
              .affine = mvfxAttributeArray(instance, e, mvfx_attributeAffine),
              .count = emitterState->count,
              .map = mvfxGasParticleMap(instance, g, e)};
    int64_t at = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        s.x[k] = mvfxAttributeArray(instance, e, (mvfxAttribute)(mvfx_attributePositionX + k));
        s.v[k] = mvfxAttributeArray(instance, e, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        s.faces[k] = state->velocity[k][state->current];
        s.validAt[k] = at;
        at += gas->faces[k];
    }
    return s;
}

// The cells liquid gas g's fastest particle would cross in seconds.
static float Crossing(const mvfxInstance* instance, int32_t g, float seconds)
{
    const mvfxCompiledGas* gas = &instance->effect->gases[g];
    int32_t e = gas->emitter;
    const float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = mvfxAttributeArray(instance, e, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    float fastest = 0.0f;
    for (int32_t i = 0; i < instance->emitters[e].count; ++i)
    {
        fastest = fmaxf(fastest, v[0][i] * v[0][i] + v[1][i] * v[1][i] + v[2][i] * v[2][i]);
    }
    const mvfxCompiledEmitter* emitter = &instance->effect->emitters[e];
    const float* gravity = emitter->gravity;
    mvfxSpaceMap pull = mvfxSpaceMapAt(instance, e, emitter->gravitySpace, &instance->to);
    float g2 = gravity[0] * gravity[0] + gravity[1] * gravity[1] + gravity[2] * gravity[2];
    // Bridson's speed: the fastest particle's, and gravity's from rest over
    // five cells, sqrt(5 h |g|), in the particles' lengths.
    float toGas = mvfxGasParticleMap(instance, g, e).sigma;
    float cell = gas->def.cellSize / toGas;
    float speed = sqrtf(fastest) + sqrtf(5.0f * cell * sqrtf(g2) * pull.sigma);
    return speed * seconds / cell;
}

int32_t mvfxLiquidSubsteps(const mvfxInstance* instance, int64_t ticks)
{
    float seconds = (float)((double)ticks / (double)MVFX_TICKS_PER_SECOND);
    float most = 1.0f;
    for (int32_t g = 0; g < instance->effect->gasCount; ++g)
    {
        const mvfxCompiledGas* gas = &instance->effect->gases[g];
        if (gas->def.kind == mvfx_gasLiquid)
        {
            most = fmaxf(most, ceilf(Crossing(instance, g, seconds) / gas->def.courant));
        }
    }
    return (int32_t)fminf(most, (float)MVFX_MAX_LIQUID_SUBSTEPS);
}

void mvfxStepLiquidGas(mvfxInstance* instance, int32_t g, const mvfxTasks* tasks)
{
    double seconds =
        (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND;
    if (!(seconds > 0.0))
    {
        return;
    }
    Step s = StepOf(instance, g, seconds);
    const mvfxCompiledEmitter* emitter = &instance->effect->emitters[s.gas->emitter];
    mvfxRunTasks(tasks, MapRange, s.count, LIQUID_RANGE, &s);
    Sort(&s);
    // A particle's share of a cell: a cube, or a square, of its spacing
    // (in the gas's lengths).
    float spacing = emitter->liquid.spacing * s.map.sigma / s.lattice.cell;
    float volume = s.lattice.n[2] > 1 ? spacing * spacing * spacing : spacing * spacing;
    // The finest level's scratch is free until the solve's cycles.
    float* expansion = s.state->levels[0].scratch;
    if (s.state->solid != nullptr)
    {
        mvfxMarkGasSolids(s.gas, s.state);
    }
    Mark(&s, volume, expansion);
    EachAxis(&s, GatherRows, tasks);
    if (s.state->solid != nullptr)
    {
        mvfxMoveGasSolids(s.gas, s.state, seconds, s.faces);
    }
    mvfxProjectGas(s.gas, s.state, expansion, tasks);
    EachAxis(&s, LiquidFaceRows, tasks);
    Extend(&s, tasks);
    mvfxRunTasks(tasks, ReadRange, s.count, LIQUID_RANGE, &s);
    s.state->holds = s.count > 0;
}
