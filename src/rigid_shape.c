// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_shape.h"

#include <math.h>

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void mvfxShapePoint(const mvfxBodyShape* s, int32_t i, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        if (s->kind == mvfx_rigidHull)
        {
            out[k] = s->hull->vertices[i][k] * s->scale[k];
        }
        else if (s->kind == mvfx_rigidBox)
        {
            out[k] = (i >> k & 1) != 0 ? s->half[k] : -s->half[k];
        }
        else
        {
            out[k] =
                k == 1 && s->kind == mvfx_rigidCapsule ? (i == 0 ? s->half[1] : -s->half[1]) : 0.0f;
        }
    }
}

// The inverse of a symmetric matrix (xx, yy, zz, xy, xz, yz); zero when it
// has none.
static void Invert(const double m[6], double out[6])
{
    double c[6] = {m[1] * m[2] - m[5] * m[5], m[0] * m[2] - m[4] * m[4], m[0] * m[1] - m[3] * m[3],
                   m[4] * m[5] - m[3] * m[2], m[3] * m[5] - m[4] * m[1], m[3] * m[4] - m[0] * m[5]};
    double det = m[0] * c[0] + m[3] * c[3] + m[4] * c[4];
    for (int32_t k = 0; k < 6; ++k)
    {
        out[k] = det > 0.0 ? c[k] / det : 0.0;
    }
}

// A hull's mass and inertia scaled by size along its axes: its second
// moments scaled by the size's product and by each axis' size twice.
static double HullInertia(const mvfxHull* hull, const float size[3], double density,
                          double inertia[6])
{
    double s[3] = {fabs((double)size[0]), fabs((double)size[1]), fabs((double)size[2])};
    double volume = s[0] * s[1] * s[2];
    static const int32_t rows[6] = {0, 1, 2, 0, 0, 1};
    static const int32_t columns[6] = {0, 1, 2, 1, 2, 2};
    double c[6];
    for (int32_t k = 0; k < 6; ++k)
    {
        c[k] = density * volume * s[rows[k]] * s[columns[k]] * (double)hull->moments[k];
    }
    inertia[0] = c[1] + c[2];
    inertia[1] = c[0] + c[2];
    inertia[2] = c[0] + c[1];
    inertia[3] = -c[3];
    inertia[4] = -c[4];
    inertia[5] = -c[5];
    return density * volume * (double)hull->volume;
}

// A primitive's mass and its inertias about its axes.
static double PrimitiveInertia(const mvfxRigidDef* rigid, const float size[3], mvfxBodyShape* s,
                               double inertia[6])
{
    double r = (double)s->radius;
    double pi = 3.14159265358979;
    double mass = (double)rigid->density * 4.0 / 3.0 * pi * r * r * r;
    for (int32_t k = 0; k < 3; ++k)
    {
        inertia[k] = 0.4 * mass * r * r;
    }
    if (s->kind == mvfx_rigidCapsule)
    {
        double length = fmax((double)fabsf(size[1]) - 2.0 * r, 0.0);
        double tube = (double)rigid->density * pi * r * r * length;
        double ends = mass;
        mass = tube + ends;
        inertia[1] = tube * r * r / 2.0 + ends * 0.4 * r * r;
        inertia[0] = tube * (length * length / 12.0 + r * r / 4.0) +
                     ends * (0.4 * r * r + length * length / 4.0 + 3.0 * length * r / 8.0);
        inertia[2] = inertia[0];
        s->half[1] = (float)(0.5 * length);
        s->count = 2;
    }
    else if (s->kind == mvfx_rigidBox)
    {
        double a = (double)fabsf(size[0]);
        double b = (double)fabsf(size[1]);
        double c = (double)fabsf(size[2]);
        mass = (double)rigid->density * a * b * c;
        inertia[0] = mass * (b * b + c * c) / 12.0;
        inertia[1] = mass * (a * a + c * c) / 12.0;
        inertia[2] = mass * (a * a + b * b) / 12.0;
        s->radius = 0.0f;
        s->count = 8;
        for (int32_t k = 0; k < 3; ++k)
        {
            s->half[k] = 0.5f * fabsf(size[k]);
        }
    }
    return mass;
}

