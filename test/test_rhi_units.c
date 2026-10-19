// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Units (chains and sheets) on a real device (lavapipe on CI) beside the
// CPU path, compared after every step (rhi_harness.h): their points'
// places and velocities within the tolerance (the projections take sqrt
// and division). Covered: rigid and compliant chains over several
// substeps with damping, sheets with shear and bending under a moving
// instance in the world's space, a capacity that binds (whole units
// refused), colliders with life lost and turns, kills by contacts, kill
// rules and kill radii (a unit killed whole), units born from events,
// pins held still, to the instance, the host's frames and a source's
// particles, and what units are still refused (tearing, aerodynamics,
// self-collision); their events (births, deaths, a point's first contact
// a step).
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite emitter of chains of points, each spacing apart.
static mvfxEmitterDef Chains(int32_t points, int32_t substeps)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.rate = 120.0f;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                 .points = points,
                                 .spacing = 0.05f,
                                 .direction = {1, 0, 0},
                                 .downDirection = {0, 1, 0},
                                 .substeps = substeps,
                                 .damping = 2.0f};
    return emitter;
}

// Sheets of across by rows points.
static mvfxEmitterDef Sheets(int32_t across, int32_t rows, int32_t substeps)
{
    mvfxEmitterDef emitter = Chains(across, substeps);
    emitter.rate = 30.0f;
    emitter.unit.kind = mvfx_unitSheet;
    emitter.unit.rows = rows;
    emitter.unit.downDirection[0] = 0.0f;
    emitter.unit.downDirection[2] = 1.0f;
    emitter.unit.downDirection[1] = 0.0f;
    emitter.unit.shearCompliance = 1e-4f;
    emitter.unit.bendCompliance = 1e-3f;
    return emitter;
}

// A moving instance turns and scales at the harness's pace but moves at a
// thousandth of it: a unit's velocity takes its correction over a
// substep's seconds, so a point the instance leaves hundreds of metres
// from the origin, where a binary32 place's last unit is about 3e-5,
// carries one unit's rounding as 7e-3 into its velocity on either path.
static void Moved(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, bool moving,
                  const char* what)
{
    // Within 1e-3: a projection carries a point's difference to its
    // neighbours, and the velocity takes the substep's correction over the
    // substep's seconds.
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .moving = moving,
                   .pace = 0.001,
                   .tolerance = 1e-3f},
            what);
}

static void TestChains(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Chains(8, 4);
    Moved(gpu, kernels, &emitter, false, "rigid chains of eight over four substeps");
    emitter = Chains(16, 8);
    emitter.unit.stretchCompliance = 1e-3f;
    emitter.unit.bendCompliance = 1e-2f;
    Moved(gpu, kernels, &emitter, false, "compliant chains over eight substeps");
    // Far more births than room: whole units refused.
    emitter = Chains(8, 2);
    emitter.rate = 2000.0f;
    Moved(gpu, kernels, &emitter, false, "chains past the capacity");
}

static void TestSheets(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Sheets(8, 4, 2);
    Moved(gpu, kernels, &emitter, false, "sheets of eight by four");
    emitter.space = mvfx_spaceWorld;
    Moved(gpu, kernels, &emitter, true, "sheets in the world, the instance moving");
}

// A run of an emitter of units that must meet colliders, or must kill.
// Within 2e-3: a contact's place, found in binary32 by either path, comes
// into the velocity over a substep's seconds (1/240 here), so the
// rounding of a bounce's point is about 1.6e-3 of the velocity at most.
static void Met(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, bool killing,
                const char* what)
{
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .colliding = !killing,
                   .killing = killing,
                   .tolerance = 2e-3f},
            what);
}

