// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shallow water on a staggered grid (Chentanez and Muller, "Real-Time
// Simulation of Large Bodies of Water with Small Scale Details", SCA
// 2010; Bridson, "Fluid Simulation for Computer Graphics", 2nd ed., its
// shallow water chapter): each cell a depth over its ground, each face a
// velocity across it. A substep advects the faces' velocities
// semi-Lagrangian, accelerates each by the surface's fall across it,
// g dh / l, held within 0.5 l / dt, and stops a dry face: one whose water
// over its sill, max(0, max(surfaces) - max(grounds)) (Audusse et al.,
// "A fast and stable well-balanced scheme with hydrostatic reconstruction
// for shallow water flows", SIAM J. Sci. Comput. 2004), is below 1e-6, so
// a lake at rest stays still over any ground. Depths move by upwind
// fluxes, the velocity times the water over the sill on the giver's side
// times l, a giver's outflows scaled by K = min(1, d l^2 / (dt sum)) so no
// depth falls below 0. Every pass is a per-cell or per-face gather into
// another buffer, a face's flux computed alike for both its cells, so the
// volume is exact in a closed basin to rounding and the same on every
// thread count.

#include "water_shallow.h"

#include "elementary.h"
#include "water.h"
#include "water_ground.h"

#include <math.h>

// The least rows a task's range takes.
#define ROW_RANGE 8

// Water over a face's sill at most this leaves it dry.
#define DRY 1e-6f

// A substep's view of a surface: its cells, its walls, the substep and
// gravity, and its buffers: the depths it began with and the next, the
// faces' velocities it began with and the next, the givers' scales, the
// ground and each row's faces held.
typedef struct Flow
{
    const int32_t* n;
    float l;
    float dt;
    float g;
    float decay;
    uint8_t closed;
    const float* now;
    float* next;
    const float* faces[2];
    float* target[2];
    float* giving;
    const float* ground;
    int32_t* heldRows;
} Flow;

// The surface's height at cell (x, z).
static float Surface(const Flow* f, int32_t x, int32_t z)
{
    int64_t i = (int64_t)z * f->n[0] + x;
    return f->ground[i] + f->now[i];
}

// Face k of an axis, its cells about it, and whether it is a wall: an X
// face (x, z) lies between cells (x - 1, z) and (x, z); a Z face between
// (x, z - 1) and (x, z).
typedef struct Face
{
    int32_t a[2];
    int32_t b[2];
    bool lowWall;
    bool highWall;
} Face;

static Face FaceAt(const Flow* f, int32_t axis, int32_t x, int32_t z)
{
    Face face = {.a = {axis == 0 ? x - 1 : x, axis == 0 ? z : z - 1}, .b = {x, z}};
    face.lowWall = axis == 0 ? x == 0 : z == 0;
    face.highWall = axis == 0 ? x == f->n[0] : z == f->n[1];
    return face;
}

// A face's surfaces on its two sides and its sill: at a wall the outside
// is an empty cell of the edge's ground.
static void Sides(const Flow* f, const Face* face, float h[2], float* sill)
{
    const int32_t* in = face->lowWall ? face->b : face->a;
    float ground = f->ground[(int64_t)in[1] * f->n[0] + in[0]];
    h[0] = face->lowWall ? ground : Surface(f, face->a[0], face->a[1]);
    h[1] = face->highWall ? ground : Surface(f, face->b[0], face->b[1]);
    const int32_t* out = face->highWall ? face->a : face->b;
    float other = f->ground[(int64_t)out[1] * f->n[0] + out[0]];
    *sill = face->lowWall || face->highWall ? ground : fmaxf(ground, other);
}

// Whether face (x, z) of an axis carries water: open, its water over its
// sill above DRY; its surfaces and sill.
static bool IsWet(const Flow* f, int32_t axis, int32_t x, int32_t z, float h[2], float* sill)
{
    Face face = FaceAt(f, axis, x, z);
    bool wall = face.lowWall || face.highWall;
    uint8_t bit = axis == 0 ? (face.lowWall ? mvfx_waterWallMinX : mvfx_waterWallMaxX)
                            : (face.lowWall ? mvfx_waterWallMinZ : mvfx_waterWallMaxZ);
    if (wall && (f->closed & bit) != 0)
    {
        return false;
    }
    Sides(f, &face, h, sill);
    return fmaxf(h[0], h[1]) - *sill > DRY;
}

