// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flocking and steering, each behavior alone against its formula in double
// on small flocks: separation, alignment, cohesion, the view cone, seeking
// with arrival, fleeing within its radius, a path open and closed, the
// force limit, a target from a parameter in the world; the neighbor
// attributes still stored; newborns unsteered; the checks.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

#define MOST    16
#define STEP    (MVFX_TICKS_PER_SECOND / 8)
#define SECONDS 0.125

// The particles before motion: their places, and their velocities after
// steering.
typedef struct Seen
{
    int32_t count;
    float place[MOST][3];
    float velocity[MOST][3];
    float neighbors[MOST];
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
        s_seen.neighbors[i] = a->neighborCount != NULL ? a->neighborCount[i] : -1.0f;
    }
}

static const mvfxHostModule s_capture = {.stage = mvfx_stageBeforeMotion, .function = Capture};

static mvfxEmitterDef Emitter(bool neighbors)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.hostModules = &s_capture;
    emitter.hostModuleCount = 1;
    emitter.neighbors =
        (mvfxNeighborDef){.enabled = neighbors, .source = -1, .radius = 4.0f, .maxNeighbors = MOST};
    emitter.steering = (mvfxSteeringDef){.enabled = true, .viewAngle = 3.14159274f};
    return emitter;
}

static mvfxContext* s_context;
static mvfxInstanceId s_instance;
static double s_origin;

// Spawns particles at places with velocities, steps a tick (their births)
// then a step whose start the module sees.
static void Run(const mvfxEmitterDef* emitter, const double* places, const float* velocities,
                int32_t count, int32_t parameterCount, const float* parameters)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    def.parameters = parameters;
    def.parameterCount = parameterCount;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.origin[0] = s_origin;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    memset(&s_seen, 0, sizeof s_seen);
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success && s_seen.count == 0,
          "newborns unsteered");
    CHECK(mvfxContext_StepTicks(s_context, STEP) == mvfx_success && s_seen.count == count,
          "stepped");
}

// Whether particle i's velocity is its spawned one plus an acceleration
// over the step.
static bool Moved(int32_t i, const float* spawned, const double acc[3])
{
    bool ok = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        double want = (double)spawned[3 * i + k] + acc[k] * SECONDS;
        ok = ok && fabs((double)s_seen.velocity[i][k] - want) <= 1e-5 * (1.0 + fabs(want));
    }
    return ok;
}

// A flock of four, the last beyond 2 of the rest, within 4 of all.
static const double s_places[12] = {0, 0, 0, 1, 0, 0, 0, 1.5, 0, 0, 0, 3.5};
static const float s_velocities[12] = {1, 0, 0, 0, 2, 0, -1, 0, 1, 0, 0, -2};

static void TestRules(void)
{
    // Each rule alone, within 2 (the last particle out of reach of all but
    // none), in double from the places the step started at.
    for (int32_t rule = 0; rule < 3; ++rule)
    {
        mvfxEmitterDef emitter = Emitter(true);
        float* weight = rule == 0   ? &emitter.steering.separation
                        : rule == 1 ? &emitter.steering.alignment
                                    : &emitter.steering.cohesion;
        float* radius = rule == 0   ? &emitter.steering.separationRadius
                        : rule == 1 ? &emitter.steering.alignmentRadius
                                    : &emitter.steering.cohesionRadius;
        *weight = 1.5f;
        *radius = 2.0f;
        Run(&emitter, s_places, s_velocities, 4, 0, NULL);
        bool ok = true;
        for (int32_t i = 0; i < 4; ++i)
        {
            double sum[3] = {0, 0, 0};
            int32_t n = 0;
            for (int32_t j = 0; j < 4; ++j)
            {
                double d[3];
                double square = 0.0;
                for (int32_t k = 0; k < 3; ++k)
                {
                    d[k] = (double)s_seen.place[j][k] - (double)s_seen.place[i][k];
                    square += d[k] * d[k];
                }
                if (j == i || !(square < 4.0))
                {
                    continue;
                }
                n += 1;
                for (int32_t k = 0; k < 3; ++k)
                {
                    sum[k] += rule == 0   ? -d[k] / square
                              : rule == 1 ? (double)s_velocities[3 * j + k]
                                          : (double)s_seen.place[j][k];
                }
            }
            double acc[3] = {0, 0, 0};
            for (int32_t k = 0; n > 0 && k < 3; ++k)
            {
                double own =
                    rule == 1 ? (double)s_velocities[3 * i + k] : (double)s_seen.place[i][k];
                acc[k] = 1.5 * (rule == 0 ? sum[k] : sum[k] / n - own);
            }
            ok = ok && Moved(i, s_velocities, acc) && s_seen.neighbors[i] == 3.0f;
        }
        CHECK(ok, "each rule alone as its formula; the attributes stored");
        mvfxDestroyContext(s_context);
    }
}