static void TestColliders(Gpu* gpu, mvfxGpu* kernels)
{
    // Chains falling on a floor and a sphere, bouncing with friction, a
    // share of a point's life lost at each contact (a unit lives by its
    // first point), turning by spins read over life at the age the lost
    // life leaves, under an angular drag.
    static const mvfxCollider own[2] = {
        {.kind = mvfx_colliderPlane, .center = {0, -0.4f, 0}, .normal = {0, 1, 0.1f}},
        {.kind = mvfx_colliderSphere, .center = {0.3f, 0.3f, 0}, .radius = 0.2f}};
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 0, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 8, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = Chains(8, 4);
    emitter.colliders = own;
    emitter.colliderCount = 2;
    emitter.collision = (mvfxCollision){
        .enabled = true, .restitution = 0.5f, .friction = 0.2f, .lifeLoss = 0.2f, .radius = 0.02f};
    emitter.spin = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {2, 4}};
    emitter.motion.angularDrag = 1.5f;
    emitter.overLife.rotation = (mvfxRotationOverLife){
        .enabled = true, .scale = {.value = 1, .curve = {.keys = keys, .keyCount = 2}}};
    Met(gpu, kernels, &emitter, false, "chains bounced off a floor and a sphere, life lost");
    // Their events: births, deaths by age and a point's first contact a
    // step, taken at its substep, off the sphere alone (points resting on
    // a floor graze it each substep, where the paths' places, apart by
    // their tolerance, touch it on one path only).
    emitter.colliders = own + 1;
    emitter.colliderCount = 1;
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath | mvfx_eventMaskCollision;
    emitter.eventCapacity = 4096;
    // Sizes over life, which a contact's event takes at the life it met
    // the contact with.
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 1, .curve = {.keys = keys, .keyCount = 2}}}};
    Met(gpu, kernels, &emitter, false, "chains' births, deaths and first contacts");
    emitter.overLife.size = (mvfxSizeOverLife){0};
    emitter.colliders = own;
    emitter.colliderCount = 2;
    emitter.events = 0;
    // Contacts that kill: a unit goes whole; or that take the life left
    // (a second contact ends any of these lives).
    emitter.collision = (mvfxCollision){.enabled = true, .killOnCollision = true};
    Met(gpu, kernels, &emitter, true, "chains a contact kills whole");
    emitter.collision = (mvfxCollision){.enabled = true, .restitution = 0.5f, .lifeLoss = 0.9f};
    Met(gpu, kernels, &emitter, true, "chains the life a contact takes kills whole");
}

static void TestKills(Gpu* gpu, mvfxGpu* kernels)
{
    // Sheets through a kill sphere and past an attractor's kill radius:
    // a point's kill within any substep kills its sheet.
    static const mvfxKillRule sphere = {
        .kind = mvfx_killInsideSphere, .center = {0.2f, 0.4f, 0.1f}, .radius = 0.15f};
    mvfxEmitterDef emitter = Sheets(8, 4, 3);
    emitter.killRules = &sphere;
    emitter.killRuleCount = 1;
    Met(gpu, kernels, &emitter, true, "sheets a kill rule kills whole");
    emitter.events = mvfx_eventMaskDeath;
    emitter.eventCapacity = 4096;
    Met(gpu, kernels, &emitter, true, "the deaths of sheets killed whole");
    emitter.events = 0;
    static const mvfxForce attractor = {.kind = mvfx_forceAttractor,
                                        .point = {-0.2f, 0.6f, 0},
                                        .strength = 2.0f,
                                        .softening = 0.2f,
                                        .killRadius = 0.1f};
    emitter.killRules = NULL;
    emitter.killRuleCount = 0;
    emitter.forces = &attractor;
    emitter.forceCount = 1;
    Met(gpu, kernels, &emitter, true, "sheets an attractor's kill radius kills whole");
}

// A run within 2e-3 (neighbors' forces and contacts turn by the places'
// differences over their distances, and a unit carries a point's to the
// rest), of one or two emitters.
static void Beside(Gpu* gpu, mvfxGpu* kernels, const Run* base, const char* what)
{
    Run run = *base;
    run.tolerance = 2e-3f;
    Compare(gpu, kernels, &run, what);
}

static void TestNeighbors(Gpu* gpu, mvfxGpu* kernels)
{
    // Chains flocking by their points' neighbors, then pushed and pulled
    // by a pair force, the CPU path taking the GPU's places and velocities
    // each step: a chain's own points are its neighbors, its constraints
    // take back most of their kick, and what is left is the rounding,
    // which runs apart over steps.
    mvfxEmitterDef emitter = Chains(8, 4);
    emitter.neighbors =
        (mvfxNeighborDef){.enabled = true, .source = -1, .radius = 0.3f, .maxNeighbors = 32};
    emitter.steering = (mvfxSteeringDef){.enabled = true,
                                         .separation = 0.5f,
                                         .separationRadius = 0.15f,
                                         .alignment = 0.4f,
                                         .alignmentRadius = 0.3f,
                                         .cohesion = 0.3f,
                                         .cohesionRadius = 0.25f,
                                         .viewAngle = 2.0f};
    Beside(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .adopting = true},
           "chains steered by their neighbors");
    emitter.steering = (mvfxSteeringDef){0};
    emitter.pairForce = (mvfxPairForceDef){
        .enabled = true, .radius = 0.2f, .strength = (mvfxLifeCurve){.value = -2.0f}};
    Beside(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .adopting = true},
           "chains under a pair force");
}

