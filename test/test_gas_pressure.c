// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's pressure projection: the multigrid's projected faces against
// those of a direct solve of the same equations on small grids; the
// divergence after 4 and 8 V cycles, and its fall in the early cycles,
// over 2D and 3D boxes of even and odd sizes with open, closed and mixed
// walls; closed walls' faces held at 0; the Taylor-Green vortex in a
// closed box kept free of divergence, its energy falling every step and by
// under a tenth in a second; an open-walled gas projected as it is
// stepped; a restarted gas the same, bit for bit, as a fresh one. A gas's
// faces are read and written through the test hooks; a step of a tick in
// cells of 100, which carries nothing, projects them.

#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

enum
{
    // The most faces along an axis a test's gas has.
    MOST_FACES = 32 * 24 * 17
};

// The gas's faces along each axis, as read or written last.
static float s_faces[3][MOST_FACES];

// Opens an instance of an effect of one gas.
static void Open(const mvfxGasDef* gas)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
}

// The count of a gas's faces along k.
static int64_t FaceCount(const int32_t* n, int32_t k)
{
    return (int64_t)(n[0] + (k == 0 ? 1 : 0)) * (n[1] + (k == 1 ? 1 : 0)) *
           (n[2] + (k == 2 ? 1 : 0));
}

// Reads the gas's faces into s_faces.
static void Read(const int32_t* n)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(mvfxTestReadGasFaces(s_context, s_instance, 0, k, s_faces[k], MOST_FACES) ==
                  FaceCount(n, k),
              "faces read");
    }
}

// Writes s_faces into the gas's faces.
static void Write(const int32_t* n)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(mvfxTestWriteGasFaces(s_context, s_instance, 0, k, s_faces[k], FaceCount(n, k)),
              "faces written");
    }
}

// Projects the gas's faces by a step of a tick, reading them back.
static void Project(const int32_t* n)
{
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "projected");
    Read(n);
}

// A hash's float in [-1, 1).
static float Noise(uint32_t i)
{
    uint32_t h = (i + 1u) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float)(h & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

// Writes noise into the gas's faces.
static void Fill(const int32_t* n)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int64_t f = 0; f < FaceCount(n, k); ++f)
        {
            s_faces[k][f] = Noise((uint32_t)(f * 3 + k));
        }
    }
    Write(n);
}

// The faces' array index of the face across k before cell (x, y, z).
static int64_t Face(const int32_t* n, int32_t k, int32_t x, int32_t y, int32_t z)
{
    const int32_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                          n[2] + (k == 2 ? 1 : 0)};
    return ((int64_t)z * m[1] + y) * m[0] + x;
}

// A cell's divergence: its faces' outflow.
static float DivergenceAt(const int32_t* n, float* const v[3], int32_t x, int32_t y, int32_t z)
{
    float d = v[0][Face(n, 0, x + 1, y, z)] - v[0][Face(n, 0, x, y, z)];
    d += v[1][Face(n, 1, x, y + 1, z)] - v[1][Face(n, 1, x, y, z)];
    return d + (n[2] > 1 ? v[2][Face(n, 2, x, y, z + 1)] - v[2][Face(n, 2, x, y, z)] : 0.0f);
}

// The largest divergence's size in s_faces.
static float MaxDivergence(const int32_t* n)
{
    float* const v[3] = {s_faces[0], s_faces[1], s_faces[2]};
    float most = 0.0f;
    for (int32_t z = 0; z < n[2]; ++z)
    {
        for (int32_t y = 0; y < n[1]; ++y)
        {
            for (int32_t x = 0; x < n[0]; ++x)
            {
                most = fmaxf(most, fabsf(DivergenceAt(n, v, x, y, z)));
            }
        }
    }
    return most;
}

// Solves a dense system a x = b of n unknowns by Gaussian elimination with
// partial pivoting, in place.
static void Solve(double* a, double* b, int32_t n)
{
    for (int32_t c = 0; c < n; ++c)
    {
        int32_t pivot = c;
        for (int32_t r = c + 1; r < n; ++r)
        {
            pivot = fabs(a[r * n + c]) > fabs(a[pivot * n + c]) ? r : pivot;
        }
        for (int32_t k = 0; k < n; ++k)
        {
            double swap = a[c * n + k];
            a[c * n + k] = a[pivot * n + k];
            a[pivot * n + k] = swap;
        }
        double swap = b[c];
        b[c] = b[pivot];
        b[pivot] = swap;
        for (int32_t r = c + 1; r < n; ++r)
        {
            double f = a[r * n + c] / a[c * n + c];
            for (int32_t k = c; k < n; ++k)
            {
                a[r * n + k] -= f * a[c * n + k];
            }
            b[r] -= f * b[c];
        }
    }
    for (int32_t r = n - 1; r >= 0; --r)
    {
        for (int32_t k = r + 1; k < n; ++k)
        {
            b[r] -= a[r * n + k] * b[k];
        }
        b[r] /= a[r * n + r];
    }
}