// Face (x, z)'s velocity before the substep, its own where out of the
// face grid (no gradient across the grid's edge).
static float Old(const Flow* f, int32_t axis, int32_t x, int32_t z, float own)
{
    int32_t w = f->n[0] + (axis == 0 ? 1 : 0);
    int32_t rows = f->n[1] + (axis == 1 ? 1 : 0);
    bool in = x >= 0 && x < w && z >= 0 && z < rows;
    return in ? f->faces[axis][(int64_t)z * w + x] : own;
}

// The water a face carries before the substep, a length squared a
// second: its velocity times the water over its sill on the giver's
// side; 0 off the face grid or through a closed wall.
static float Discharge(const Flow* f, int32_t axis, int32_t x, int32_t z)
{
    float v = Old(f, axis, x, z, 0.0f);
    float h[2];
    float sill = 0.0f;
    if (v == 0.0f || !IsWet(f, axis, x, z, h, &sill))
    {
        return 0.0f;
    }
    return v * fmaxf((v > 0.0f ? h[0] : h[1]) - sill, 0.0f);
}

// A cell's depth, 0 off the grid.
static float DepthAt(const Flow* f, int32_t x, int32_t z)
{
    bool in = x >= 0 && x < f->n[0] && z >= 0 && z < f->n[1];
    return in ? f->now[(int64_t)z * f->n[0] + x] : 0.0f;
}

// Face (x, z) of an axis carried with the flow, in Stelling and
// Duinmeijer's momentum-conserving upwind form ("A staggered conservative
// scheme for every Froude number in rapidly varied shallow water flows",
// Int. J. Numer. Meth. Fluids 2003): drawn toward each upstream
// neighbor's velocity by the discharge toward it over the face's depth,
// dt q / (d l), along its axis and across it, the pulls held to 1 in all
// so the result stays between its neighbors'; a face filling from a fast
// flow takes the flow's speed.
static float Carried(const Flow* f, int32_t axis, int32_t x, int32_t z)
{
    // Steps along the face's axis and across it, in face indices.
    const int32_t along[2] = {axis == 0 ? 1 : 0, axis == 0 ? 0 : 1};
    const int32_t across[2] = {along[1], along[0]};
    int32_t other = 1 - axis;
    float u = Old(f, axis, x, z, 0.0f);
    // The discharges through the face's two cells along its axis, and
    // across it at its two edges (the other axis's faces of both cells).
    int32_t lx = x - along[0];
    int32_t lz = z - along[1];
    float back = 0.5f * (Discharge(f, axis, lx, lz) + Discharge(f, axis, x, z));
    float ahead =
        0.5f * (Discharge(f, axis, x, z) + Discharge(f, axis, x + along[0], z + along[1]));
    float below = 0.5f * (Discharge(f, other, lx, lz) + Discharge(f, other, x, z));
    float above = 0.5f * (Discharge(f, other, lx + across[0], lz + across[1]) +
                          Discharge(f, other, x + across[0], z + across[1]));
    const float pull[4] = {fmaxf(back, 0.0f), fmaxf(-ahead, 0.0f), fmaxf(below, 0.0f),
                           fmaxf(-above, 0.0f)};
    const float from[4] = {Old(f, axis, x - along[0], z - along[1], u),
                           Old(f, axis, x + along[0], z + along[1], u),
                           Old(f, axis, x - across[0], z - across[1], u),
                           Old(f, axis, x + across[0], z + across[1], u)};
    float depth = 0.5f * (DepthAt(f, lx, lz) + DepthAt(f, x, z));
    float k = f->dt / (fmaxf(depth, DRY) * f->l);
    float total = k * (pull[0] + pull[1] + pull[2] + pull[3]);
    float held = total > 1.0f ? k / total : k;
    float moved = 0.0f;
    for (int32_t j = 0; j < 4; ++j)
    {
        moved += pull[j] * (from[j] - u);
    }
    return u + held * moved;
}

// Face (x, z) of an axis carried with the flow and slowed by friction,
// exp(-friction dt), then accelerated by the surface's fall across it and
// held within 0.5 l / dt (a fall off a cliff outruns the substep's bound):
// 0 at a closed wall or dry, an open wall's only outward; whether the
// bound held it.
static float NextVelocity(const Flow* f, int32_t axis, int32_t x, int32_t z, bool* held)
{
    Face face = FaceAt(f, axis, x, z);
    float h[2];
    float sill = 0.0f;
    *held = false;
    if (!IsWet(f, axis, x, z, h, &sill))
    {
        return 0.0f;
    }
    float v = Carried(f, axis, x, z) * f->decay + f->dt * f->g * (h[0] - h[1]) / f->l;
    float most = 0.5f * f->l / f->dt;
    *held = fabsf(v) > most;
    v = fminf(fmaxf(v, -most), most);
    // An open wall lets water out, never in: its outside is empty, so its
    // fall is outward, but a cell draining both ways can carry it inward.
    return face.lowWall ? fminf(v, 0.0f) : (face.highWall ? fmaxf(v, 0.0f) : v);
}

