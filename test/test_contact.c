// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particle contacts: an overlapping pair parted in one pass with its
// momentum kept, restitution and friction as their shares, coincident
// particles left alone, a pair parting not kicked, radii from sizes, a
// cluster against brute force over all pairs in double (no pair past
// the tolerance after the passes), contacts capped and counted once a
// step, the checks.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

#define MOST 64

// The particles after motion and contacts.
typedef struct Seen
{
    int32_t count;
    float place[MOST][3];
    float velocity[MOST][3];
} Seen;

static Seen s_seen;

static void Capture(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* a = &call->particles;
    s_seen.count = a->count;
    for (int32_t i = 0; i < a->count && i < MOST; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            s_seen.place[i][k] = a->position[k][i];
            s_seen.velocity[i][k] = a->velocity[k][i];
        }
    }
}

static const mvfxHostModule s_capture = {.stage = mvfx_stageAfterMotion, .function = Capture};

// Radius 0.5 with no size stored, one pass, no restitution or friction.
static mvfxEmitterDef Emitter(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.hostModules = &s_capture;
    emitter.hostModuleCount = 1;
    emitter.contacts =
        (mvfxContactDef){.enable = true, .radiusScale = 0.5f, .iterations = 1, .maxContacts = 16};
    return emitter;
}

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// Spawns particles and steps 1 tick: they move by their velocities over
// it, then keep apart.
static void Run(const mvfxEmitterDef* emitter, const double* places, const float* velocities,
                int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    memset(&s_seen, 0, sizeof s_seen);
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success && s_seen.count == count, "stepped");
}

static bool Near(float a, double b)
{
    return fabs((double)a - b) <= 1e-5 * (1.0 + fabs(b));
}

static void TestPair(void)
{
    // At rest 0.6 apart, radii 0.5: each moves 0.20025 out, ending at the
    // rest distance, 1.0005 apart.
    mvfxEmitterDef emitter = Emitter();
    static const double places[6] = {0, 0, 0, 0.6, 0, 0};
    static const float rest[6] = {0};
    Run(&emitter, places, rest, 2);
    CHECK(Near(s_seen.place[0][0], -0.20025) && Near(s_seen.place[1][0], 0.80025) &&
              s_seen.place[0][1] == 0.0f && s_seen.velocity[0][0] == 0.0f,
          "parted in one pass, no velocity from it");
    mvfxDestroyContext(s_context);
    // Closing at 1 each with restitution 0.5: each goes back at 0.5, the
    // momentum kept.
    emitter.contacts.restitution = 0.5f;
    static const float closing[6] = {1, 0, 0, -1, 0, 0};
    Run(&emitter, places, closing, 2);
    CHECK(Near(s_seen.velocity[0][0], -0.5) && Near(s_seen.velocity[1][0], 0.5) &&
              s_seen.velocity[0][0] + s_seen.velocity[1][0] == 0.0f,
          "restitution's share, momentum kept");
    mvfxDestroyContext(s_context);
    // Closing and sliding, no restitution and all friction: they end
    // moving together.
    emitter.contacts.restitution = 0.0f;
    emitter.contacts.friction = 1.0f;
    static const float sliding[6] = {1, 2, 0, -1, -1, 0};
    Run(&emitter, places, sliding, 2);
    CHECK(Near(s_seen.velocity[0][0], 0.0) && Near(s_seen.velocity[1][0], 0.0) &&
              Near(s_seen.velocity[0][1], 0.5) && Near(s_seen.velocity[1][1], 0.5),
          "friction's share");
    mvfxDestroyContext(s_context);
    // Touching and parting: no kick.
    static const double touching[6] = {0, 0, 0, 1, 0, 0};
    static const float parting[6] = {-1, 0, 0, 1, 0, 0};
    Run(&emitter, touching, parting, 2);
    CHECK(s_seen.velocity[0][0] == -1.0f && s_seen.velocity[1][0] == 1.0f, "parting, no kick");
    mvfxDestroyContext(s_context);
}