static void TestView(void)
{
    // Heading +x with a half angle of a right angle: the neighbor ahead and
    // the one beside are seen, the one behind is not.
    mvfxEmitterDef emitter = Emitter(true);
    emitter.steering.cohesion = 1.0f;
    emitter.steering.cohesionRadius = 4.0f;
    emitter.steering.viewAngle = 1.5707964f;
    static const double places[12] = {0, 0, 0, 1, 0, 0, 0.1, 1, 0, -1, 0, 0};
    static const float velocities[12] = {1, 0, 0};
    Run(&emitter, places, velocities, 4, 0, NULL);
    // Ahead and beside (a little ahead, as the first tick moved it): their
    // mean (0.55, 0.5) less its place.
    const double acc[3] = {(1.0 + (double)s_seen.place[2][0]) / 2.0 - (double)s_seen.place[0][0],
                           0.5 - (double)s_seen.place[0][1], 0.0};
    CHECK(Moved(0, velocities, acc), "the view cone");
    mvfxDestroyContext(s_context);
}

static void TestViewEdges(void)
{
    // A view of 0 sees only straight ahead: the neighbor on the heading's
    // line, not the one a little off it. A view of pi sees the one exactly
    // behind. At rest, a narrow view sees all.
    mvfxEmitterDef emitter = Emitter(true);
    emitter.steering.cohesion = 1.0f;
    emitter.steering.cohesionRadius = 4.0f;
    emitter.steering.viewAngle = 0.0f;
    static const double places[9] = {0, 0, 0, 2, 0, 0, 2, 0.5, 0};
    static const float ahead[9] = {1, 0, 0, 1, 0, 0, 1, 0, 0};
    Run(&emitter, places, ahead, 3, 0, NULL);
    const double one[3] = {(double)s_seen.place[1][0] - (double)s_seen.place[0][0], 0, 0};
    CHECK(Moved(0, ahead, one), "a view of 0, straight ahead");
    mvfxDestroyContext(s_context);
    emitter.steering.viewAngle = 3.14159274f;
    static const double behind[6] = {0, 0, 0, -2, 0, 0};
    Run(&emitter, behind, ahead, 2, 0, NULL);
    const double back[3] = {(double)s_seen.place[1][0] - (double)s_seen.place[0][0], 0, 0};
    CHECK(Moved(0, ahead, back), "a view of pi, behind too");
    mvfxDestroyContext(s_context);
    emitter.steering.viewAngle = 0.5f;
    static const float rest[6] = {0};
    Run(&emitter, behind, rest, 2, 0, NULL);
    const double resting[3] = {-2.0, 0, 0};
    CHECK(Moved(0, rest, resting), "at rest, all");
    mvfxDestroyContext(s_context);
}