static void TestContacts(Gpu* gpu, mvfxGpu* kernels)
{
    // Sheets whose points are kept apart from other sheets' after the
    // births, as particles' are. Their radii (at most 0.02) stay below
    // half the spacing: a sheet's own points held at the spacing by its
    // constraints and pushed to their radii by contacts would rest at the
    // contact's edge, met on one path only by rounding. Contacts enough
    // for every point (a sheet's points lie at equal distances, so a cap
    // would choose among ties by rounding).
    mvfxEmitterDef emitter = Sheets(8, 4, 2);
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.02f, 0.02f, 0.02f}, .max = {0.04f, 0.04f, 0.04f}};
    emitter.contacts = (mvfxContactDef){.enabled = true,
                                        .radiusScale = 0.5f,
                                        .iterations = 2,
                                        .maxContacts = 32,
                                        .restitution = 0.3f,
                                        .friction = 0.2f};
    Beside(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .adopting = true},
           "sheets whose points are kept apart");
}

static void TestHostBirths(Gpu* gpu, mvfxGpu* kernels)
{
    // The host's chains at its places, beside the emitter's own.
    static const double positions[3 * 4] = {0.5, 0, 0, -0.5, 0.2, 0, 0, 0.3, 0.4, 0, 0, 0};
    static const float velocities[3 * 4] = {1, 2, 0, 0, 3, 1, -1, 2, 0, 0, 1, 0};
    const mvfxSpawnRequest given = {.count = 4, .positions = positions, .velocities = velocities};
    mvfxEmitterDef emitter = Chains(8, 2);
    emitter.hostSpawnCapacity = 4;
    Beside(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .spawn = &given},
           "the host's chains");
}

static void TestEventBirths(Gpu* gpu, mvfxGpu* kernels)
{
    // Chains born from a sprite emitter's deaths, a birth a chain, in the
    // world's space and the instance's.
    mvfxEmitterDef emitters[2] = {Sprite(), Chains(4, 2)};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].rate = 0.0f;
    emitters[1].burstCount = 0;
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 1,
                                              .maxCount = 2,
                                              .inheritVelocity = 0.5f,
                                              .capacity = 128};
    Beside(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2},
           "chains born from another emitter's deaths");
    emitters[0].space = mvfx_spaceWorld;
    emitters[1].space = mvfx_spaceWorld;
    Beside(gpu, kernels,
           &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .pace = 0.001},
           "chains born from deaths, in the world's space");
    // Room for 4 chains, 8 births a step: the sprite's deaths give more,
    // refused whole.
    emitters[1].capacity = 16;
    emitters[1].eventSpawn.capacity = 4;
    Beside(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .secondLeast = 16},
           "chains born from deaths past their room");
    // Chains born from deaths that a kill sphere kills whole, even in
    // the step they are born.
    static const mvfxKillRule sphere = {
        .kind = mvfx_killInsideSphere, .center = {0, 0.3f, 0}, .radius = 0.3f};
    emitters[1].capacity = 1024;
    emitters[1].eventSpawn.capacity = 128;
    emitters[1].killRules = &sphere;
    emitters[1].killRuleCount = 1;
    Beside(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2},
           "chains born from deaths killed whole");
}

// A run of pinned units, the CPU path taking the GPU's places and
// velocities each step: a chain hanging from a pinned point is a
// pendulum of many links, which parts the two paths' rounding over steps
// as fast as any chaotic motion (1.3e-3 in velocity by step 9 run free);
// a step from the same state stays within 1e-3.
static void Pinned(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, bool moving,
                   const char* what)
{
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .moving = moving,
                   .pace = 0.001,
                   .adopting = true,
                   .tolerance = 1e-3f},
            what);
}