// Whether a wall is closed.
static bool Closed(const mvfxGasDef* def, mvfxGasWalls wall)
{
    return (def->closedWalls & wall) != 0;
}

static void TestDirect(void)
{
    // A 2D gas of 7 by 5 cells, all walls open, and with its low X and
    // high Y walls closed: the equations written here (each neighbor
    // inside weighs 1, an open wall 2 with the pressure 0 on it, a closed
    // one 0) solved directly, each face then less the difference of the
    // pressures beside it (an open wall's twice its cell's, a closed
    // wall's 0), against the faces 16 V cycles from 0 leave.
    static double a[35 * 35];
    static double b[35];
    static float before[2][48];
    static const mvfxGasWalls walls[2] = {0, mvfx_gasWallMinX | mvfx_gasWallMaxY};
    bool right = true;
    for (int32_t w = 0; w < 2; ++w)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 7;
        def.size[1] = 5;
        def.cellSize = 100.0f;
        def.closedWalls = walls[w];
        def.pressureCycles = 16;
        Open(&def);
        Fill(def.size);
        for (int32_t y = 0; y < 5; ++y)
        {
            s_faces[0][Face(def.size, 0, 0, y, 0)] *= Closed(&def, mvfx_gasWallMinX) ? 0.0f : 1.0f;
            s_faces[0][Face(def.size, 0, 7, y, 0)] *= Closed(&def, mvfx_gasWallMaxX) ? 0.0f : 1.0f;
        }
        for (int32_t x = 0; x < 7; ++x)
        {
            s_faces[1][Face(def.size, 1, x, 0, 0)] *= Closed(&def, mvfx_gasWallMinY) ? 0.0f : 1.0f;
            s_faces[1][Face(def.size, 1, x, 5, 0)] *= Closed(&def, mvfx_gasWallMaxY) ? 0.0f : 1.0f;
        }
        memcpy(before[0], s_faces[0], sizeof(float) * 8 * 5);
        memcpy(before[1], s_faces[1], sizeof(float) * 7 * 6);
        memset(a, 0, sizeof a);
        float* const v[3] = {s_faces[0], s_faces[1], s_faces[2]};
        for (int32_t y = 0; y < 5; ++y)
        {
            for (int32_t x = 0; x < 7; ++x)
            {
                int32_t i = y * 7 + x;
                b[i] = (double)DivergenceAt(def.size, v, x, y, 0);
                const int32_t near[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                const mvfxGasWalls sides[4] = {mvfx_gasWallMinX, mvfx_gasWallMaxX, mvfx_gasWallMinY,
                                               mvfx_gasWallMaxY};
                for (int32_t s = 0; s < 4; ++s)
                {
                    bool inside =
                        near[s][0] >= 0 && near[s][0] < 7 && near[s][1] >= 0 && near[s][1] < 5;
                    double c = inside ? 1.0 : (Closed(&def, sides[s]) ? 0.0 : 2.0);
                    a[i * 35 + i] -= c;
                    if (inside)
                    {
                        a[i * 35 + near[s][1] * 7 + near[s][0]] += c;
                    }
                }
            }
        }
        double most = 0.0;
        for (int32_t i = 0; i < 35; ++i)
        {
            most = fmax(most, fabs(b[i]));
        }
        Solve(a, b, 35);
        Project(def.size);
        double error = 0.0;
        for (int32_t k = 0; k < 2; ++k)
        {
            const int32_t m[2] = {7 + (k == 0 ? 1 : 0), 5 + (k == 1 ? 1 : 0)};
            const mvfxGasWalls low = k == 0 ? mvfx_gasWallMinX : mvfx_gasWallMinY;
            const mvfxGasWalls high = k == 0 ? mvfx_gasWallMaxX : mvfx_gasWallMaxY;
            for (int32_t y = 0; y < m[1]; ++y)
            {
                for (int32_t x = 0; x < m[0]; ++x)
                {
                    int32_t at = k == 0 ? x : y;
                    int32_t n = k == 0 ? 7 : 5;
                    int32_t step = k == 0 ? 1 : 7;
                    int32_t cell = y * 7 + x;
                    double after = at < n ? b[cell] : 0.0;
                    double previous = at > 0 ? b[cell - step] : 0.0;
                    bool wall = at == 0 || at == n;
                    bool closed = (at == 0 && Closed(&def, low)) || (at == n && Closed(&def, high));
                    double expected = closed ? 0.0
                                             : (double)before[k][y * m[0] + x] -
                                                   (wall ? 2.0 : 1.0) * (after - previous);
                    error = fmax(error, fabs((double)s_faces[k][y * m[0] + x] - expected));
                }
            }
        }
        right = right && error <= 1e-4 * most && MaxDivergence(def.size) <= 1e-4f;
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "the multigrid's faces the direct solve's");
}