static void TestRuleEdges(void)
{
    // Exactly the cohesion radius away: left out. At the same place:
    // separation has no direction, and adds nothing.
    mvfxEmitterDef emitter = Emitter(true);
    emitter.steering.cohesion = 1.0f;
    emitter.steering.cohesionRadius = 2.0f;
    emitter.steering.separation = 1.0f;
    emitter.steering.separationRadius = 2.0f;
    static const double places[9] = {0, 0, 0, 2, 0, 0, 0, 0, 0};
    static const float rest[9] = {0};
    Run(&emitter, places, rest, 3, 0, NULL);
    const double none[3] = {0, 0, 0};
    CHECK(Moved(0, rest, none) && Moved(2, rest, none), "the radius exclusive; no direction");
    mvfxDestroyContext(s_context);
}

static void TestPoints(void)
{
    // Seeking (5, 0, 0) at 2 with a weight of 3 from rest and moving; one
    // within the arrival radius of 2 at half the distance wants half the
    // speed.
    mvfxEmitterDef emitter = Emitter(false);
    emitter.steering.cruiseSpeed = 2.0f;
    emitter.steering.seek = (mvfxSteerPoint){.weight = 3.0f, .point = {5, 0, 0}, .radius = 2.0f};
    static const double places[9] = {0, 0, 0, 0, 4, 0, 4, 0, 0};
    static const float velocities[9] = {0, 0, 0, 1, 0, 0, 0, 0, 0};
    Run(&emitter, places, velocities, 3, 0, NULL);
    double d = sqrt(25.0 + (double)s_seen.place[1][1] * (double)s_seen.place[1][1]);
    double x0 = 5.0 - (double)s_seen.place[0][0];
    const double seek0[3] = {3.0 * 2.0 * x0 / fabs(x0), 0, 0};
    const double seek1[3] = {3.0 * (2.0 * 5.0 / d - 1.0),
                             3.0 * 2.0 * -(double)s_seen.place[1][1] / d, 0};
    const double arrive[3] = {3.0 * 2.0 * (5.0 - (double)s_seen.place[2][0]) / 2.0, 0, 0};
    CHECK(Moved(0, velocities, seek0) && Moved(1, velocities, seek1) &&
              Moved(2, velocities, arrive),
          "seeking, arriving");
    mvfxDestroyContext(s_context);
    // Fleeing (0, 0, 0) within 3 at 2: the near one, not the far one.
    emitter.steering.seek.weight = 0.0f;
    emitter.steering.flee = (mvfxSteerPoint){.weight = 1.0f, .radius = 3.0f};
    static const double far[6] = {0, 2, 0, 0, 5, 0};
    Run(&emitter, far, velocities, 2, 0, NULL);
    const double flee[3] = {0, 2.0, 0};
    const double still[3] = {0, 0, 0};
    CHECK(Moved(0, velocities, flee) && Moved(1, velocities, still), "fleeing within its radius");
    mvfxDestroyContext(s_context);
}

static void TestForceAndParameter(void)
{
    // A target from a parameter in the world, the instance at x = 100; the
    // summed steering held to a force of 0.5.
    mvfxEmitterDef emitter = Emitter(false);
    emitter.steering.cruiseSpeed = 10.0f;
    emitter.steering.maxForce = 0.5f;
    emitter.steering.seek = (mvfxSteerPoint){
        .weight = 100.0f, .space = mvfx_spaceWorld, .fromParameter = true, .parameter = 0};
    static const float parameter[4] = {100, 0, -7, 0};
    static const double place[3] = {100, 0, 0};
    static const float rest[3] = {0, 0, 0};
    s_origin = 100.0;
    Run(&emitter, place, rest, 1, 1, parameter);
    s_origin = 0.0;
    const double held[3] = {0, 0, -0.5};
    CHECK(Moved(0, rest, held), "toward the parameter, held to the force");
    mvfxDestroyContext(s_context);
    // Fleeing with no radius: everywhere.
    emitter.steering.seek = (mvfxSteerPoint){0};
    emitter.steering.maxForce = 0.0f;
    emitter.steering.flee = (mvfxSteerPoint){.weight = 1.0f, .point = {0, -1000, 0}};
    Run(&emitter, s_places, rest, 1, 0, NULL);
    const double away[3] = {0, 10.0, 0};
    CHECK(Moved(0, rest, away), "fleeing everywhere");
    mvfxDestroyContext(s_context);
}

