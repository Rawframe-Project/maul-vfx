// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_body.h"

#include "direction.h"
#include "event.h"

#include <math.h>

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

mvfxBody mvfxLoadBody(const mvfxRigidSolve* s, int32_t i)
{
    mvfxBody b = {0};
    for (int32_t k = 0; k < 3; ++k)
    {
        b.x[k] = s->x[k][i];
        b.v[k] = s->v[k][i];
        b.w[k] = s->w[k][i];
        b.x0[k] = s->start[(size_t)k * s->stride + (size_t)i];
    }
    for (int32_t k = 0; k < 6; ++k)
    {
        b.inverseInertia[k] = s->mass[(size_t)(1 + k) * s->stride + (size_t)i];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        b.q[k] = s->q[k][i];
        b.q0[k] = s->start[(size_t)(3 + k) * s->stride + (size_t)i];
    }
    b.inverseMass = s->mass[i];
    return b;
}

void mvfxStoreBody(const mvfxRigidSolve* s, int32_t i, const mvfxBody* b)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        s->x[k][i] = b->x[k];
        s->v[k][i] = b->v[k];
        s->w[k][i] = b->w[k];
        s->start[(size_t)k * s->stride + (size_t)i] = b->x0[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        s->q[k][i] = b->q[k];
        s->start[(size_t)(3 + k) * s->stride + (size_t)i] = b->q0[k];
    }
}

// The inverse inertia times v, in the world.
static void InverseInertiaTimes(const mvfxBody* b, const float v[3], float out[3])
{
    const float inverse[4] = {-b->q[0], -b->q[1], -b->q[2], b->q[3]};
    float local[3];
    mvfxRotate(inverse, v, local);
    const float* m = b->inverseInertia;
    const float turned[3] = {m[0] * local[0] + m[3] * local[1] + m[4] * local[2],
                             m[3] * local[0] + m[1] * local[1] + m[5] * local[2],
                             m[4] * local[0] + m[5] * local[1] + m[2] * local[2]};
    mvfxRotate(b->q, turned, out);
}

float mvfxBodyWeight(const mvfxBody* b, const float r[3], const float n[3])
{
    float c[3];
    float turned[3];
    Cross(r, n, c);
    InverseInertiaTimes(b, c, turned);
    return b->inverseMass + Dot(c, turned);
}

// Turns q by a turn vector to first order (q + [turn, 0] q / 2),
// normalized.
static void TurnBy(float q[4], const float turn[3])
{
    const float spin[4] = {turn[0], turn[1], turn[2], 0.0f};
    float dq[4];
    mvfxMultiplyRotations(spin, q, dq);
    float length = 0.0f;
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] += 0.5f * dq[k];
        length += q[k] * q[k];
    }
    length = sqrtf(length);
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] /= length;
    }
}

void mvfxBodyPush(mvfxBody* b, const float r[3], const float p[3], bool both)
{
    float c[3];
    float turn[3];
    Cross(r, p, c);
    InverseInertiaTimes(b, c, turn);
    TurnBy(b->q, turn);
    if (both)
    {
        TurnBy(b->q0, turn);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        b->x[k] += p[k] * b->inverseMass;
        b->x0[k] += both ? p[k] * b->inverseMass : 0.0f;
    }
}

void mvfxBodyKick(mvfxBody* b, const float r[3], const float j[3])
{
    float c[3];
    float turn[3];
    Cross(r, j, c);
    InverseInertiaTimes(b, c, turn);
    for (int32_t k = 0; k < 3; ++k)
    {
        b->v[k] += j[k] * b->inverseMass;
        b->w[k] += turn[k];
    }
}

void mvfxBodyPointVelocity(const mvfxBody* b, const float r[3], float out[3])
{
    Cross(b->w, r, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] += b->v[k];
    }
}

// The patch's way over the substep (its displacement), a's from b's (b
// NULL for the world).
static void PatchSlide(const mvfxBody* a, const float ra[3], const float ra0[3], const mvfxBody* b,
                       const float rb[3], const float rb0[3], float slide[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        slide[k] = a->x[k] + ra[k] - a->x0[k] - ra0[k];
        slide[k] -= b != nullptr ? b->x[k] + rb[k] - b->x0[k] - rb0[k] : 0.0f;
    }
}

// The generalized inverse mass of the patch along u.
static float PatchWeight(const mvfxBody* a, const float ra[3], const mvfxBody* b, const float rb[3],
                         const float u[3])
{
    return mvfxBodyWeight(a, ra, u) + (b != nullptr ? mvfxBodyWeight(b, rb, u) : 0.0f);
}

