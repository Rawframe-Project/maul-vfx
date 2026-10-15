// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_shape.h"

#include <math.h>

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

mvfxBodyShape mvfxBodyShapeOf(const mvfxRigidDef* rigid, const float size[3], float* inverseMass,
                              float inverseInertia[3])
{
    mvfxBodyShape s = {.kind = rigid->shape, .radius = 0.5f * fabsf(size[0])};
    double r = (double)s.radius;
    double pi = 3.14159265358979;
    double mass = (double)rigid->density * 4.0 / 3.0 * pi * r * r * r;
    double inertia[3] = {0.4 * mass * r * r, 0.4 * mass * r * r, 0.4 * mass * r * r};
    s.count = 1;
    if (s.kind == mvfx_rigidCapsule)
    {
        double length = fmax((double)fabsf(size[1]) - 2.0 * r, 0.0);
        double tube = (double)rigid->density * pi * r * r * length;
        double ends = mass;
        mass = tube + ends;
        inertia[1] = tube * r * r / 2.0 + ends * 0.4 * r * r;
        inertia[0] = tube * (length * length / 12.0 + r * r / 4.0) +
                     ends * (0.4 * r * r + length * length / 4.0 + 3.0 * length * r / 8.0);
        inertia[2] = inertia[0];
        s.half[1] = (float)(0.5 * length);
        s.points[0][1] = s.half[1];
        s.points[1][1] = -s.half[1];
        s.count = 2;
    }
    else if (s.kind == mvfx_rigidBox)
    {
        double a = (double)fabsf(size[0]);
        double b = (double)fabsf(size[1]);
        double c = (double)fabsf(size[2]);
        mass = (double)rigid->density * a * b * c;
        inertia[0] = mass * (b * b + c * c) / 12.0;
        inertia[1] = mass * (a * a + c * c) / 12.0;
        inertia[2] = mass * (a * a + b * b) / 12.0;
        s.radius = 0.0f;
        s.count = 8;
        for (int32_t k = 0; k < 3; ++k)
        {
            s.half[k] = 0.5f * fabsf(size[k]);
        }
        for (int32_t i = 0; i < 8; ++i)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                s.points[i][k] = (i >> k & 1) != 0 ? s.half[k] : -s.half[k];
            }
        }
    }
    // A shape of no volume is a point of unit mass that does not turn.
    *inverseMass = mass > 0.0 ? (float)(1.0 / mass) : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        inverseInertia[k] = inertia[k] > 0.0 ? (float)(1.0 / inertia[k]) : 0.0f;
    }
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
    if (s->kind == mvfx_rigidBox)
    {
        return sqrtf(Dot(s->half, s->half));
    }
    return s->half[1] + s->radius;
}
