// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The surface module's own eigenvalue: the largest eigenvalue of
// symmetric matrices against a reference, near-degenerate ones' bits as
// committed, a plane's exact.

#include "gas_surface.h"
#include "test_harness.h"

#include <math.h>
#include <stdint.h>

// The largest eigenvalue of a symmetric 3 by 3 matrix by Jacobi's
// rotations in binary64.
static double Reference(const float m[6])
{
    double e[6];
    for (int32_t k = 0; k < 6; ++k)
    {
        e[k] = (double)m[k];
    }
    double a[3][3] = {{e[0], e[3], e[4]}, {e[3], e[1], e[5]}, {e[4], e[5], e[2]}};
    for (int32_t sweep = 0; sweep < 30; ++sweep)
    {
        for (int32_t p = 0; p < 2; ++p)
        {
            for (int32_t q = p + 1; q < 3; ++q)
            {
                if (fabs(a[p][q]) < 1e-300)
                {
                    continue;
                }
                double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                double t = (theta >= 0.0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
                double c = 1.0 / sqrt(t * t + 1.0);
                double s = t * c;
                for (int32_t k = 0; k < 3; ++k)
                {
                    double x = a[k][p];
                    double y = a[k][q];
                    a[k][p] = c * x - s * y;
                    a[k][q] = s * x + c * y;
                }
                for (int32_t k = 0; k < 3; ++k)
                {
                    double x = a[p][k];
                    double y = a[q][k];
                    a[p][k] = c * x - s * y;
                    a[q][k] = s * x + c * y;
                }
            }
        }
    }
    return fmax(fmax(a[0][0], a[1][1]), a[2][2]);
}

// R diag(d) R^T in m for a turn R from four entries of state: for kind 1,
// d's largest pair alike; for 2, its smallest pair; for 3, all within
// 1e-3. The entries within 3 still.
static void Repeated(int32_t kind, float m[6], uint64_t* state)
{
    double u[5];
    for (int32_t k = 0; k < 5; ++k)
    {
        *state = *state * 6364136223846793005u + 1442695040888963407u;
        u[k] = (double)(*state >> 11) / 9007199254740992.0;
    }
    double a = 0.5 + 2.0 * u[4];
    const double d[3] = {a, kind == 2 ? a - 2.0 * u[0] : (kind == 3 ? a - 1e-3 * u[0] : a),
                         kind == 3 ? a - 1e-3 * u[1] : a - 2.0 * u[1]};
    double q[4] = {2.0 * u[0] - 1.0, 2.0 * u[1] - 1.0, 2.0 * u[2] - 1.0, 2.0 * u[3] - 1.0};
    double length = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] /= length;
    }
    const double x = q[0];
    const double y = q[1];
    const double z = q[2];
    const double w = q[3];
    const double r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                            {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                            {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    const int32_t rows[6] = {0, 1, 2, 0, 0, 1};
    const int32_t columns[6] = {0, 1, 2, 1, 2, 2};
    for (int32_t e = 0; e < 6; ++e)
    {
        double sum = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            sum += r[rows[e]][k] * d[k] * r[columns[e]][k];
        }
        m[e] = (float)sum;
    }
}

static void TestEigenvalueBits(void)
{
    // 64 near-degenerate matrices (three eigenvalues within 1e-3, turned):
    // their largest eigenvalues' bits hash to what is committed, on every
    // platform.
    uint64_t state = 0x2545F4914F6CDD1Du;
    float found[64];
    for (int32_t c = 0; c < 64; ++c)
    {
        float m[6];
        Repeated(3, m, &state);
        found[c] = mvfxSurfaceEigenvalue(m, 3, 0.4f);
    }
    uint64_t hash = mvfxHash64(0u, found, (int32_t)sizeof found);
    CHECK(hash == 0xF00EFE8D94E08F23u, "near-degenerate eigenvalues' bits as committed");
}

static void TestEigenvalue(void)
{
    // Random symmetric matrices of entries within 3, three in four with
    // repeated eigenvalues at random turns: at or above 0.4 within 2e-6 of
    // the reference (relative to 1 or its size; measured 7.7e-7, where
    // Newton on the cubic left 6.9% at a double top); below, below 0.4
    // too. In 2D, exact to rounding.
    uint64_t state = 0x9E3779B97F4A7C15u;
    float worst = 0.0f;
    bool below = true;
    for (int32_t c = 0; c < 20000; ++c)
    {
        float m[6];
        for (int32_t e = 0; e < 6; ++e)
        {
            state = state * 6364136223846793005u + 1442695040888963407u;
            m[e] = (float)((double)(state >> 11) / 9007199254740992.0 * 6.0 - 3.0);
        }
        if (c % 4 != 0)
        {
            // Repeated eigenvalues, turned: a double one at the top or the
            // bottom, or three nearly alike.
            Repeated(c % 4, m, &state);
        }
        double reference = Reference(m);
        float found = mvfxSurfaceEigenvalue(m, 3, 0.4f);
        if (reference >= 0.4)
        {
            float scale = fmaxf(1.0f, fabsf((float)reference));
            worst = fmaxf(worst, fabsf(found - (float)reference) / scale);
        }
        else
        {
            below = below && found < 0.4f;
        }
    }
    const float flat[6] = {1.5f, -0.5f, 0.0f, 2.0f, 0.0f, 0.0f};
    double exact = 0.5 + sqrt(1.0 + 4.0);
    bool plane = fabs((double)mvfxSurfaceEigenvalue(flat, 2, 0.4f) - exact) <= 1e-6;
    CHECK(worst <= 2e-6f && below && plane, "the largest eigenvalue");
}

int main(void)
{
    TestEigenvalue();
    TestEigenvalueBits();
    return s_failures == 0 ? 0 : 1;
}