// mvfxBodyReach in binary64, whose squares stay in range for the
// smallest shapes.
static double ReachOf(const mvfxBodyShape* s)
{
    double reach = 0.0;
    for (int32_t i = 0; i < s->count && s->kind == mvfx_rigidHull; ++i)
    {
        float p[3];
        mvfxShapePoint(s, i, p);
        reach = fmax(reach, (double)p[0] * (double)p[0] + (double)p[1] * (double)p[1] +
                               (double)p[2] * (double)p[2]);
    }
    if (s->kind == mvfx_rigidHull)
    {
        return sqrt(reach);
    }
    for (int32_t k = 0; k < 3 && s->kind == mvfx_rigidBox; ++k)
    {
        reach += (double)s->half[k] * (double)s->half[k];
    }
    if (s->kind == mvfx_rigidBox)
    {
        return sqrt(reach);
    }
    return (double)s->half[1] + (double)s->radius;
}

mvfxBodyShape mvfxBodyShapeOf(const mvfxRigidDef* rigid, const mvfxHull* hull, const float size[3],
                              mvfxBodyMass* mass)
{
    mvfxBodyShape s = {.kind = rigid->shape, .count = 1, .radius = 0.5f * fabsf(size[0])};
    double inertia[6] = {0.0};
    double m = 0.0;
    if (s.kind == mvfx_rigidHull)
    {
        s.radius = 0.0f;
        s.hull = hull;
        s.count = hull->vertexCount;
        for (int32_t k = 0; k < 3; ++k)
        {
            s.scale[k] = fabsf(size[k]);
        }
        m = HullInertia(hull, size, (double)rigid->density, inertia);
    }
    else
    {
        m = PrimitiveInertia(rigid, size, &s, inertia);
    }
    // A shape of no volume is a point of unit mass that does not turn.
    *mass = (mvfxBodyMass){.inverseMass = 1.0f};
    if (!(m > 0.0))
    {
        return s;
    }
    int32_t massUnit = 0;
    int32_t lengthUnit = 0;
    (void)frexp(m, &massUnit);
    (void)frexp(ReachOf(&s), &lengthUnit);
    double scaled[6];
    double inverse[6];
    for (int32_t k = 0; k < 6; ++k)
    {
        scaled[k] = ldexp(inertia[k], -massUnit - 2 * lengthUnit);
    }
    Invert(scaled, inverse);
    mass->inverseMass = (float)ldexp(1.0 / m, massUnit);
    for (int32_t k = 0; k < 6; ++k)
    {
        mass->inertia[k] = (float)scaled[k];
        mass->inverseInertia[k] = (float)inverse[k];
    }
    mass->massUnit = massUnit;
    mass->lengthUnit = lengthUnit;
    return s;
}

void mvfxAddContact(mvfxBodyContacts* list, const mvfxBodyContact* c)
{
    if (!(c->depth < list->margin))
    {
        return;
    }
    mvfxBodyContact added = *c;
    added.collider = list->collider;
    if (list->count < MVFX_MAX_RIGID_CONTACTS)
    {
        list->items[list->count++] = added;
        return;
    }
    list->capped += 1;
    int32_t shallow = 0;
    for (int32_t j = 1; j < list->count; ++j)
    {
        shallow = list->items[j].depth > list->items[shallow].depth ? j : shallow;
    }
    if (c->depth < list->items[shallow].depth)
    {
        list->items[shallow] = added;
    }
}

void mvfxAddBodyContact(mvfxBodyContacts* list, const float point[3], const float world[3],
                        const float n[3], float depth)
{
    mvfxBodyContact contact = {.depth = depth};
    for (int32_t k = 0; k < 3; ++k)
    {
        contact.body[k] = point[k];
        contact.normal[k] = n[k];
        contact.surface[k] = world[k] - depth * n[k];
    }
    mvfxAddContact(list, &contact);
}

float mvfxBodyReach(const mvfxBodyShape* s)
{
    if (s->kind == mvfx_rigidHull)
    {
        float reach = 0.0f;
        for (int32_t i = 0; i < s->count; ++i)
        {
            float p[3];
            mvfxShapePoint(s, i, p);
            reach = fmaxf(reach, Dot(p, p));
        }
        return sqrtf(reach);
    }
    if (s->kind == mvfx_rigidBox)
    {
        return sqrtf(Dot(s->half, s->half));
    }
    return s->half[1] + s->radius;
}