static void TestPath(void)
{
    // An open path along x to 10, radius 0.5, lookahead 1: a particle at
    // (5, 3) heading +x at 1 predicts (6, 3), is 3 off, and seeks (7, 0) at
    // 2; one on the path is left alone; one past the end seeks the end.
    mvfxEmitterDef emitter = Emitter(false);
    static const float line[6] = {0, 0, 0, 10, 0, 0};
    emitter.steering.cruiseSpeed = 2.0f;
    emitter.steering.path = (mvfxSteerPath){
        .weight = 1.0f, .points = line, .pointCount = 2, .radius = 0.5f, .lookahead = 1.0f};
    static const double places[9] = {5, 3, 0, 5, 0.2, 0, 12, 3, 0};
    static const float velocities[9] = {1, 0, 0, 1, 0, 0, 1, 0, 0};
    Run(&emitter, places, velocities, 3, 0, NULL);
    const float* p = s_seen.place[0];
    // Its target: the predicted place's nearest, on by 1.
    double target0[3] = {(double)p[0] + 1.0 + 1.0, 0, 0};
    double d0[3] = {target0[0] - (double)p[0], -(double)p[1], 0};
    double l0 = sqrt(d0[0] * d0[0] + d0[1] * d0[1]);
    const double acc0[3] = {2.0 * d0[0] / l0 - 1.0, 2.0 * d0[1] / l0, 0};
    const double none[3] = {0, 0, 0};
    const float* e = s_seen.place[2];
    double d2[3] = {10.0 - (double)e[0], -(double)e[1], 0};
    double l2 = sqrt(d2[0] * d2[0] + d2[1] * d2[1]);
    const double acc2[3] = {2.0 * d2[0] / l2 - 1.0, 2.0 * d2[1] / l2, 0};
    CHECK(Moved(0, velocities, acc0) && Moved(1, velocities, none) && Moved(2, velocities, acc2),
          "an open path");
    mvfxDestroyContext(s_context);
    // A closed square of side 4: near the last corner heading -y, on along
    // the closing side and round onto the first.
    static const float square[12] = {0, 0, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0};
    emitter.steering.path.points = square;
    emitter.steering.path.pointCount = 4;
    emitter.steering.path.closed = true;
    emitter.steering.path.lookahead = 2.0f;
    static const double corner[3] = {-1, 1, 0};
    static const float down[3] = {0, -1, 0};
    Run(&emitter, corner, down, 1, 0, NULL);
    // Predicts (-1, -1): nearest (0, 0), the closing side's end, and on by
    // 2 along the first side to (2, 0).
    const float* c = s_seen.place[0];
    double d[3] = {2.0 - (double)c[0], -(double)c[1], 0};
    double l = sqrt(d[0] * d[0] + d[1] * d[1]);
    const double acc[3] = {2.0 * d[0] / l, 2.0 * d[1] / l + 1.0, 0};
    CHECK(Moved(0, down, acc), "round a closed path");
    mvfxDestroyContext(s_context);
    // At the square's center at rest, every side is 2 away: the first, so
    // it seeks (2, 0).
    static const double center[3] = {2, 2, 0};
    static const float rest[3] = {0, 0, 0};
    Run(&emitter, center, rest, 1, 0, NULL);
    const double first[3] = {0, -2.0, 0};
    CHECK(Moved(0, rest, first), "the first nearest side");
    mvfxDestroyContext(s_context);
    // On by 30 round a path of 16: the predicted place (5, 1) is nearest
    // (4, 1) on the second side, and 30 less 16 is 14 on: 3 to (4, 4), 4
    // to (0, 4), 4 to (0, 0), 3 to (3, 0).
    emitter.steering.path.lookahead = 0.1f;
    emitter.steering.path.radius = 0.0f;
    static const double side[3] = {5, -29, 0};
    static const float up[3] = {0, 300, 0};
    Run(&emitter, side, up, 1, 0, NULL);
    const float* w = s_seen.place[0];
    double dw[3] = {3.0 - (double)w[0], -(double)w[1], 0};
    double lw = sqrt(dw[0] * dw[0] + dw[1] * dw[1]);
    const double round[3] = {2.0 * dw[0] / lw, 2.0 * dw[1] / lw - 300.0, 0};
    CHECK(Moved(0, up, round), "on by more than the whole path");
    mvfxDestroyContext(s_context);
    // An open path of two sides ends at its last point: on past it, the
    // target stays there.
    static const float bend[9] = {0, 0, 0, 10, 0, 0, 10, 10, 0};
    emitter.steering.path = (mvfxSteerPath){
        .weight = 1.0f, .points = bend, .pointCount = 3, .radius = 0.5f, .lookahead = 2.0f};
    static const double end[3] = {13, 9, 0};
    static const float north[3] = {0, 1, 0};
    Run(&emitter, end, north, 1, 0, NULL);
    const float* e2 = s_seen.place[0];
    double de[3] = {10.0 - (double)e2[0], 10.0 - (double)e2[1], 0};
    double le = sqrt(de[0] * de[0] + de[1] * de[1]);
    const double last[3] = {2.0 * de[0] / le, 2.0 * de[1] / le - 1.0, 0};
    CHECK(Moved(0, north, last), "an open path's end");
    mvfxDestroyContext(s_context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    static const float line[6] = {0, 0, 0, 1, 0, 0};
    static const float bad[6] = {0, 0, 0, NAN, 0, 0};
    mvfxEmitterDef emitter = Emitter(true);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 17; ++rule)
    {
        emitter = Emitter(true);
        mvfxSteeringDef* s = &emitter.steering;
        s->path = (mvfxSteerPath){.weight = 1.0f, .points = line, .pointCount = 2};
        switch (rule)
        {
        case 0:
            emitter.neighbors.enabled = false, s->separation = 1.0f, s->separationRadius = 1.0f;
            break;
        case 1:
            s->alignment = 1.0f, s->alignmentRadius = 5.0f;
            break;
        case 2:
            s->cohesion = 1.0f, s->cohesionRadius = 0.0f;
            break;
        case 3:
            s->separation = -1.0f;
            break;
        case 4:
            s->viewAngle = 3.2f;
            break;
        case 5:
            s->cruiseSpeed = -1.0f;
            break;
        case 6:
            s->maxForce = NAN;
            break;
        case 7:
            s->seek = (mvfxSteerPoint){.weight = 1.0f, .fromParameter = true, .parameter = 0};
            break;
        case 8:
            s->flee = (mvfxSteerPoint){.weight = 1.0f, .space = 2};
            break;
        case 9:
            s->seek = (mvfxSteerPoint){.weight = 1.0f, .radius = -1.0f};
            break;
        case 10:
            s->flee = (mvfxSteerPoint){.weight = 1.0f, .point = {2e6f, 0, 0}};
            break;
        case 11:
            s->path.pointCount = 1;
            break;
        case 12:
            s->path.pointCount = MVFX_MAX_PATH_POINTS + 1;
            break;
        case 13:
            s->path.points = NULL;
            break;
        case 14:
            s->path.points = bad;
            break;
        case 15:
            s->path.lookahead = 11.0f;
            break;
        default:
            s->path.space = 2;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = rule == 10 || rule == 12 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(s_context, &def, &effect) == want;
    }
    CHECK(refused == 17, "every check");
    emitter = Emitter(true);
    emitter.steering.separation = MVFX_MAX_EXTENT;
    emitter.steering.separationRadius = 4.0f;
    emitter.steering.path = (mvfxSteerPath){.points = NULL, .pointCount = 0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success,
          "the bounds; a path of no weight unread");
    emitter.steering.enabled = false;
    emitter.steering.viewAngle = NAN;
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success,
          "steering not enabled unread");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestRules();
    TestView();
    TestViewEdges();
    TestRuleEdges();
    TestPoints();
    TestForceAndParameter();
    TestPath();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
