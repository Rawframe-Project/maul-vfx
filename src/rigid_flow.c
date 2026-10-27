// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_flow.h"

#include "elementary.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The most sweeps the Jacobi rotations take.
#define MOST_SWEEPS 16

// The residual turns' pieces: at most this many, each of a size this
// cube root of a turn's error terms.
#define MOST_PIECES 64.0
#define PIECE_TURN  0.25

// The largest half turn mvfxSinCos takes.
#define MOST_HALF_TURN 0x1p24

// One Jacobi rotation zeroing a[p][q] of a symmetric matrix, its axes'
// columns v turned with it.
static void Rotate(double a[3][3], double v[3][3], int32_t p, int32_t q)
{
    double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
    double t = (theta < 0.0 ? -1.0 : 1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
    double c = 1.0 / sqrt(t * t + 1.0);
    double s = t * c;
    double apq = a[p][q];
    a[p][p] -= t * apq;
    a[q][q] += t * apq;
    a[p][q] = 0.0;
    a[q][p] = 0.0;
    int32_t r = 3 - p - q;
    double arp = a[r][p];
    double arq = a[r][q];
    a[r][p] = c * arp - s * arq;
    a[p][r] = a[r][p];
    a[r][q] = s * arp + c * arq;
    a[q][r] = a[r][q];
    for (int32_t k = 0; k < 3; ++k)
    {
        double vkp = v[k][p];
        double vkq = v[k][q];
        v[k][p] = c * vkp - s * vkq;
        v[k][q] = s * vkp + c * vkq;
    }
}

// The unit quaternion of a rotation matrix (its columns the turned axes),
// by its largest of the trace and diagonal (Shepperd's).
static void QuaternionOf(const double m[3][3], float out[4])
{
    double trace = m[0][0] + m[1][1] + m[2][2];
    double q[4];
    if (trace >= m[0][0] && trace >= m[1][1] && trace >= m[2][2])
    {
        double s = 2.0 * sqrt(1.0 + trace);
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
        q[3] = 0.25 * s;
    }
    else if (m[0][0] >= m[1][1] && m[0][0] >= m[2][2])
    {
        double s = 2.0 * sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
        q[0] = 0.25 * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
        q[3] = (m[2][1] - m[1][2]) / s;
    }
    else if (m[1][1] >= m[2][2])
    {
        double s = 2.0 * sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]);
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25 * s;
        q[2] = (m[1][2] + m[2][1]) / s;
        q[3] = (m[0][2] - m[2][0]) / s;
    }
    else
    {
        double s = 2.0 * sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25 * s;
        q[3] = (m[1][0] - m[0][1]) / s;
    }
    double length = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int32_t k = 0; k < 4; ++k)
    {
        out[k] = (float)(q[k] / length);
    }
}

mvfxPrincipal mvfxPrincipalOf(const float inertia[6])
{
    static const int32_t at[3][3] = {{0, 3, 4}, {3, 1, 5}, {4, 5, 2}};
    double a[3][3];
    for (int32_t r = 0; r < 3; ++r)
    {
        for (int32_t c = 0; c < 3; ++c)
        {
            a[r][c] = (double)inertia[at[r][c]];
        }
    }
    double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static const int32_t pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (int32_t sweep = 0; sweep < MOST_SWEEPS; ++sweep)
    {
        if (a[0][1] == 0.0 && a[0][2] == 0.0 && a[1][2] == 0.0)
        {
            break;
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            if (a[pairs[k][0]][pairs[k][1]] != 0.0)
            {
                Rotate(a, v, pairs[k][0], pairs[k][1]);
            }
        }
    }
    // Largest first, a tie keeping the earlier axis first.
    int32_t order[3] = {0, 1, 2};
    for (int32_t i = 1; i < 3; ++i)
    {
        for (int32_t j = i; j > 0 && a[order[j]][order[j]] > a[order[j - 1]][order[j - 1]]; --j)
        {
            int32_t swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    }
    mvfxPrincipal p;
    double axes[3][3];
    for (int32_t c = 0; c < 3; ++c)
    {
        p.moments[c] = (float)a[order[c]][order[c]];
        int32_t largest = 0;
        for (int32_t k = 1; k < 3; ++k)
        {
            largest = fabs(v[k][order[c]]) > fabs(v[largest][order[c]]) ? k : largest;
        }
        double sign = v[largest][order[c]] < 0.0 ? -1.0 : 1.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            axes[k][c] = sign * v[k][order[c]];
        }
    }
    double det = axes[0][0] * (axes[1][1] * axes[2][2] - axes[2][1] * axes[1][2]) -
                 axes[0][1] * (axes[1][0] * axes[2][2] - axes[2][0] * axes[1][2]) +
                 axes[0][2] * (axes[1][0] * axes[2][1] - axes[2][0] * axes[1][1]);
    for (int32_t k = 0; k < 3 && det < 0.0; ++k)
    {
        axes[k][2] = -axes[k][2];
    }
    QuaternionOf(axes, p.rotation);
    return p;
}

// Scales a quaternion to unit length.
static void Unit(double q[4])
{
    double length = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] /= length;
    }
}