static void TestEdges(void)
{
    // At one place: no line to part them along.
    mvfxEmitterDef emitter = Emitter();
    emitter.contacts.restitution = 1.0f;
    static const double same[6] = {1, 2, 3, 1, 2, 3};
    static const float rest[6] = {0};
    Run(&emitter, same, rest, 2);
    CHECK(s_seen.place[0][0] == 1.0f && s_seen.place[1][2] == 3.0f && s_seen.velocity[0][0] == 0.0f,
          "coincident, left alone");
    mvfxDestroyContext(s_context);
    // Sizes of 2 at a scale of 0.25: radii 0.5 again.
    emitter = Emitter();
    emitter.contacts.radiusScale = 0.25f;
    emitter.size =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {2, 2, 2, 0}, .max = {2, 2, 2, 0}};
    static const double places[6] = {0, 0, 0, 0.6, 0, 0};
    Run(&emitter, places, rest, 2);
    CHECK(Near(s_seen.place[0][0], -0.20025) && Near(s_seen.place[1][0], 0.80025),
          "radii from sizes");
    mvfxDestroyContext(s_context);
}

static void TestBoundaries(void)
{
    // A touching B exactly, C overlapping B by 0.4: B moves 0.20025 from C
    // alone (to the rest distance), A's touch not counted in its average.
    mvfxEmitterDef emitter = Emitter();
    static const double line[9] = {0, 0, 0, 1, 0, 0, 1.6, 0, 0};
    static const float rest[9] = {0};
    Run(&emitter, line, rest, 3);
    CHECK(Near(s_seen.place[1][0], 0.79975), "touching exactly is not a contact");
    mvfxDestroyContext(s_context);
    // A and B moving together (not closing), C closing on B: B's impulse
    // from C alone stops it.
    static const double row[9] = {0, 0, 0, 1, 0, 0, 2, 0, 0};
    static const float moving[9] = {1, 0, 0, 1, 0, 0, -1, 0, 0};
    Run(&emitter, row, moving, 3);
    CHECK(s_seen.velocity[1][0] == 0.0f && s_seen.velocity[2][0] == 0.0f,
          "a contact not closing is not averaged");
    mvfxDestroyContext(s_context);
    // Within the slop past the radii's sum, two cells of the radius apart,
    // closing: kicked.
    static const double apart[6] = {0.9999, 0, 0, 2.0004, 0, 0};
    static const float closing[6] = {1, 0, 0, -1, 0, 0};
    Run(&emitter, apart, closing, 2);
    CHECK(s_seen.velocity[0][0] == 0.0f && s_seen.velocity[1][0] == 0.0f, "within the slop");
    mvfxDestroyContext(s_context);
}

static uint64_t Capped(void)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_context, s_instance, 0, &stats) == mvfx_success, "stats");
    return stats.contactCappedParticleCount;
}

// The largest overlap of any pair in double, as a share of the radii's sum.
static double WorstOverlap(void)
{
    double worst = 0.0;
    for (int32_t i = 0; i < s_seen.count; ++i)
    {
        for (int32_t j = i + 1; j < s_seen.count; ++j)
        {
            double square = 0.0;
            for (int32_t k = 0; k < 3; ++k)
            {
                double d = (double)s_seen.place[i][k] - (double)s_seen.place[j][k];
                square += d * d;
            }
            double overlap = (1.0 - sqrt(square)) / 1.0;
            worst = overlap > worst ? overlap : worst;
        }
    }
    return worst;
}