static void TestDivergence(void)
{
    // Noise for a velocity over boxes of even and odd sizes in 2D and 3D,
    // all walls open, all closed and some of each: 4 V cycles from 0 leave
    // at most 1e-3 of the largest divergence, 8 at most 5e-5 (by then
    // near float's round-off), and cycles 2 to 4 each leave at most 0.4
    // of it (about 0.3 seen). A gas of each count of cycles.
    static const int32_t sizes[4][3] = {{64, 64, 1}, {33, 20, 1}, {32, 24, 16}, {17, 9, 5}};
    static const mvfxGasWalls walls[3] = {0, 63,
                                          mvfx_gasWallMinY | mvfx_gasWallMaxX | mvfx_gasWallMinZ};
    bool right = true;
    for (int32_t s = 0; s < 4; ++s)
    {
        for (int32_t w = 0; w < 3; ++w)
        {
            mvfxGasDef def = mvfxDefaultGasDef();
            memcpy(def.size, sizes[s], sizeof def.size);
            def.cellSize = 100.0f;
            def.closedWalls = walls[w];
            float after[9];
            for (int32_t c = 0; c <= 8; ++c)
            {
                def.pressureCycles = c;
                Open(&def);
                Fill(def.size);
                Project(def.size);
                after[c] = MaxDivergence(def.size);
                right = right && (c < 2 || c > 4 || after[c] <= 0.4f * after[c - 1]);
                mvfxDestroyContext(s_context);
            }
            right = right && after[4] <= 1e-3f * after[0] && after[8] <= 5e-5f * after[0];
        }
    }
    CHECK(right, "the divergence removed");
}

static void TestWalls(void)
{
    // A 2D gas, its low X and high Y walls closed: after a projection those
    // walls' faces are 0, the open ones' not, and its Z faces are 0.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 12;
    def.size[1] = 10;
    def.cellSize = 100.0f;
    def.closedWalls = mvfx_gasWallMinX | mvfx_gasWallMaxY;
    Open(&def);
    Fill(def.size);
    Project(def.size);
    float* const* v = (float* const[3]){s_faces[0], s_faces[1], s_faces[2]};
    bool closed = true;
    bool open = false;
    for (int32_t y = 0; y < 10; ++y)
    {
        closed = closed && v[0][Face(def.size, 0, 0, y, 0)] == 0.0f;
        open = open || v[0][Face(def.size, 0, 12, y, 0)] != 0.0f;
    }
    for (int32_t x = 0; x < 12; ++x)
    {
        closed = closed && v[1][Face(def.size, 1, x, 10, 0)] == 0.0f;
        open = open || v[1][Face(def.size, 1, x, 0, 0)] != 0.0f;
    }
    for (int64_t f = 0; f < FaceCount(def.size, 2); ++f)
    {
        closed = closed && v[2][f] == 0.0f;
    }
    CHECK(closed && open, "closed walls' faces held at 0");
    mvfxDestroyContext(s_context);
}

// The kinetic energy's sum over the faces in s_faces of a 2D gas.
static double Energy(const int32_t* n)
{
    double e = 0.0;
    for (int32_t k = 0; k < 2; ++k)
    {
        for (int64_t f = 0; f < FaceCount(n, k); ++f)
        {
            double w = (double)s_faces[k][f];
            e += w * w;
        }
    }
    return e;
}