static void TestPins(Gpu* gpu, mvfxGpu* kernels)
{
    // Chains hanging from their first points, held where they were born.
    mvfxEmitterDef emitter = Chains(8, 4);
    emitter.unit.pinFirst = true;
    Pinned(gpu, kernels, &emitter, false, "chains hanging from their first points");
    // Sheets hanging from their first rows in the world, the instance
    // moving away from them.
    emitter = Sheets(8, 4, 2);
    emitter.unit.pinFirstRow = true;
    emitter.space = mvfx_spaceWorld;
    Pinned(gpu, kernels, &emitter, true, "sheets hanging from their first rows in the world");
    // Past the rebase distance (by step 46 of 80), the anchors moving with
    // the places. Within 2e-2: born under the moving instance, the sheets
    // lie up to the rebase distance from the anchor, where a place's last
    // unit (1.2e-4 at 1,024 m) over a substep of 1/120 s is 1.5e-2 m/s on
    // either path (1.2e-2 measured); an anchor left unshifted parts by the
    // whole shift.
    emitter.lifetime = mvfxRangeSource(0.1f, 0.1f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .pace = 0.25,
                   .steps = 80,
                   .adopting = true,
                   .tolerance = 2e-2f},
            "sheets hanging from their first rows past the rebase distance");
    // Chains held to the instance, its first points carried as it moves,
    // turns and scales.
    emitter = Chains(8, 4);
    emitter.unit.pinFirst = true;
    emitter.unit.attach = mvfx_attachInstance;
    emitter.space = mvfx_spaceWorld;
    Pinned(gpu, kernels, &emitter, true, "chains held to the moving instance");
    // Chains held to the host's frames, two set as the host's chains are
    // born on them, a third the emitter's own take (frame 0) and none
    // past them (held where they are, as are those born on frame 1 before
    // the host sets it); the frames moving and turning.
    static const double places[3 * 4] = {0.5, 0, 0, -0.5, 0.2, 0, 0, 0.3, 0.4, 0, 0, 0};
    static const int32_t onFrames[4] = {0, 1, 1, 5};
    const mvfxSpawnRequest given = {.count = 4, .positions = places, .attachFrames = onFrames};
    emitter = Chains(8, 4);
    emitter.unit.pinFirst = true;
    emitter.unit.attach = mvfx_attachHost;
    emitter.unit.attachFrameCapacity = 8;
    emitter.hostSpawnCapacity = 4;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .spawn = &given,
                   .attachFrames = 2,
                   .adopting = true,
                   .tolerance = 1e-3f},
            "chains held to the host's moving frames");
    // Chains born on a sprite emitter's contacts with a floor, a chain a
    // contact, held to the particle that met it: their first points follow
    // the bouncing, turning particle until it dies and lets them go. The
    // emitter's own chains are held where they are born.
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -0.4f, 0}, .normal = {0, 1, 0}};
    mvfxEmitterDef pair[2] = {Sprite(), Chains(4, 2)};
    pair[0].colliders = &floor;
    pair[0].colliderCount = 1;
    pair[0].collision = (mvfxCollision){.enabled = true, .restitution = 0.6f, .radius = 0.02f};
    pair[0].lifetime = mvfxRangeSource(0.4f, 0.8f);
    pair[0].angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-3, -3, -3, 0}, .max = {3, 3, 3, 0}};
    pair[0].events = mvfx_eventMaskCollision;
    pair[0].eventCapacity = 4096;
    pair[1].rate = 30.0f;
    pair[1].burstCount = 0;
    pair[1].unit.pinFirst = true;
    pair[1].unit.attach = mvfx_attachParticles;
    pair[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                          .source = 0,
                                          .kind = mvfx_eventCollision,
                                          .minCount = 1,
                                          .maxCount = 1,
                                          .capacity = 128};
    Compare(gpu, kernels,
            &(Run){.emitters = pair, .emitterCount = 2, .adopting = true, .tolerance = 1e-3f},
            "chains held to the particles whose contacts bore them");
}

// Whether a planned instance of an emitter is refused on the GPU for its
// units.
static bool RefusedForUnits(mvfxGpu* kernels, const mvfxEmitterDef* emitter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.planned = true;
    mvfxInstanceId planned = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success;
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = planned;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    bool refused = made && mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorState &&
                   sim == NULL && refusal.emitter == 0 && refusal.features == MVFX_FEATURE_UNITS;
    mvfxDestroyContext(context);
    return refused;
}

static void TestRefused(mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Chains(4, 2);
    emitter.unit.tearStrain = 0.5f;
    CHECK(RefusedForUnits(kernels, &emitter), "tearing units refused");
    emitter = Sheets(4, 4, 2);
    emitter.unit.normalDrag = 0.5f;
    CHECK(RefusedForUnits(kernels, &emitter), "sheets in the air refused");
    emitter = Chains(4, 2);
    emitter.unit.selfThickness = 0.02f;
    emitter.unit.selfContacts = 8;
    CHECK(RefusedForUnits(kernels, &emitter), "self-colliding units refused");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestRefused(kernels);
    TestChains(gpu, kernels);
    TestSheets(gpu, kernels);
    TestPins(gpu, kernels);
    TestColliders(gpu, kernels);
    TestKills(gpu, kernels);
    TestNeighbors(gpu, kernels);
    TestContacts(gpu, kernels);
    TestHostBirths(gpu, kernels);
    TestEventBirths(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