static void TestCluster(void)
{
    // Forty particles at rest in a box of 5 where they overlap: sixteen
    // passes leave no pair overlapping past a hundredth of the radii's sum,
    // the first already better than none.
    static double places[120];
    static const float rest[120] = {0};
    uint32_t random = 7u;
    for (int32_t i = 0; i < 120; ++i)
    {
        random = random * 1664525u + 1013904223u;
        places[i] = 5.0 * (double)(random >> 8u) / 16777216.0;
    }
    mvfxEmitterDef emitter = Emitter();
    emitter.contacts.iterations = 0;
    emitter.contacts.enable = false;
    Run(&emitter, places, rest, 40);
    double before = WorstOverlap();
    mvfxDestroyContext(s_context);
    emitter.contacts.enable = true;
    emitter.contacts.iterations = 1;
    Run(&emitter, places, rest, 40);
    double once = WorstOverlap();
    mvfxDestroyContext(s_context);
    emitter.contacts.iterations = MVFX_MAX_CONTACT_ITERATIONS;
    Run(&emitter, places, rest, 40);
    double after = WorstOverlap();
    CHECK(before > 0.5 && once < before && after < 0.01 && Capped() == 0,
          "no pair past the tolerance after the passes");
    mvfxDestroyContext(s_context);
}

static void TestCapped(void)
{
    // Five overlapping, at most two each: every one capped, counted once a
    // step whatever the passes.
    mvfxEmitterDef emitter = Emitter();
    emitter.contacts.maxContacts = 2;
    emitter.contacts.iterations = 3;
    static const double places[15] = {0, 0, 0, 0.1, 0, 0, 0, 0.1, 0, 0, 0, 0.1, 0.1, 0.1, 0.1};
    static const float rest[15] = {0};
    Run(&emitter, places, rest, 5);
    CHECK(Capped() == 5, "capped, counted once a step");
    CHECK(mvfxInstance_Restart(s_context, s_instance) == mvfx_success && Capped() == 0,
          "a restart counts again");
    mvfxDestroyContext(s_context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Emitter();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 9; ++rule)
    {
        emitter = Emitter();
        mvfxContactDef* c = &emitter.contacts;
        switch (rule)
        {
        case 0:
            c->radiusScale = 0.0f;
            break;
        case 1:
            c->radiusScale = 2.0f * MVFX_MAX_EXTENT;
            break;
        case 2:
            c->iterations = 0;
            break;
        case 3:
            c->iterations = MVFX_MAX_CONTACT_ITERATIONS + 1;
            break;
        case 4:
            c->maxContacts = 0;
            break;
        case 5:
            c->maxContacts = MVFX_MAX_NEIGHBORS + 1;
            break;
        case 6:
            c->restitution = 1.5f;
            break;
        case 7:
            c->friction = -0.1f;
            break;
        default:
            c->friction = NAN;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = rule == 1 || rule == 3 || rule == 5 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(s_context, &def, &effect, NULL) == want;
    }
    CHECK(refused == 9, "every check");
    emitter = Emitter();
    emitter.contacts = (mvfxContactDef){.enable = true,
                                        .radiusScale = MVFX_MAX_EXTENT,
                                        .iterations = MVFX_MAX_CONTACT_ITERATIONS,
                                        .maxContacts = MVFX_MAX_NEIGHBORS,
                                        .restitution = 1.0f,
                                        .friction = 1.0f};
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "the bounds");
    emitter.contacts.enable = false;
    emitter.contacts.iterations = -3;
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "not enabled, unread");
    // A beam takes no contacts and no steering.
    mvfxEmitterDef beam = mvfxDefaultEmitterDef();
    beam.rate = 0.0f;
    beam.capacity = 8;
    beam.ribbon.ids = mvfx_ribbonOne;
    beam.beam = (mvfxBeamDef){.enable = true, .segments = 4, .end = {.point = {1, 0, 0}}};
    def.emitters = &beam;
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "a beam");
    beam.contacts = Emitter().contacts;
    refused = mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_errorInvalid;
    beam.contacts.enable = false;
    beam.steering.enable = true;
    refused += mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_errorInvalid;
    CHECK(refused == 2, "not on a beam");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestPair();
    TestEdges();
    TestBoundaries();
    TestCluster();
    TestCapped();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