static void TestTaylorGreen(void)
{
    // The Taylor-Green vortex u = sin x cos y, v = -cos x sin y in a closed
    // box of side pi (32 by 32 cells), which holds it still but for the
    // advection's own dissipation: a second of steps of 1/64 s keeps it
    // free of divergence (at most 1e-5), its energy falling every step,
    // by at most a tenth in all (8% at 32 cells).
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.cellSize = 3.14159265f / 32.0f;
    def.closedWalls = 63;
    Open(&def);
    const float h = def.cellSize;
    memset(s_faces, 0, sizeof s_faces);
    float* u = s_faces[0];
    float* v = s_faces[1];
    for (int32_t y = 0; y < 32; ++y)
    {
        for (int32_t x = 0; x <= 32; ++x)
        {
            u[y * 33 + x] = sinf((float)x * h) * cosf(((float)y + 0.5f) * h);
        }
    }
    for (int32_t y = 0; y <= 32; ++y)
    {
        for (int32_t x = 0; x < 32; ++x)
        {
            v[y * 32 + x] = -cosf(((float)x + 0.5f) * h) * sinf((float)y * h);
        }
    }
    Write(def.size);
    double start = Energy(def.size);
    double last = start;
    bool falling = true;
    bool free = true;
    for (int32_t i = 0; i < 64; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
        Read(def.size);
        double e = Energy(def.size);
        falling = falling && e < last;
        free = free && MaxDivergence(def.size) <= 1e-5f;
        last = e;
    }
    CHECK(falling && free && last >= 0.9 * start, "the Taylor-Green vortex kept");
    mvfxDestroyContext(s_context);
}

// Steps a context by n steps of 1/64 s.
static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    }
}

// A 2D gas of 32 by 24 cells, every wall open, a box inside blowing up
// and to the right.
static mvfxGasDef Blown(int32_t cycles)
{
    static const mvfxGasSource wind = {.shape = mvfx_gasBox,
                                       .center = {-0.3f, -0.2f, 0},
                                       .halfExtents = {0.4f, 0.3f, 1},
                                       .density = 1,
                                       .setsVelocity = true,
                                       .velocity = {1.5f, 2.0f, 0}};
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 24;
    def.pressureCycles = cycles;
    def.sources = &wind;
    def.sourceCount = 1;
    return def;
}

static void TestStepped(void)
{
    // The blown gas stepped 3 times with 4 V cycles, its walls all open:
    // its divergence at most a hundredth of the same gas's without them.
    float most[2];
    for (int32_t r = 0; r < 2; ++r)
    {
        mvfxGasDef def = Blown(r == 0 ? 0 : 4);
        Open(&def);
        Steps(3);
        Read(def.size);
        most[r] = MaxDivergence(def.size);
        mvfxDestroyContext(s_context);
    }
    CHECK(most[0] > 0.0f && most[1] <= 0.01f * most[0], "a stepped gas projected");
}

// The gas's faces along X and Y and its density into out.
static void Keep(const int32_t* n, float* out)
{
    Read(n);
    memcpy(out, s_faces[0], (size_t)FaceCount(n, 0) * sizeof(float));
    out += FaceCount(n, 0);
    memcpy(out, s_faces[1], (size_t)FaceCount(n, 1) * sizeof(float));
    out += FaceCount(n, 1);
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDensity, out,
                               (int64_t)n[0] * n[1]) == mvfx_success,
          "density");
}

static void TestRestart(void)
{
    // The blown gas stepped 5 times, restarted and stepped 5 more: its
    // velocity and density bit for bit a fresh gas's after 5, the last
    // solve's pressure, which the next solve starts from, not carried past
    // the restart.
    static float fresh[33 * 24 + 32 * 25 + 32 * 24];
    static float restarted[33 * 24 + 32 * 25 + 32 * 24];
    mvfxGasDef def = Blown(2);
    Open(&def);
    Steps(5);
    Keep(def.size, fresh);
    CHECK(mvfxInstance_Restart(s_context, s_instance) == mvfx_success, "restart");
    Steps(5);
    Keep(def.size, restarted);
    CHECK(memcmp(fresh, restarted, sizeof fresh) == 0, "a restarted gas a fresh one");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestDirect();
    TestDivergence();
    TestWalls();
    TestTaylorGreen();
    TestStepped();
    TestRestart();
    return s_failures;
}