// Where a friction impulse along the patch acts on a body at offset r
// from its center to the patch's: level with the center while the
// patch's normal loads can balance its tipping (it times the center's
// height over the patch) with balance, their load times the patch's
// radius; at the patch past that, the contacts' pushes alone resisting.
static void Lever(const float r[3], const float n[3], float balance, float impulse, float out[3])
{
    float along = Dot(r, n);
    float level = impulse * fabsf(along) <= balance ? 1.0f : 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = r[k] - level * along * n[k];
    }
}

void mvfxRubPatch(const mvfxRigidSolve* s, mvfxBody* a, const float ra[3], const float ra0[3],
                  mvfxBody* b, const float rb[3], const float rb0[3], const float n[3], float load,
                  float radius, float* rubbed)
{
    float slide[3];
    PatchSlide(a, ra, ra0, b, rb, rb0, slide);
    float along = Dot(slide, n);
    float st[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        st[k] = slide[k] - along * n[k];
    }
    float slid = sqrtf(Dot(st, st));
    if (!(slid > 0.0f) || !(load > 0.0f))
    {
        return;
    }
    float u[3] = {st[0] / slid, st[1] / slid, st[2] / slid};
    // Held while holding the bodies' slide (by their masses alone: their
    // turning is the normal contacts' to hold) takes at most the static
    // coefficient times the load; its slide is cancelled at the patch
    // then. Else it slides, left to the dynamic friction. Taken by the
    // patch's weight, its turning included, it was cheap to hold, and a
    // box was held on a slope steeper than its friction allows.
    float masses = a->inverseMass + (b != nullptr ? b->inverseMass : 0.0f);
    float holding = slid / masses;
    if (!(*rubbed + holding <= s->rigid->staticFriction * load))
    {
        return;
    }
    *rubbed += holding;
    float la[3];
    float lb[3] = {0.0f, 0.0f, 0.0f};
    Lever(ra, n, load * radius, *rubbed, la);
    if (b != nullptr)
    {
        Lever(rb, n, load * radius, *rubbed, lb);
    }
    float lambda = slid / PatchWeight(a, la, b, lb, u);
    const float p[3] = {-lambda * u[0], -lambda * u[1], -lambda * u[2]};
    mvfxBodyPush(a, la, p, false);
    if (b != nullptr)
    {
        const float back[3] = {-p[0], -p[1], -p[2]};
        mvfxBodyPush(b, lb, back, false);
    }
}

void mvfxSlowPatch(mvfxBody* a, const float ra[3], mvfxBody* b, const float rb[3], const float n[3],
                   float pressed, float radius, float coefficient, float* slowed)
{
    float v[3];
    mvfxBodyPointVelocity(a, ra, v);
    if (b != nullptr)
    {
        float vb[3];
        mvfxBodyPointVelocity(b, rb, vb);
        for (int32_t k = 0; k < 3; ++k)
        {
            v[k] -= vb[k];
        }
    }
    float along = Dot(v, n);
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] -= along * n[k];
    }
    float speed = sqrtf(Dot(v, v));
    if (!(speed > 0.0f))
    {
        return;
    }
    float u[3] = {v[0] / speed, v[1] / speed, v[2] / speed};
    float limit = coefficient * pressed;
    float la[3];
    float lb[3] = {0.0f, 0.0f, 0.0f};
    Lever(ra, n, pressed * radius, limit, la);
    if (b != nullptr)
    {
        Lever(rb, n, pressed * radius, limit, lb);
    }
    float stop = speed / PatchWeight(a, la, b, lb, u);
    float j = fminf(stop, fmaxf(limit - *slowed, 0.0f));
    *slowed += j;
    const float impulse[3] = {-j * u[0], -j * u[1], -j * u[2]};
    mvfxBodyKick(a, la, impulse);
    if (b != nullptr)
    {
        const float back[3] = {-impulse[0], -impulse[1], -impulse[2]};
        mvfxBodyKick(b, lb, back);
    }
}

// The body's turn over the substep about unit n (to first order).
static float TurnAbout(const mvfxBody* b, const float n[3])
{
    const float inverse[4] = {-b->q0[0], -b->q0[1], -b->q0[2], b->q0[3]};
    float dq[4];
    mvfxMultiplyRotations(b->q, inverse, dq);
    float sign = dq[3] < 0.0f ? -2.0f : 2.0f;
    return sign * (dq[0] * n[0] + dq[1] * n[1] + dq[2] * n[2]);
}

// The bodies' weight for turning about unit n: their inverse inertias
// about it.
static float TwistWeight(const mvfxBody* a, const mvfxBody* b, const float n[3])
{
    float turned[3];
    InverseInertiaTimes(a, n, turned);
    float weight = Dot(n, turned);
    if (b != nullptr)
    {
        InverseInertiaTimes(b, n, turned);
        weight += Dot(n, turned);
    }
    return weight;
}