// The quaternion product a b: b's turn, then a's.
static void Multiply(const double a[4], const double b[4], double out[4])
{
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

// Turns v by a unit quaternion.
static void Turn(const double q[4], const double v[3], double out[3])
{
    double tx = 2.0 * (q[1] * v[2] - q[2] * v[1]);
    double ty = 2.0 * (q[2] * v[0] - q[0] * v[2]);
    double tz = 2.0 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

// The sine and cosine of a half turn, by mvfxSinCos and scaled to a unit
// pair in binary64: binary32's pair is one only to rounding, the same at
// each substep at one angle, which shrank or grew a body's momentum by
// 0.15% over 24000 substeps. False past mvfxSinCos's range.
static bool HalfTurn(double half, double* sine, double* cosine)
{
    if (!(fabs(half) <= MOST_HALF_TURN))
    {
        return false;
    }
    float s = 0.0f;
    float c = 1.0f;
    mvfxSinCos((float)half, &s, &c);
    double length = sqrt((double)s * (double)s + (double)c * (double)c);
    *sine = (double)s / length;
    *cosine = (double)c / length;
    return true;
}

// Turns the body by theta about its principal axis i, and its momentum L
// in its frame back by theta: the residual's exact flow.
static bool TurnAbout(int32_t i, double theta, double q[4], double L[3])
{
    double sine = 0.0;
    double cosine = 1.0;
    if (!HalfTurn(0.5 * theta, &sine, &cosine))
    {
        return false;
    }
    double r[4] = {0.0, 0.0, 0.0, cosine};
    r[i] = sine;
    double turned[4];
    Multiply(q, r, turned);
    memcpy(q, turned, sizeof turned);
    double c = cosine * cosine - sine * sine;
    double s = 2.0 * sine * cosine;
    int32_t a = (i + 1) % 3;
    int32_t b = (i + 2) % 3;
    double la = L[a];
    double lb = L[b];
    L[a] = c * la + s * lb;
    L[b] = -s * la + c * lb;
    return true;
}

// How many equal pieces a substep's residual turns take: the least n
// from 1 to MOST_PIECES with n^3 at least the larger of a^2 b and a b^2
// over PIECE_TURN^3, a = turn (1 / I2 - 1 / I1) and b = turn (1 / I1 -
// 1 / I0) for turn = h |L| (n = ceil(cbrt(...) / PIECE_TURN), exactly).
// The splitting's error goes as their product: a box 0.5 by 1 by 1.5 at 5
// radians a substep had its energy off by up to 150% in one piece. A body
// symmetric about an axis, a or b 0, takes one.
static double Pieces(double turn, double least, double most)
{
    double a = turn * least;
    double b = turn * most;
    double reach = fmax(a * a * b, a * b * b) / (PIECE_TURN * PIECE_TURN * PIECE_TURN);
    double n = 1.0;
    while (n < MOST_PIECES && n * n * n < reach)
    {
        n += 1.0;
    }
    return n;
}

bool mvfxFlowBody(const mvfxPrincipal* p, float t, float q[4], float w[3])
{
    const double m[3] = {(double)p->moments[0], (double)p->moments[1], (double)p->moments[2]};
    const double h = (double)t;
    const double v[3] = {(double)w[0], (double)w[1], (double)w[2]};
    if (m[0] == m[2] || !(m[2] > 0.0))
    {
        return false;
    }
    double sine = 0.0;
    double cosine = 1.0;
    double turned[4];
    const double from[4] = {(double)q[0], (double)q[1], (double)q[2], (double)q[3]};
    const double rotation[4] = {(double)p->rotation[0], (double)p->rotation[1],
                                (double)p->rotation[2], (double)p->rotation[3]};
    double qp[4];
    Multiply(from, rotation, qp);
    Unit(qp);
    const double back[4] = {-qp[0], -qp[1], -qp[2], qp[3]};
    double wp[3];
    Turn(back, v, wp);
    double L[3] = {m[0] * wp[0], m[1] * wp[1], m[2] * wp[2]};
    double least = 1.0 / m[2] - 1.0 / m[1];
    double most = 1.0 / m[0] - 1.0 / m[1];
    double pieces = Pieces(h * sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]), least, -most);
    double piece = h / pieces;
    bool done = true;
    for (double k = 0.0; done && k < pieces; k += 1.0)
    {
        done = TurnAbout(2, 0.5 * piece * L[2] * least, qp, L) &&
               TurnAbout(0, piece * L[0] * most, qp, L) &&
               TurnAbout(2, 0.5 * piece * L[2] * least, qp, L);
    }
    double size = sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    if (!done || !(size > 0.0) || !isfinite(size) ||
        !HalfTurn(0.5 * h * size / m[1], &sine, &cosine))
    {
        return false;
    }
    const double r[4] = {L[0] / size * sine, L[1] / size * sine, L[2] / size * sine, cosine};
    Multiply(qp, r, turned);
    Unit(turned);
    const double body[3] = {L[0] / m[0], L[1] / m[1], L[2] / m[2]};
    double out[3];
    Turn(turned, body, out);
    const double undo[4] = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    Multiply(turned, undo, qp);
    Unit(qp);
    for (int32_t k = 0; k < 3; ++k)
    {
        w[k] = (float)out[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] = (float)qp[k];
    }
    return true;
}