// Steps rows of faces: row z of X faces, and of Z faces (whose last row,
// n1, the last range takes too); each row's faces held into its count.
static void FaceRows(int32_t start, int32_t end, void* context)
{
    const Flow* f = context;
    for (int32_t z = start; z < end; ++z)
    {
        int32_t count = 0;
        for (int32_t axis = 0; axis < 2; ++axis)
        {
            int32_t w = f->n[0] + (axis == 0 ? 1 : 0);
            int32_t rows = axis == 1 && z == f->n[1] - 1 ? 2 : 1;
            for (int32_t r = 0; r < rows; ++r)
            {
                for (int32_t x = 0; x < w; ++x)
                {
                    bool held = false;
                    f->target[axis][(int64_t)(z + r) * w + x] =
                        NextVelocity(f, axis, x, z + r, &held);
                    count += held ? 1 : 0;
                }
            }
        }
        f->heldRows[z] = count;
    }
}

// The water a face carries a second before its giver's scale: its
// velocity times the water over its sill on the giver's side times l;
// toward +X (+Z) above 0. Its giver's index, -1 for none (the outside).
static float FaceFlux(const Flow* f, int32_t axis, int32_t x, int32_t z, int64_t* giver)
{
    Face face = FaceAt(f, axis, x, z);
    float v = f->target[axis][(int64_t)z * (f->n[0] + (axis == 0 ? 1 : 0)) + x];
    float h[2];
    float sill = 0.0f;
    Sides(f, &face, h, &sill);
    bool fromLow = v > 0.0f;
    bool outside = fromLow ? face.lowWall : face.highWall;
    const int32_t* cell = fromLow ? face.a : face.b;
    *giver = outside || v == 0.0f ? -1 : (int64_t)cell[1] * f->n[0] + cell[0];
    float over = fmaxf((fromLow ? h[0] : h[1]) - sill, 0.0f);
    return *giver < 0 ? 0.0f : v * over * f->l;
}

// Cell (x, z)'s four faces' fluxes, out of it above 0 (-X, +X, -Z, +Z),
// unscaled.
static void CellFluxes(const Flow* f, int32_t x, int32_t z, float out[4], int64_t givers[4])
{
    out[0] = -FaceFlux(f, 0, x, z, &givers[0]);
    out[1] = FaceFlux(f, 0, x + 1, z, &givers[1]);
    out[2] = -FaceFlux(f, 1, x, z, &givers[2]);
    out[3] = FaceFlux(f, 1, x, z + 1, &givers[3]);
}

static void GivingRows(int32_t start, int32_t end, void* context)
{
    const Flow* f = context;
    for (int32_t z = start; z < end; ++z)
    {
        for (int32_t x = 0; x < f->n[0]; ++x)
        {
            float out[4];
            int64_t givers[4];
            CellFluxes(f, x, z, out, givers);
            int64_t i = (int64_t)z * f->n[0] + x;
            float sum = 0.0f;
            for (int32_t k = 0; k < 4; ++k)
            {
                sum += givers[k] == i ? out[k] : 0.0f;
            }
            float most = f->now[i] * f->l * f->l / f->dt;
            f->giving[i] = sum > most ? most / sum : 1.0f;
        }
    }
}

static void DepthRows(int32_t start, int32_t end, void* context)
{
    const Flow* f = context;
    for (int32_t z = start; z < end; ++z)
    {
        for (int32_t x = 0; x < f->n[0]; ++x)
        {
            float out[4];
            int64_t givers[4];
            CellFluxes(f, x, z, out, givers);
            float lost = 0.0f;
            for (int32_t k = 0; k < 4; ++k)
            {
                lost += givers[k] < 0 ? 0.0f : out[k] * f->giving[givers[k]];
            }
            int64_t i = (int64_t)z * f->n[0] + x;
            f->next[i] = fmaxf(f->now[i] - f->dt * lost / (f->l * f->l), 0.0f);
        }
    }
}