void mvfxTwistPatch(const mvfxRigidSolve* s, mvfxBody* a, mvfxBody* b, const float n[3],
                    float limit, float* twisted)
{
    float turn = TurnAbout(a, n) - (b != nullptr ? TurnAbout(b, n) : 0.0f);
    float weight = TwistWeight(a, b, n);
    if (turn == 0.0f || !(weight > 0.0f))
    {
        return;
    }
    float holding = fabsf(turn) / weight;
    if (!(*twisted + holding <= s->rigid->staticFriction * limit))
    {
        return;
    }
    *twisted += holding;
    float back[3];
    float l[3] = {-turn / weight * n[0], -turn / weight * n[1], -turn / weight * n[2]};
    InverseInertiaTimes(a, l, back);
    TurnBy(a->q, back);
    if (b != nullptr)
    {
        const float m[3] = {-l[0], -l[1], -l[2]};
        InverseInertiaTimes(b, m, back);
        TurnBy(b->q, back);
    }
}

void mvfxUnspinPatch(mvfxBody* a, mvfxBody* b, const float n[3], float limit, float* spun)
{
    float spin = Dot(a->w, n) - (b != nullptr ? Dot(b->w, n) : 0.0f);
    float weight = TwistWeight(a, b, n);
    if (spin == 0.0f || !(weight > 0.0f))
    {
        return;
    }
    float j = fminf(fabsf(spin) / weight, fmaxf(limit - *spun, 0.0f));
    *spun += j;
    float sign = spin > 0.0f ? -1.0f : 1.0f;
    float turned[3];
    const float l[3] = {sign * j * n[0], sign * j * n[1], sign * j * n[2]};
    InverseInertiaTimes(a, l, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        a->w[k] += turned[k];
    }
    if (b != nullptr)
    {
        const float m[3] = {-l[0], -l[1], -l[2]};
        InverseInertiaTimes(b, m, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            b->w[k] += turned[k];
        }
    }
}

int32_t mvfxPassIndex(int32_t first, int32_t end, int32_t step, bool backward)
{
    return backward ? end - 1 - (step - first) : step;
}

void mvfxCorrectBody(mvfxBody* b, float h)
{
    const float inverse[4] = {-b->q0[0], -b->q0[1], -b->q0[2], b->q0[3]};
    float dq[4];
    mvfxMultiplyRotations(b->q, inverse, dq);
    float sign = dq[3] < 0.0f ? -1.0f : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        b->v[k] = (b->x[k] - b->x0[k]) / h;
        b->w[k] = sign * 2.0f * dq[k] / h;
    }
}

float mvfxBounceTarget(const mvfxRigidSolve* s, float speed)
{
    float bounce = speed < -s->still ? s->rigid->restitution : 0.0f;
    return -bounce * fminf(speed, 0.0f);
}

// The life a collision takes: its share of the life left, the particle
// killed past its age.
static bool LoseLife(const mvfxRigidSolve* s, int32_t i, float loss)
{
    mvfxInstance* instance = s->instance;
    float* inverseLife = mvfxAttributeArray(instance, s->emitter, mvfx_attributeInverseLife);
    const uint32_t* low = mvfxAttributeArray(instance, s->emitter, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(instance, s->emitter, mvfx_attributeBirthHigh);
    inverseLife[i] /= 1.0f - loss;
    int64_t birth = (int64_t)(((uint64_t)high[i] << 32u) | low[i]);
    double age = (double)(s->tick - birth) / (double)MVFX_TICKS_PER_SECOND;
    return age * (double)inverseLife[i] >= 1.0;
}

void mvfxStrikeBody(const mvfxRigidSolve* s, int32_t i, const float point[3], const float normal[3])
{
    if ((s->flags[i] & MVFX_RIGID_STRUCK) != 0)
    {
        return;
    }
    s->flags[i] |= MVFX_RIGID_STRUCK;
    mvfxInstance* instance = s->instance;
    const mvfxCollision* collision = &instance->effect->emitters[s->emitter].collision;
    uint32_t* count = mvfxAttributeArray(instance, s->emitter, mvfx_attributeCollisions);
    count[i] = count[i] == UINT32_MAX ? UINT32_MAX : count[i] + 1u;
    if ((s->flags[i] & MVFX_RIGID_EVENTED) == 0)
    {
        s->flags[i] |= MVFX_RIGID_EVENTED;
        mvfxRecordEvent(instance, s->emitter,
                        &(mvfxEventFact){.kind = mvfx_eventCollision,
                                         .particle = i,
                                         .tick = s->tick,
                                         .placeTick = s->tick,
                                         .point = point,
                                         .normal = normal});
    }
    bool killed = collision->killOnCollision || collision->lifeLoss >= 1.0f ||
                  (collision->lifeLoss > 0.0f && LoseLife(s, i, collision->lifeLoss));
    instance->killScratch[i] |= killed ? 1 : 0;
}