// The fastest anything moves: the most face speed and the deepest
// water's wave speed, sqrt(g d).
float mvfxShallowFastest(const mvfxCompiledWater* water, const mvfxWaterState* state)
{
    const float* d = state->heights[state->current];
    float deepest = 0.0f;
    for (int64_t i = 0; i < water->cells; ++i)
    {
        deepest = fmaxf(deepest, d[i]);
    }
    float speed = 0.0f;
    for (int32_t k = 0; k < 2; ++k)
    {
        const float* v = state->faces[k][state->faceCurrent];
        for (int64_t i = 0; i < mvfxWaterFaces(water->def.size, k); ++i)
        {
            speed = fmaxf(speed, fabsf(v[i]));
        }
    }
    return speed + sqrtf(water->def.gravity * deepest);
}

// Whether the surface moves, and its height's largest size.
static void Settle(const mvfxCompiledWater* water, mvfxWaterState* state)
{
    const float* d = state->heights[state->current];
    float reach = 0.0f;
    for (int64_t i = 0; i < water->cells; ++i)
    {
        reach = fmaxf(reach, fabsf(state->ground[i] + d[i]));
    }
    float speed = 0.0f;
    for (int32_t k = 0; k < 2; ++k)
    {
        const float* v = state->faces[k][state->faceCurrent];
        for (int64_t i = 0; i < mvfxWaterFaces(water->def.size, k); ++i)
        {
            speed = fmaxf(speed, fabsf(v[i]));
        }
    }
    state->reach = reach;
    state->holds = speed > MVFX_WATER_STILL;
}

// One substep of dt: the faces' velocities carried and accelerated into
// the other buffer, the faces held counted; the givers' scales; the
// depths.
static void Substep(const mvfxCompiledWater* water, mvfxWaterState* state, float dt,
                    const mvfxTasks* tasks)
{
    int32_t now = state->faceCurrent;
    Flow f = {.n = water->def.size,
              .l = water->def.cellSize,
              .dt = dt,
              .g = water->def.gravity,
              .decay = mvfxExp(-water->def.friction * dt),
              .closed = water->def.closedWalls,
              .now = state->heights[state->current],
              .next = state->heights[1 - state->current],
              .faces = {state->faces[0][now], state->faces[1][now]},
              .target = {state->faces[0][1 - now], state->faces[1][1 - now]},
              .giving = state->giving,
              .ground = state->ground,
              .heldRows = state->heldRows};
    mvfxRunTasks(tasks, FaceRows, water->def.size[1], ROW_RANGE, &f);
    for (int32_t z = 0; z < water->def.size[1]; ++z)
    {
        state->facesHeld += (uint64_t)state->heldRows[z];
    }
    mvfxRunTasks(tasks, GivingRows, water->def.size[1], ROW_RANGE, &f);
    mvfxRunTasks(tasks, DepthRows, water->def.size[1], ROW_RANGE, &f);
    state->current = 1 - state->current;
    state->faceCurrent = 1 - now;
}

int32_t mvfxShallowSubsteps(const mvfxCompiledWater* water, float fastest, double seconds,
                            float* dt, bool* slowed)
{
    *dt = 0.0f;
    *slowed = false;
    if (!(seconds > 0.0))
    {
        return 0;
    }
    double longest = fastest > 0.0f ? 0.5 * (double)water->def.cellSize / (double)fastest : seconds;
    double count = fmax(ceil(seconds / longest), 1.0);
    *slowed = count > (double)MVFX_MAX_WATER_SUBSTEPS;
    *dt = (float)(*slowed ? longest : seconds / count);
    return *slowed ? MVFX_MAX_WATER_SUBSTEPS : (int32_t)count;
}

void mvfxStepShallow(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks)
{
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    mvfxWaterState* state = &instance->waters[w];
    double seconds =
        (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND;
    // Counted before the fill, pours and pushes, from the speed of the
    // state the last context step left, as a planned instance counts from
    // the speed reported.
    float dt = 0.0f;
    bool slowed = false;
    int32_t substeps = mvfxShallowSubsteps(water, state->stepSpeed, seconds, &dt, &slowed);
    mvfxReadWaterGround(instance, w, tasks);
    if (!state->filled)
    {
        mvfxFillWater(instance, w);
    }
    // Sources pour while the instance plays.
    if (instance->stepStart < instance->stopTick)
    {
        mvfxPourWaterSources(instance, w, seconds);
    }
    mvfxPushShallow(instance, w);
    for (int32_t s = 0; s < substeps; ++s)
    {
        Substep(water, state, dt, tasks);
    }
    state->stepsSlowed += slowed ? 1u : 0u;
    Settle(water, state);
}
