// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene the determinism tests step: an effect of three endless
// emitters over four instances, with full and churning emitters among
// them, and an effect of emitter lives (a rate curve, looping bursts,
// delays) over two, and an effect of mesh emitters over one, rigid
// particles, gases and liquids; a fixed sequence of steps, and a hash of
// every instance's state.

#ifndef MAUL_VFX_TEST_VFX_SCENE_H
#define MAUL_VFX_TEST_VFX_SCENE_H

#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

// The steps of the scene, in ticks: uneven, as a host's frames are.
static const int64_t s_sceneSteps[] = {17476, 17477, 16000, 1,     40000, 17476, 3,
                                       99999, 17476, 17476, 8192,  65536, 17477, 1048576,
                                       5,     17476, 30000, 17476, 17476, 2};

#define SCENE_STEP_COUNT ((int32_t)(sizeof s_sceneSteps / sizeof s_sceneSteps[0]))

// A host module of the scene: before motion, a kick of up to 0.5 a second
// along each axis to every particle, drawn from the module's stream by
// serial, so it is the same however the work is split.
static inline void SceneKick(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    for (int32_t i = 0; i < p->count; ++i)
    {
        uint32_t words[4];
        mvfxModule_Random(call, p->serial[i], 0u, words);
        for (int32_t k = 0; k < 3; ++k)
        {
            p->velocity[k][i] += ((float)(words[k] >> 8u) * 0x1p-24f - 0.5f);
        }
    }
}

// Creates the scene's effects and instances on a context.
// Rigid particles: boxes of mixed sizes in the world thrown onto a
// tilted plane, a sphere and a turned box moving with the instance, kept
// in the host's room and meeting the depth image, losing life as they
// strike; capsules in the emitter's space bouncing inside a sphere around
// a block and a ball of distances; spheres on a plane and a bump of
// heights meeting the host's scene answers, killed by their second
// strike.
static inline void BuildRigidScene(mvfxContext* context)
{
    static const mvfxBurst throws = {.time = 0.0f,
                                     .minCount = 3,
                                     .maxCount = 3,
                                     .cycles = 0,
                                     .interval = 0.3f,
                                     .probability = 1.0f};
    static const mvfxCollider ground[3] = {
        {.kind = mvfx_colliderPlane,
         .space = mvfx_spaceLocal,
         .center = {0, -1, 0},
         .normal = {0.2f, 1, 0}},
        {.kind = mvfx_colliderSphere,
         .space = mvfx_spaceLocal,
         .center = {1, -0.6f, 0},
         .radius = 0.6f},
        {.kind = mvfx_colliderBox,
         .space = mvfx_spaceLocal,
         .center = {-0.8f, -0.7f, 0},
         .halfExtents = {0.4f, 0.3f, 0.4f},
         .rotation = {0, 0.38268343f, 0, 0.92387953f}},
    };
    // A bump of heights for the spheres and a ball of distances in the
    // capsules' bowl.
    static const float bump[9] = {0, 0.1f, 0, 0.1f, 0.4f, 0.2f, 0, 0.2f, 0};
    mvfxGridDef bumpDef = mvfxDefaultGridDef();
    bumpDef.size[0] = 3;
    bumpDef.size[2] = 3;
    bumpDef.values = bump;
    mvfxGridId bumpGrid = {0};
    CHECK(mvfxCreateGrid(context, &bumpDef, &bumpGrid) == mvfx_success, "rigid heights");
    float distances[125];
    for (int32_t k = 0; k < 125; ++k)
    {
        float x = (float)(k % 5) * 0.25f - 0.5f;
        float y = (float)(k / 5 % 5) * 0.25f - 0.5f;
        float z = (float)(k / 25) * 0.25f - 0.5f;
        distances[k] = sqrtf(x * x + y * y + z * z) - 0.3f;
    }
    mvfxGridDef ballDef = mvfxDefaultGridDef();
    ballDef.size[0] = 5;
    ballDef.size[1] = 5;
    ballDef.size[2] = 5;
    ballDef.values = distances;
    mvfxGridId ballGrid = {0};
    CHECK(mvfxCreateGrid(context, &ballDef, &ballGrid) == mvfx_success, "rigid ball");
    const mvfxCollider hills[2] = {
        {.kind = mvfx_colliderPlane,
         .space = mvfx_spaceLocal,
         .center = {0, -1, 0},
         .normal = {0.2f, 1, 0}},
        {.kind = mvfx_colliderHeightfield,
         .space = mvfx_spaceLocal,
         .center = {0, -0.9f, 0},
         .halfExtents = {1, 0, 1},
         .rotation = {0, 0.38268343f, 0, 0.92387953f},
         .grid = bumpGrid},
    };
    const mvfxCollider bowl[3] = {
        {.kind = mvfx_colliderSphere, .space = mvfx_spaceLocal, .radius = 2.0f, .inside = true},
        {.kind = mvfx_colliderBox,
         .space = mvfx_spaceLocal,
         .center = {0.3f, -1.4f, 0},
         .halfExtents = {0.5f, 0.2f, 0.3f},
         .rotation = {0, 0, 0.19509032f, 0.98078528f}},
        {.kind = mvfx_colliderDistance,
         .space = mvfx_spaceLocal,
         .center = {-0.3f, -1.7f, -0.4f},
         .halfExtents = {0.5f, 0.5f, 0.5f},
         .rotation = {0, 0, 0, 1},
         .grid = ballGrid},
    };
    static const mvfxCollider room = {.kind = mvfx_colliderBox,
                                      .space = mvfx_spaceLocal,
                                      .inside = true,
                                      .halfExtents = {1.6f, 3, 1.2f},
                                      .rotation = {0, 0, 0, 1}};
    static const mvfxKillRule secondStrike = {.kind = mvfx_killCollisions, .collisions = 2};
    // A lopsided hull: a wedge with a spike, its inertia across its axes.
    static const float lopsided[21] = {0,    0, 0,    1, 0, 0,    0, 0.6f, 0,    0,   0,
                                       0.3f, 1, 0.1f, 0, 0, 0.4f, 0, 0.7f, 0.8f, 1.1f};
    mvfxMeshDef hullDef = mvfxDefaultMeshDef();
    hullDef.positions = lopsided;
    hullDef.vertexCount = 7;
    mvfxMeshId hullMesh = {0};
    CHECK(mvfxCreateMesh(context, &hullDef, &hullMesh) == mvfx_success, "rigid hull");
    mvfxEmitterDef emitters[5] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef()};
    for (int32_t e = 0; e < 4; ++e)
    {
        mvfxEmitterDef* emitter = &emitters[e];
        emitter->rate = 0.0f;
        emitter->bursts = &throws;
        emitter->burstCount = 1;
        emitter->capacity = 24;
        emitter->lifetime = mvfxRangeSource(2.0f, 2.0f);
        emitter->shape.kind = mvfx_shapeSphere;
        emitter->shape.radius = 0.5f;
        emitter->velocity.speed = (mvfxRange){1.0f, 3.0f};
        emitter->motion.gravity[1] = -9.81f;
        emitter->orientationMode = mvfx_orientationRandom;
        emitter->angularVelocity =
            (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-3, -3, -3}, .max = {3, 3, 3}};
        emitter->size = (mvfxVectorSource){
            .kind = mvfx_sourceRange, .min = {0.2f, 0.3f, 0.2f}, .max = {0.5f, 0.6f, 0.4f}};
        emitter->rigid = (mvfxRigidDef){.enabled = true,
                                        .shape = (mvfxRigidShape)e,
                                        .density = 1.0f + (float)e,
                                        .substeps = 4,
                                        .restitution = 0.3f,
                                        .staticFriction = 0.5f,
                                        .dynamicFriction = 0.3f,
                                        .bodyContacts = 8,
                                        .hull = hullMesh};
        emitter->collision.enabled = true;
        emitter->colliders = ground;
        emitter->colliderCount = 3;
    }
    emitters[0].rigid.shape = mvfx_rigidBox;
    emitters[0].collision.lifeLoss = 0.2f;
    emitters[1].rigid.shape = mvfx_rigidCapsule;
    emitters[1].space = mvfx_spaceLocal;
    emitters[1].rigid.restitution = 0.8f;
    emitters[1].size.min[1] = 0.6f;
    emitters[1].size.max[1] = 0.9f;
    emitters[0].collision.hostColliders = true;
    emitters[0].collision.depthImage = true;
    emitters[0].collision.depthThickness = 0.3f;
    emitters[0].collision.depthSamples = 2;
    emitters[1].colliders = bowl;
    emitters[1].colliderCount = 3;
    emitters[2].rigid.shape = mvfx_rigidSphere;
    emitters[2].killRules = &secondStrike;
    emitters[2].killRuleCount = 1;
    emitters[2].colliders = hills;
    emitters[2].colliderCount = 2;
    emitters[2].collision.sceneQueries = true;
    emitters[2].collision.queryCapacity = 8;
    emitters[2].collision.lookahead = 0.2f;
    emitters[3].colliders = bowl;
    emitters[3].colliderCount = 3;
    emitters[3].size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.4f, 0.3f, 0.4f}, .max = {0.7f, 0.5f, 0.6f}};
    // A box set down at rest on the world's floor, sleeping by the
    // default's speed and time: asleep after the long step, then held.
    static const mvfxBurst setDown = {.minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1};
    static const mvfxCollider floor = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    mvfxEmitterDef* resting = &emitters[4];
    resting->rate = 0.0f;
    resting->bursts = &setDown;
    resting->burstCount = 1;
    resting->capacity = 2;
    resting->lifetime = mvfxRangeSource(30.0f, 30.0f);
    resting->shape.offset[1] = 0.25f;
    resting->velocity.speed = (mvfxRange){0.0f, 0.0f};
    resting->motion.gravity[1] = -9.81f;
    resting->size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 0.5f, 0.5f}, .max = {0.5f, 0.5f, 0.5f}};
    resting->rigid.enabled = true;
    resting->rigid.shape = mvfx_rigidBox;
    resting->rigid.density = 1.0f;
    resting->rigid.substeps = 4;
    resting->rigid.staticFriction = 0.6f;
    resting->rigid.dynamicFriction = 0.4f;
    resting->collision.enabled = true;
    resting->colliders = &floor;
    resting->colliderCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 5;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "rigid effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.origin[0] = 1745.0;
    instanceDef.seed = 0x816Du;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "rigid instance");
    CHECK(mvfxInstance_SetColliders(context, instance, &room, 1) == mvfx_success, "rigid room");
}

// Gases: a 2D gas in the world, poured into by a sphere of density,
// heat and fuel and blown across by a box of diagonal wind, decaying, in
// two substeps, closed below and on its left, buoyant, confined, its fuel
// burning and expanding; a 3D gas moving with its turning instance, a jet
// rising from a box near its closed floor, 3 V cycles of pressure,
// MacCormack's advection, buoyant in the world's gravity and confined.
// The 2D gas flows past a still sphere, stirred by turbulence; the 3D gas
// past a turned box shelf and a host's sphere moving through it, spun by a
// vortex in the instance's frame; world-space motes follow it and pour
// smoke into it.
// Their velocities are not whole cells a step.
static inline void BuildGasScene(mvfxContext* context)
{
    static const mvfxGasSource flat[2] = {
        {.shape = mvfx_gasSphere,
         .center = {-0.6f, -0.3f, 0},
         .radius = 0.25f,
         .density = 3,
         .temperature = 40,
         .fuel = 0.5f},
        {.shape = mvfx_gasBox,
         .center = {-0.4f, 0, 0},
         .halfExtents = {0.3f, 0.6f, 1},
         .setsVelocity = true,
         .velocity = {1.3f, 0.45f, 0}},
    };
    static const mvfxGasSource jet = {.shape = mvfx_gasBox,
                                      .center = {0.1f, -0.35f, 0},
                                      .halfExtents = {0.15f, 0.08f, 0.15f},
                                      .density = 5,
                                      .temperature = 10,
                                      .setsVelocity = true,
                                      .velocity = {0.2f, 1.7f, -0.3f}};
    // A still sphere in the world in the 2D gas's plume; a box in the
    // instance's frame in the 3D gas's corner.
    static const mvfxCollider rock = {.kind = mvfx_colliderSphere,
                                      .space = mvfx_spaceWorld,
                                      .center = {0.3f, 0.2f, -310.0f},
                                      .radius = 0.2f};
    static const mvfxCollider shelf = {.kind = mvfx_colliderBox,
                                       .space = mvfx_spaceLocal,
                                       .center = {0.35f, 0.75f, 0},
                                       .halfExtents = {0.2f, 0.06f, 0.3f},
                                       .rotation = {0, 0, 0.19509032f, 0.98078528f}};
    // Turbulence stirring the 2D gas, scrolling, its seed the instance's;
    // a vortex about the 3D gas's Y in the instance's frame.
    static const mvfxForce stir = {.kind = mvfx_forceTurbulence,
                                   .strength = 1.5f,
                                   .noise = {.frequency = 1.5f,
                                             .octaves = 2,
                                             .lacunarity = 2,
                                             .gain = 0.5f,
                                             .scroll = {0.2f, 0.1f, 0},
                                             .seedPerInstance = true}};
    static const mvfxForce spin = {.kind = mvfx_forceVortex,
                                   .space = mvfx_spaceLocal,
                                   .vector = {0, 1, 0},
                                   .point = {0.05f, 0, 0.1f},
                                   .strength = 0.7f,
                                   .pull = 0.2f};
    mvfxGasDef gases[2] = {mvfxDefaultGasDef(), mvfxDefaultGasDef()};
    gases[0].size[0] = 24;
    gases[0].size[1] = 16;
    gases[0].space = mvfx_gasWorld;
    gases[0].substeps = 2;
    gases[0].densityDecay = 0.4f;
    gases[0].temperatureDecay = 1.5f;
    gases[0].sources = flat;
    gases[0].closedWalls = mvfx_gasWallMinX | mvfx_gasWallMinY;
    gases[0].heatLift = 0.05f;
    gases[0].densityWeight = 0.02f;
    gases[0].vorticity = 1.5f;
    gases[0].ignitionTemperature = 0.5f;
    gases[0].burnRate = 2.0f;
    gases[0].burnHeat = 3.0f;
    gases[0].burnSmoke = 0.5f;
    gases[0].burnExpansion = 0.1f;
    gases[0].obstacles = &rock;
    gases[0].forces = &stir;
    gases[0].forceCount = 1;
    gases[0].obstacleCount = 1;
    gases[0].sourceCount = 2;
    gases[1].size[0] = 12;
    gases[1].size[1] = 10;
    gases[1].size[2] = 6;
    gases[1].center[1] = 0.5f;
    gases[1].velocityDecay = 0.2f;
    gases[1].sources = &jet;
    gases[1].closedWalls = mvfx_gasWallMinY;
    gases[1].pressureCycles = 3;
    gases[1].advection = mvfx_gasMacCormack;
    gases[1].heatLift = 0.02f;
    gases[1].vorticity = 0.8f;
    gases[1].obstacles = &shelf;
    gases[1].obstacleCount = 1;
    gases[1].hostObstacles = true;
    gases[1].forces = &spin;
    gases[1].forceCount = 1;
    gases[1].sourceCount = 1;
    // Motes in the world following the 3D gas and pouring smoke into it.
    static const mvfxForce follow = {.kind = mvfx_forceGas,
                                     .strength = 1.0f,
                                     .fieldMode = mvfx_fieldVelocity,
                                     .tightness = 3.0f,
                                     .gas = 1};
    mvfxEmitterDef motes = mvfxDefaultEmitterDef();
    motes.capacity = 64;
    motes.rate = 40.0f;
    motes.lifetime = mvfxRangeSource(0.4f, 0.6f);
    motes.shape.kind = mvfx_shapeSphere;
    motes.shape.radius = 0.2f;
    motes.shape.offset[1] = 0.4f;
    motes.forces = &follow;
    motes.forceCount = 1;
    motes.gasSplat = (mvfxGasSplat){.enabled = true, .gas = 1, .density = 1.5f, .drag = 2.0f};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &motes;
    def.emitterCount = 1;
    def.gases = gases;
    def.gasCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "gas effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.origin[2] = -310.0;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "gas instance");
}

// Liquids: a planar liquid in the world poured from a box above a box it
// keeps inside, with viscosity, confinement and the tensile correction,
// under gravity; and a 3D liquid of its turning instance's space in a
// box of that space, poured from a sphere. Their particles die and are
// born again, joining the liquid a step after their birth.
static inline void BuildLiquidScene(mvfxContext* context)
{
    static const mvfxCollider tank = {.kind = mvfx_colliderBox,
                                      .inside = true,
                                      .center = {0.0f, 0.5f, 0.0f},
                                      .halfExtents = {0.5f, 0.5f, 1.0f},
                                      .rotation = {0, 0, 0, 1}};
    static const mvfxCollider cup = {.kind = mvfx_colliderBox,
                                     .space = mvfx_spaceLocal,
                                     .inside = true,
                                     .center = {0.0f, 0.3f, 0.0f},
                                     .halfExtents = {0.3f, 0.3f, 0.3f},
                                     .rotation = {0, 0, 0, 1}};
    mvfxEmitterDef liquids[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    for (int32_t e = 0; e < 2; ++e)
    {
        mvfxEmitterDef* liquid = &liquids[e];
        liquid->capacity = e == 0 ? 96 : 64;
        liquid->rate = 60.0f;
        liquid->lifetime = mvfxRangeSource(1.2f, 1.6f);
        liquid->velocity.speed = (mvfxRange){0.2f, 0.6f};
        liquid->motion.gravity[1] = -9.81f;
        liquid->collision.enabled = true;
        liquid->liquid.enabled = true;
        liquid->liquid.spacing = 0.08f;
    }
    liquids[0].shape.kind = mvfx_shapeBox;
    liquids[0].shape.offset[1] = 0.8f;
    liquids[0].shape.halfExtents[0] = 0.2f;
    liquids[0].shape.halfExtents[1] = 0.1f;
    liquids[0].colliders = &tank;
    liquids[0].colliderCount = 1;
    liquids[0].liquid.planar = true;
    liquids[0].liquid.viscosity = 0.05f;
    liquids[0].liquid.vorticity = 2.0f;
    liquids[0].liquid.tensile = 0.1f;
    liquids[1].space = mvfx_spaceLocal;
    liquids[1].shape.kind = mvfx_shapeSphere;
    liquids[1].shape.radius = 0.1f;
    liquids[1].shape.offset[1] = 0.45f;
    liquids[1].colliders = &cup;
    liquids[1].colliderCount = 1;
    liquids[1].liquid.substeps = 2;
    liquids[1].liquid.iterations = 2;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = liquids;
    def.emitterCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "liquid effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.origin[2] = -360.0;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "liquid instance");
}

static inline void BuildScene(mvfxContext* context)
{
    // A 3x2x2 field of binary16 vectors, made of exact small values.
    static float fieldValues[36];
    for (int32_t i = 0; i < 12; ++i)
    {
        fieldValues[3 * i] = (float)(i % 3) - 1.0f;
        fieldValues[3 * i + 1] = 0.5f * (float)((i / 3) % 2);
        fieldValues[3 * i + 2] = 0.25f * (float)(i / 6) - 0.125f;
    }
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 3;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.components = 3;
    gridDef.format = mvfx_gridFloat16;
    gridDef.values = fieldValues;
    mvfxGridId fieldGrid = {0};
    CHECK(mvfxCreateGrid(context, &gridDef, &fieldGrid) == mvfx_success, "scene grid");
    mvfxEmitterDef emitters[5] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef()};
    // The fifth is a beam aimed by parameter 0, bent through a control,
    // its noise moving, struck again every 0.3 seconds.
    emitters[4].rate = 0.0f;
    emitters[4].capacity = 7;
    emitters[4].lifetime = mvfxRangeSource(0.3f, 0.3f);
    emitters[4].color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 0.7f, 1.0f, 1.0f}, .max = {0.7f, 0.9f, 1.0f, 1.0f}};
    emitters[4].ribbon.ids = mvfx_ribbonOne;
    emitters[4].ribbon.texture = mvfx_ribbonTileByDistance;
    emitters[4].beam = (mvfxBeamDef){.enabled = true,
                                     .segments = 6,
                                     .start = {.point = {0.0f, 0.0f, 0.5f}},
                                     .end = {.fromParameter = true, .parameter = 0},
                                     .controlCount = 1,
                                     .controls = {{.point = {1.0f, 2.0f, 0.0f}}},
                                     .noiseAmplitude = 0.4f,
                                     .noiseFrequency = 2.0f,
                                     .noiseSpeed = 1.5f};
    emitters[0].capacity = 100;
    emitters[0].light = (mvfxLightDef){.enabled = true,
                                       .every = 2,
                                       .intensity = 3.0f,
                                       .alphaScales = true,
                                       .radius = 0.5f,
                                       .radiusBySize = 2.0f};
    // A colour drawn per particle, so the output's sRGB matters.
    emitters[0].color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.3f, 0.5f, 0.4f}, .max = {0.9f, 0.7f, 0.6f, 1.0f}};
    emitters[0].rate = 37.5f;
    emitters[0].lifetime = mvfxRangeSource(0.25f, 1.5f);
    emitters[1].capacity = 64;
    // Each emitter's quads turn their own way.
    emitters[0].sprite = (mvfxSpriteDef){.billboard = mvfx_billboardDirectional, .stretch = 0.3f};
    emitters[1].sprite.billboard = mvfx_billboardAxisY;
    emitters[2].sprite.billboard = mvfx_billboardRotated;
    emitters[3].sprite.billboard = mvfx_billboardFixed;
    emitters[2].angle = mvfxRangeSource(0.0f, 6.2831853f);
    emitters[2].spin = mvfxRangeSource(-2.0f, 2.0f);
    emitters[3].orientationMode = mvfx_orientationRandom;
    // Three emitters drawn in three orders.
    emitters[0].sortMode = mvfx_sortBackToFront;
    emitters[1].sortMode = mvfx_sortYoungestFirst;
    emitters[2].sortMode = mvfx_sortFrontToBack;
    emitters[2].sortMeasure = mvfx_sortByDepth;
    // The second draws one ribbon, narrowing and fading toward its young
    // end.
    static const mvfxCurveKey narrowing[2] = {
        {.time = 0.0f, .value = 2.0f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 0.5f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxColorKey ribbonWarm[2] = {{0.0f, 1.0f, 0.6f, 0.2f}, {1.0f, 0.3f, 0.3f, 0.9f}};
    static const mvfxAlphaKey ribbonFade[2] = {{0.0f, 1.0f}, {1.0f, 0.2f}};
    emitters[1].ribbon =
        (mvfxRibbonDef){.ids = mvfx_ribbonOne,
                        .pieces = 4,
                        .bendAngle = 0.3f,
                        .texture = mvfx_ribbonTileByDistance,
                        .tileLength = 0.5f,
                        .scroll = 0.75f,
                        .facing = mvfx_ribbonFaceCamera,
                        .normal = {0.0f, 0.0f, 1.0f},
                        .breakDistance = 1.5f,
                        .pinHead = true,
                        .width = {.value = 1.0f, .curve = {.keys = narrowing, .keyCount = 2}},
                        .color = {.colorKeys = ribbonWarm,
                                  .colorKeyCount = 2,
                                  .alphaKeys = ribbonFade,
                                  .alphaKeyCount = 2}};
    emitters[1].meshIndex = (mvfxFrameStart){.mode = mvfx_frameRandom, .first = 1, .last = 6};
    emitters[1].rate = 2000.0f;
    emitters[1].lifetime = mvfxRangeSource(0.01f, 0.05f);
    emitters[0].shape.kind = mvfx_shapeSphere;
    emitters[0].shape.radius = 1.5f;
    emitters[0].shape.thickness = 0.3f;
    emitters[0].velocity.speed = (mvfxRange){1.0f, 4.0f};
    emitters[1].shape.kind = mvfx_shapeCone;
    emitters[1].shape.radius = 0.5f;
    emitters[1].shape.length = 2.0f;
    emitters[1].shape.angle = 0.4f;
    emitters[1].shape.coneVolume = true;
    emitters[1].shape.rotation[0] = 0.38268343f;
    emitters[1].shape.rotation[3] = 0.92387953f;
    emitters[1].velocity.mode = mvfx_directionCone;
    emitters[1].velocity.coneAngle = 0.25f;
    emitters[1].velocity.randomize = 0.2f;
    emitters[1].velocity.speed = (mvfxRange){5.0f, 6.0f};
    emitters[2].shape.kind = mvfx_shapeBox;
    emitters[2].shape.halfExtents[0] = 1.0f;
    emitters[2].shape.halfExtents[1] = 0.5f;
    emitters[2].shape.halfExtents[2] = 2.0f;
    emitters[2].shape.thickness = 0.0f;
    emitters[2].shape.offset[1] = 3.0f;
    emitters[2].velocity.mode = mvfx_directionFromCenter;
    emitters[2].velocity.speed = (mvfxRange){0.5f, 0.5f};
    // Motion: gravity and drag, gravity in the emitter's space with a drag
    // per particle, and drag alone.
    emitters[0].motion.gravity[1] = -9.75f;
    emitters[0].motion.drag = 0.4f;
    emitters[1].motion.gravity[2] = 3.0f;
    emitters[1].motion.gravitySpace = mvfx_spaceLocal;
    emitters[1].motion.drag = 1.5f;
    emitters[1].motion.dragScale = mvfxRangeSource(0.25f, 2.0f);
    emitters[2].motion.drag = 30.0f;
    // Forces: an attractor in a sphere, with a kill radius, in the
    // emitter's space on the first; a vortex in a tube
    // and a vector field in a turned, tiled box, in the emitter's space, on
    // the third.
    static const mvfxForce attract = {
        .kind = mvfx_forceAttractor,
        .space = mvfx_spaceLocal,
        .point = {0, 2, 0},
        .strength = 6.0f,
        .softening = 0.5f,
        .killRadius = 1.0f,
        .falloff = {
            .kind = mvfx_falloffSphere, .center = {0, 2, 0}, .radius = 5.0f, .exponent = 1.5f}};
    const mvfxForce swirlAndField[2] = {
        {.kind = mvfx_forceVortex,
         .space = mvfx_spaceLocal,
         .vector = {0, 1, 0},
         .strength = 4.0f,
         .pull = 1.0f,
         .falloff =
             {.kind = mvfx_falloffTube, .axis = {0, 1, 0}, .radius = 3.0f, .exponent = 1.0f}},
        {.kind = mvfx_forceVectorField,
         .space = mvfx_spaceLocal,
         .point = {0, 3, 0},
         .strength = 3.0f,
         .grid = fieldGrid,
         .boxSize = {4, 2, 6},
         .boxRotation = {0.6f, 0, 0, 0.8f},
         .border = mvfx_borderRepeat},
    };
    emitters[0].forces = &attract;
    emitters[0].forceCount = 1;
    emitters[2].forces = swirlAndField;
    emitters[2].forceCount = 2;
    static const mvfxForce turbulence = {.kind = mvfx_forceTurbulence,
                                         .strength = 2.5f,
                                         .noise = {.frequency = 0.4f,
                                                   .octaves = 3,
                                                   .lacunarity = 2.0f,
                                                   .gain = 0.5f,
                                                   .scroll = {0.5f, 0.0f, 0.25f},
                                                   .seed = 12345u}};
    emitters[1].forces = &turbulence;
    emitters[1].forceCount = 1;
    emitters[2].capacity = 7;
    // Velocity modules: an orbit slowing over the life and a speed limit
    // on the first; half the motion and a radial push on the second.
    static const mvfxCurveKey slowing[2] = {
        {.time = 0.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 0.25f, .interpolation = mvfx_interpolationLinear}};
    emitters[0].velocityModules.orbitAxis[1] = 1.0f;
    emitters[0].velocityModules.orbitRate =
        (mvfxLifeCurve){.value = 2.0f, .curve = {.keys = slowing, .keyCount = 2}};
    emitters[0].velocityModules.limitSpeed = true;
    emitters[0].velocityModules.speedLimit.value = 3.0f;
    emitters[0].velocityModules.limitDampen = 5.0f;
    emitters[1].velocityModules.motionScale.value = 0.5f;
    emitters[1].velocityModules.radialSpeed.value = 1.25f;
    // Kill rules, in the emitter's space: past a sphere or at a third
    // collision on the first, past a plane on the second.
    static const mvfxKillRule outside[2] = {
        {.kind = mvfx_killOutsideSphere, .space = mvfx_spaceLocal, .radius = 2.5f},
        {.kind = mvfx_killCollisions, .collisions = 3},
    };
    static const mvfxKillRule plane = {.kind = mvfx_killPastPlane,
                                       .space = mvfx_spaceLocal,
                                       .center = {0, 0, 1.75f},
                                       .normal = {0.2f, 0, 1}};
    emitters[0].killRules = outside;
    emitters[0].killRuleCount = 2;
    emitters[1].killRules = &plane;
    emitters[1].killRuleCount = 1;
    static const mvfxHostModule kick = {.stage = mvfx_stageBeforeMotion, .function = SceneKick};
    emitters[1].hostModules = &kick;
    emitters[1].hostModuleCount = 1;
    // The first falls onto a floor and a bumpy heightfield in its own
    // space, bouncing, sliding and losing life, and meets the walls its
    // instances set.
    static const float bumps[16] = {-1.0f, -0.7f, -0.9f,  -0.5f, -0.6f, -0.4f, -0.8f, -0.9f,
                                    -0.9f, -0.5f, -0.45f, -0.7f, -0.4f, -0.8f, -0.6f, -1.0f};
    mvfxGridDef bumpDef = mvfxDefaultGridDef();
    bumpDef.size[0] = 4;
    bumpDef.size[1] = 1;
    bumpDef.size[2] = 4;
    bumpDef.values = bumps;
    mvfxGridId bumpGrid = {0};
    CHECK(mvfxCreateGrid(context, &bumpDef, &bumpGrid) == mvfx_success, "scene heights");
    const mvfxCollider ground[2] = {
        {.kind = mvfx_colliderPlane,
         .space = mvfx_spaceLocal,
         .center = {0, -1.2f, 0},
         .normal = {0, 1, 0}},
        {.kind = mvfx_colliderHeightfield,
         .space = mvfx_spaceLocal,
         .halfExtents = {1.5f, 0, 1.5f},
         .rotation = {0, 0.38268343f, 0, 0.92387953f},
         .grid = bumpGrid},
    };
    // The second keeps out of a ball of distances, five points a side.
    float ballDistances[125];
    for (int32_t k = 0; k < 125; ++k)
    {
        float x = (float)(k % 5) * 0.5f - 1.0f;
        float y = (float)(k / 5 % 5) * 0.5f - 1.0f;
        float z = (float)(k / 25) * 0.5f - 1.0f;
        ballDistances[k] = sqrtf(x * x + y * y + z * z) - 0.5f;
    }
    mvfxGridDef ballDef = mvfxDefaultGridDef();
    ballDef.size[0] = 5;
    ballDef.size[1] = 5;
    ballDef.size[2] = 5;
    ballDef.format = mvfx_gridFloat16;
    ballDef.values = ballDistances;
    mvfxGridId ballGrid = {0};
    CHECK(mvfxCreateGrid(context, &ballDef, &ballGrid) == mvfx_success, "scene ball");
    const mvfxCollider ball = {.kind = mvfx_colliderDistance,
                               .space = mvfx_spaceLocal,
                               .center = {0, 0, 0.6f},
                               .halfExtents = {1, 1, 1},
                               .rotation = {0, 0, 0, 1},
                               .grid = ballGrid};
    emitters[1].collision = (mvfxCollision){.enabled = true, .restitution = 0.3f};
    emitters[1].colliders = &ball;
    emitters[1].colliderCount = 1;
    emitters[0].collision = (mvfxCollision){.enabled = true,
                                            .hostColliders = true,
                                            .restitution = 0.6f,
                                            .friction = 0.2f,
                                            .lifeLoss = 0.1f,
                                            .restSpeed = 1.0f,
                                            .radius = 0.05f,
                                            .sceneQueries = true,
                                            .queryCapacity = 24,
                                            .lookahead = 0.1f};
    emitters[0].colliders = ground;
    emitters[0].colliderCount = 2;
    // It tells of every birth, death and collision, up to 12 a step.
    emitters[0].events = mvfx_eventMaskBirth | mvfx_eventMaskDeath | mvfx_eventMaskCollision;
    // Its own neighbors within 2.5, at most 3; the second's own within 0.5,
    // at most 4; emitter 2 finds the second's within 3 (so the second's
    // grid's cells are 3), at most 4.
    emitters[0].neighbors =
        (mvfxNeighborDef){.enabled = true, .source = -1, .radius = 2.5f, .maxNeighbors = 3};
    emitters[2].neighbors =
        (mvfxNeighborDef){.enabled = true, .source = 1, .radius = 3.0f, .maxNeighbors = 4};
    emitters[1].neighbors =
        (mvfxNeighborDef){.enabled = true, .source = -1, .radius = 0.5f, .maxNeighbors = 4};
    // The second's particles push each other within 0.4, harder the
    // nearer.
    static const mvfxCurveKey nearer[2] = {
        {.time = 0.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 0.0f, .interpolation = mvfx_interpolationLinear},
    };
    emitters[1].pairForce =
        (mvfxPairForceDef){.enabled = true,
                           .radius = 0.4f,
                           .strength = {.value = -1.5f, .curve = {.keys = nearer, .keyCount = 2}}};
    // The second links its particles within 0.6, at most 3 each and 64 in
    // all, fading.
    emitters[1].plexus = (mvfxPlexusDef){.enabled = true,
                                         .distance = 0.6f,
                                         .maxLinks = 64,
                                         .maxPerParticle = 3,
                                         .width = 0.2f,
                                         .fade = true};
    // The second keeps its particles apart, a tenth of their size each.
    emitters[1].contacts = (mvfxContactDef){.enabled = true,
                                            .radiusScale = 0.1f,
                                            .iterations = 3,
                                            .maxContacts = 2,
                                            .restitution = 0.3f,
                                            .friction = 0.2f};
    // The second flocks, seeks parameter 0 in the world, flees a point of
    // its own and follows a closed square round the instance.
    static const float squarePath[12] = {-2, 0, -2, 2, 0, -2, 2, 0, 2, -2, 0, 2};
    emitters[1].steering = (mvfxSteeringDef){
        .enabled = true,
        .separation = 0.2f,
        .separationRadius = 0.3f,
        .alignment = 0.5f,
        .alignmentRadius = 0.4f,
        .cohesion = 0.8f,
        .cohesionRadius = 0.5f,
        .viewAngle = 2.5f,
        .cruiseSpeed = 2.0f,
        .maxForce = 6.0f,
        .seek = {.weight = 0.6f,
                 .space = mvfx_spaceWorld,
                 .fromParameter = true,
                 .radius = 2000.0f},
        .flee = {.weight = 0.3f, .space = mvfx_spaceLocal, .point = {0, 1, 0}, .radius = 1.5f},
        .path = {.weight = 0.4f,
                 .space = mvfx_spaceLocal,
                 .points = squarePath,
                 .pointCount = 4,
                 .closed = true,
                 .radius = 1.5f,
                 .lookahead = 0.5f},
    };
    emitters[0].eventCapacity = 12;
    // The third collides with the depth image each instance carries.
    emitters[2].collision = (mvfxCollision){.enabled = true,
                                            .restitution = 0.5f,
                                            .depthImage = true,
                                            .depthThickness = 1.0f,
                                            .depthSamples = 8};
    // Seven a second, so it spawns within the scene's 1.4 seconds and
    // fills.
    emitters[2].rate = 7.0f;
    emitters[2].lifetime = mvfxRangeSource(5.0f, 9.0f);
    // The fourth throws sparks from the first's collisions, and the third
    // spawns from the sparks' deaths too, so it steps after them.
    emitters[3].rate = 0.0f;
    emitters[3].capacity = 40;
    // The sparks draw after the rest, inside a fixed box.
    emitters[3].drawPriority = 1;
    emitters[3].fixedBounds = true;
    emitters[3].boundsMin[0] = -4.0f;
    emitters[3].boundsMin[1] = -1.0f;
    emitters[3].boundsMin[2] = -3.0f;
    emitters[3].boundsMax[0] = 5.0f;
    emitters[3].boundsMax[1] = 6.0f;
    emitters[3].boundsMax[2] = 2.0f;
    emitters[3].lifetime = mvfxRangeSource(0.05f, 0.3f);
    emitters[3].velocity.speed = (mvfxRange){0.5f, 1.5f};
    emitters[3].events = mvfx_eventMaskDeath;
    // Each collision's sparks draw a ribbon of their own.
    emitters[3].ribbon.ids = mvfx_ribbonPerSource;
    emitters[3].ribbon.pieces = 3;
    emitters[3].ribbon.texture = mvfx_ribbonTilePerSegment;
    emitters[3].ribbon.facing = mvfx_ribbonFaceNormal;
    emitters[3].ribbon.normal[1] = 1.0f;
    emitters[3].eventCapacity = 16;
    emitters[3].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventCollision,
                                              .minCount = 1,
                                              .maxCount = 3,
                                              .inheritVelocity = 0.4f,
                                              .inheritColor = true,
                                              .capacity = 48};
    emitters[2].eventSpawn = (mvfxEventSpawn){
        .enabled = true, .source = 3, .kind = mvfx_eventDeath, .maxCount = 1, .capacity = 8};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 5;
    static const float beamEnd[4] = {3.0f, 1.0f, 2.0f, 0.0f};
    def.parameters = beamEnd;
    def.parameterCount = 1;
    // A lower level: 60 percent of the capacity, 70 percent of the rate,
    // no sparks.
    static const mvfxQualityLevel lower = {
        .capacityScale = 0.6f, .rateScale = 0.7f, .omitted = 1u << 3u};
    def.qualityLevels = &lower;
    def.qualityLevelCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "scene effect");
    // An effect of emitter lives: a rate curve over loops, delayed and
    // looping bursts of random counts and chances, and a late constant rate.
    static const mvfxCurveKey rise[3] = {
        {0.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationCubic},
        {0.4f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
        {1.0f, 0.2f, 0.0f, 0.0f, mvfx_interpolationLinear},
    };
    static const mvfxBurst bursts[2] = {
        {.time = 0.0f,
         .minCount = 5,
         .maxCount = 20,
         .cycles = 3,
         .interval = 0.1f,
         .probability = 0.8f},
        {.time = 0.25f, .minCount = 30, .maxCount = 30, .cycles = 1, .probability = 1.0f},
    };
    mvfxEmitterDef lives[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                               mvfxDefaultEmitterDef()};
    lives[0].capacity = 50;
    lives[0].rate = 400.0f;
    lives[0].rateCurve = (mvfxCurve){rise, 3, mvfx_tangentsMonotone};
    lives[0].duration = 0.75f;
    lives[0].lifetime = mvfxRangeSource(0.05f, 0.3f);
    lives[1].capacity = 40;
    lives[1].rate = 0.0f;
    lives[1].bursts = bursts;
    lives[1].burstCount = 2;
    lives[1].startDelay = 0.2f;
    lives[1].duration = 0.5f;
    lives[1].loops = 3;
    lives[1].lifetime = mvfxRangeSource(0.1f, 0.6f);
    lives[0].shape.kind = mvfx_shapeCircle;
    lives[0].shape.radius = 2.0f;
    lives[0].shape.thickness = 0.5f;
    lives[0].shape.arc = 4.0f;
    lives[0].shape.arcMode = mvfx_arcPingPong;
    lives[0].shape.arcSpeed = 1.5f;
    lives[0].velocity.speed = (mvfxRange){2.0f, 2.0f};
    lives[1].shape.kind = mvfx_shapeHemisphere;
    lives[1].shape.radius = 1.0f;
    lives[1].shape.thickness = 0.0f;
    lives[1].velocity.mode = mvfx_directionFixed;
    lives[1].velocity.direction[1] = 1.0f;
    lives[1].velocity.speed = (mvfxRange){0.0f, 3.0f};
    lives[2].shape.kind = mvfx_shapeLine;
    lives[2].shape.length = 4.0f;
    lives[2].shape.arcMode = mvfx_arcLoop;
    lives[2].shape.arcSpeed = 0.5f;
    lives[2].velocity.speed = (mvfxRange){1.0f, 1.0f};
    lives[2].capacity = 30;
    lives[2].rate = 50.0f;
    lives[2].startDelay = 1.0f;
    lives[2].lifetime = mvfxRangeSource(0.2f, 0.9f);
    mvfxEffectDef livesDef = mvfxDefaultEffectDef();
    livesDef.emitters = lives;
    livesDef.emitterCount = 3;
    mvfxEffectId livesEffect = {0};
    CHECK(mvfxCreateEffect(context, &livesDef, &livesEffect) == mvfx_success, "lives effect");
    for (int32_t i = 0; i < 2; ++i)
    {
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = livesEffect;
        instanceDef.seed = 0x5EEDu + (uint32_t)i;
        mvfxInstanceId instance = {0};
        CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success,
              "lives instance");
    }
    // An effect spawning from a mesh: an octahedron's surface, edges and
    // volume (eight tetrahedra about its center).
    static const float octahedron[] = {1, 0, 0, -1, 0, 0, 0,  1, 0, 0, -1,
                                       0, 0, 0, 1,  0, 0, -1, 0, 0, 0};
    static const uint32_t faces[] = {0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4,
                                     2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5};
    uint32_t cells[32];
    for (int32_t f = 0; f < 8; ++f)
    {
        cells[4 * f] = faces[3 * f];
        cells[4 * f + 1] = faces[3 * f + 1];
        cells[4 * f + 2] = faces[3 * f + 2];
        cells[4 * f + 3] = 6;
    }
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = octahedron;
    meshDef.vertexCount = 7;
    meshDef.triangles = faces;
    meshDef.triangleCount = 8;
    meshDef.tetrahedra = cells;
    meshDef.tetrahedronCount = 8;
    mvfxMeshId mesh = {0};
    CHECK(mvfxCreateMesh(context, &meshDef, &mesh) == mvfx_success, "scene mesh");
    static const mvfxMeshMode modes[3] = {mvfx_meshSurface, mvfx_meshEdges, mvfx_meshVolume};
    mvfxEmitterDef meshEmitters[3];
    for (int32_t i = 0; i < 3; ++i)
    {
        meshEmitters[i] = mvfxDefaultEmitterDef();
        meshEmitters[i].capacity = 24;
        meshEmitters[i].rate = 30.0f + 10.0f * (float)i;
        meshEmitters[i].lifetime = mvfxRangeSource(0.2f, 0.7f);
        meshEmitters[i].shape.kind = mvfx_shapeMesh;
        meshEmitters[i].shape.mesh = mesh;
        meshEmitters[i].shape.meshMode = modes[i];
        meshEmitters[i].velocity.speed = (mvfxRange){0.5f, 1.5f};
    }
    // Initial values over them: a size and a gradient's color and a spin on
    // the surface; Euler angles, an angular velocity and frames on the edges;
    // custom attributes and lives from parameters in the volume.
    static const mvfxColorKey warm[2] = {{0.0f, 1.0f, 0.8f, 0.2f}, {1.0f, 0.6f, 0.1f, 0.0f}};
    static const mvfxAlphaKey fade[2] = {{0.0f, 1.0f}, {1.0f, 0.3f}};
    static const float parameters[8] = {0.6f, 0.0f, 0.0f, 0.0f, 2.0f, -1.0f, 0.5f, 3.0f};
    static const mvfxCustomAttribute custom[2] = {
        {.components = 3,
         .initial = {.kind = mvfx_sourceRange, .min = {-1, 0, 2}, .max = {1, 5, 2}}},
        {.components = 4,
         .initial = {.kind = mvfx_sourceParameter,
                     .min = {1, 1, 1, 1},
                     .max = {2, 2, 2, 2},
                     .uniform = true,
                     .parameter = 1}},
    };
    meshEmitters[0].size = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                              .min = {0.1f, 0.1f, 0.1f},
                                              .max = {0.4f, 0.4f, 0.4f},
                                              .uniform = true};
    meshEmitters[0].color = (mvfxVectorSource){.kind = mvfx_sourceGradient,
                                               .min = {1, 1, 1, 1},
                                               .max = {1, 1, 1, 1},
                                               .gradient = {warm, 2, fade, 2}};
    meshEmitters[0].spin = mvfxRangeSource(-3.0f, 3.0f);
    meshEmitters[1].orientationMode = mvfx_orientationEuler;
    meshEmitters[1].orientation =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, 0}, .max = {3.1f, 1.0f, 6.2f}};
    meshEmitters[1].angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-1, -1, -1}, .max = {1, 1, 1}};
    meshEmitters[1].frame = (mvfxFrameStart){mvfx_frameRandom, 0, 15};
    meshEmitters[2].custom = custom;
    meshEmitters[2].customCount = 2;
    meshEmitters[2].lifetime =
        (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {0.5f, 1.0f}, .parameter = 0};
    meshEmitters[0].motion.angularDrag = 0.75f;
    // Its spin slowing over the life, by a curve.
    static const mvfxCurveKey easing[2] = {
        {.time = 0.0f, .value = 1.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 0.25f, .interpolation = mvfx_interpolationLinear}};
    meshEmitters[0].overLife.rotation = (mvfxRotationOverLife){
        .enabled = true, .scale = {.value = 1.0f, .curve = {.keys = easing, .keyCount = 2}}};
    static const mvfxForce meshSwirl = {
        .kind = mvfx_forceTurbulence,
        .space = mvfx_spaceLocal,
        .strength = 1.5f,
        .fieldMode = mvfx_fieldVelocity,
        .tightness = 3.0f,
        .noise = {.frequency = 1.25f,
                  .octaves = 2,
                  .lacunarity = 3.0f,
                  .gain = 0.75f,
                  .seed = 9u,
                  .seedPerInstance = true},
        .falloff = {.kind = mvfx_falloffSphere, .radius = 6.0f, .exponent = 1.0f}};
    meshEmitters[0].forces = &meshSwirl;
    meshEmitters[0].forceCount = 1;
    meshEmitters[1].motion.gravity[0] = 1.0f;
    meshEmitters[0].ratePerDistance = 1.5f;
    meshEmitters[0].inheritVelocity = 0.25f;
    meshEmitters[1].space = mvfx_spaceLocal;
    // A world wind on the local emitter, its strength from parameter 1,
    // and a constant in a cone.
    static const mvfxForce meshForces[2] = {
        {.kind = mvfx_forceWind,
         .vector = {2, 0, -1},
         .strength = 0.5f,
         .strengthFromParameter = true,
         .parameter = 1},
        {.kind = mvfx_forceConstant,
         .vector = {0, 0, 1},
         .strength = 2.0f,
         .falloff = {.kind = mvfx_falloffCone,
                     .axis = {0, 0, 1},
                     .radius = 8.0f,
                     .angle = 0.8f,
                     .exponent = 2.0f}},
    };
    meshEmitters[1].forces = meshForces;
    meshEmitters[1].forceCount = 2;
    static const mvfxKillRule slow = {.kind = mvfx_killSlower, .speed = 0.4f, .minimumAge = 0.2f};
    meshEmitters[1].killRules = &slow;
    meshEmitters[1].killRuleCount = 1;
    mvfxEffectDef meshEffectDef = mvfxDefaultEffectDef();
    meshEffectDef.emitters = meshEmitters;
    meshEffectDef.emitterCount = 3;
    meshEffectDef.parameters = parameters;
    meshEffectDef.parameterCount = 2;
    mvfxEffectId meshEffect = {0};
    CHECK(mvfxCreateEffect(context, &meshEffectDef, &meshEffect) == mvfx_success, "mesh effect");
    mvfxInstanceDef meshInstanceDef = mvfxDefaultInstanceDef();
    meshInstanceDef.effect = meshEffect;
    meshInstanceDef.seed = 0x3E5Bu;
    mvfxInstanceId meshInstance = {0};
    CHECK(mvfxCreateInstance(context, &meshInstanceDef, &meshInstance) == mvfx_success,
          "mesh instance");
    // Chains and sheets: pinned ropes in the world, two every 0.4 seconds,
    // loose local tails falling with drag toward an attractor, which pulls
    // their points apart, and banners.
    mvfxEmitterDef chainEmitters[6] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                       mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                       mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    static const mvfxBurst ropes = {.time = 0.0f,
                                    .minCount = 2,
                                    .maxCount = 2,
                                    .cycles = 0,
                                    .interval = 0.4f,
                                    .probability = 1.0f};
    chainEmitters[0].rate = 0.0f;
    chainEmitters[0].bursts = &ropes;
    chainEmitters[0].burstCount = 1;
    chainEmitters[0].capacity = 6 * 8;
    chainEmitters[0].lifetime = mvfxRangeSource(1.2f, 1.2f);
    chainEmitters[0].shape.kind = mvfx_shapeSphere;
    chainEmitters[0].shape.radius = 1.0f;
    chainEmitters[0].motion.gravity[1] = -9.81f;
    chainEmitters[0].ribbon.ids = mvfx_ribbonOne;
    chainEmitters[0].unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                          .points = 6,
                                          .spacing = 0.3f,
                                          .direction = {1, -1, 0},
                                          .substeps = 4,
                                          .stretchCompliance = 1e-4f,
                                          .bendCompliance = 1e-2f,
                                          .damping = 1.5f,
                                          .pinFirst = true,
                                          .selfThickness = 0.5f,
                                          .selfFriction = 0.3f,
                                          .selfContacts = 6,
                                          .attach = mvfx_attachInstance};
    chainEmitters[1].space = mvfx_spaceLocal;
    chainEmitters[1].rate = 3.0f;
    chainEmitters[1].capacity = 4 * 6;
    chainEmitters[1].lifetime = mvfxRangeSource(0.8f, 1.6f);
    chainEmitters[1].velocity.speed = (mvfxRange){1.0f, 3.0f};
    chainEmitters[1].motion.gravity[1] = -4.0f;
    chainEmitters[1].motion.drag = 0.6f;
    static const mvfxForce push = {.kind = mvfx_forceAttractor,
                                   .space = mvfx_spaceLocal,
                                   .point = {0, -1, 0.5f},
                                   .strength = 3.0f,
                                   .softening = 0.25f};
    chainEmitters[1].forces = &push;
    chainEmitters[1].forceCount = 1;
    chainEmitters[1].unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                          .points = 4,
                                          .spacing = 0.2f,
                                          .direction = {0, 0, 1},
                                          .substeps = 3,
                                          .stretchCompliance = 1e-3f,
                                          .bendCompliance = 0.1f,
                                          .damping = 0.5f,
                                          .tearStrain = 0.02f};
    chainEmitters[1].events = mvfx_eventMaskTear;
    chainEmitters[1].eventCapacity = 16;
    // Sparks where the tails tear.
    chainEmitters[5].space = mvfx_spaceLocal;
    chainEmitters[5].rate = 0.0f;
    chainEmitters[5].capacity = 16;
    chainEmitters[5].lifetime = mvfxRangeSource(0.3f, 0.5f);
    chainEmitters[5].velocity.speed = (mvfxRange){0.5f, 1.5f};
    chainEmitters[5].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                                   .source = 1,
                                                   .kind = mvfx_eventTear,
                                                   .minCount = 1,
                                                   .maxCount = 2,
                                                   .inheritVelocity = 0.5f,
                                                   .capacity = 8};
    // Banners: sheets pinned by their first row, sheared by a gusting wind
    // that also moves the air their triangles meet.
    static const mvfxBurst banners = {.time = 0.1f,
                                      .minCount = 1,
                                      .maxCount = 1,
                                      .cycles = 0,
                                      .interval = 0.6f,
                                      .probability = 1.0f};
    chainEmitters[2].rate = 0.0f;
    chainEmitters[2].bursts = &banners;
    chainEmitters[2].burstCount = 1;
    chainEmitters[2].hostSpawnCapacity = 1;
    chainEmitters[2].capacity = 3 * 5 * 4;
    chainEmitters[2].lifetime = mvfxRangeSource(1.5f, 1.5f);
    chainEmitters[2].motion.gravity[1] = -9.81f;
    chainEmitters[2].motion.drag = 0.8f;
    static const mvfxForce gust = {
        .kind = mvfx_forceTurbulence,
        .strength = 4.0f,
        .fieldMode = mvfx_fieldVelocity,
        .tightness = 2.0f,
        .noise = {.frequency = 0.7f, .octaves = 1, .lacunarity = 2.0f, .gain = 0.5f, .seed = 21u}};
    chainEmitters[2].forces = &gust;
    chainEmitters[2].forceCount = 1;
    chainEmitters[2].unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                                          .points = 5,
                                          .rows = 4,
                                          .spacing = 0.25f,
                                          .direction = {1, 0, 0},
                                          .downDirection = {0, -0.5f, 1},
                                          .substeps = 4,
                                          .stretchCompliance = 1e-4f,
                                          .shearCompliance = 1e-3f,
                                          .bendCompliance = 0.05f,
                                          .damping = 1.0f,
                                          .normalDrag = 0.8f,
                                          .tangentDrag = 0.05f,
                                          .pinFirstRow = true,
                                          .attach = mvfx_attachHost,
                                          .attachFrameCapacity = 2};
    // Kites: spinning particles, each trailing a streamer pinned to it and
    // let go when it dies.
    chainEmitters[3].rate = 2.5f;
    chainEmitters[3].capacity = 4;
    chainEmitters[3].lifetime = mvfxRangeSource(0.6f, 1.0f);
    chainEmitters[3].velocity.speed = (mvfxRange){2.0f, 3.0f};
    chainEmitters[3].events = mvfx_eventMaskBirth;
    chainEmitters[3].eventCapacity = 4;
    chainEmitters[3].angularVelocity = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0, 1.0f, -2.0f, 0}, .max = {0, 3.0f, 2.0f, 0}};
    chainEmitters[4].rate = 0.0f;
    chainEmitters[4].capacity = 4 * 3;
    chainEmitters[4].lifetime = mvfxRangeSource(1.6f, 1.6f);
    chainEmitters[4].motion.gravity[1] = -6.0f;
    chainEmitters[4].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                                   .source = 3,
                                                   .kind = mvfx_eventBirth,
                                                   .minCount = 1,
                                                   .maxCount = 1,
                                                   .capacity = 4};
    chainEmitters[4].unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                          .points = 3,
                                          .spacing = 0.2f,
                                          .direction = {0, -1, 0.5f},
                                          .substeps = 3,
                                          .bendCompliance = 0.05f,
                                          .damping = 0.8f,
                                          .pinFirst = true,
                                          .attach = mvfx_attachParticles};
    mvfxEffectDef chainEffectDef = mvfxDefaultEffectDef();
    chainEffectDef.emitters = chainEmitters;
    chainEffectDef.emitterCount = 6;
    mvfxEffectId chainEffect = {0};
    CHECK(mvfxCreateEffect(context, &chainEffectDef, &chainEffect) == mvfx_success, "chain effect");
    mvfxInstanceDef chainInstanceDef = mvfxDefaultInstanceDef();
    chainInstanceDef.effect = chainEffect;
    chainInstanceDef.origin[0] = 1495.0;
    chainInstanceDef.seed = 0xC4A1u;
    mvfxInstanceId chainInstance = {0};
    CHECK(mvfxCreateInstance(context, &chainInstanceDef, &chainInstance) == mvfx_success,
          "chain instance");
    for (int32_t i = 0; i < 4; ++i)
    {
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        instanceDef.origin[0] = 1000.0 * (double)i;
        instanceDef.seed = i == 3 ? 0 : 0xABCDu + (uint32_t)i;
        // The third plays at the effect's lower level.
        instanceDef.quality = i == 2 ? 1 : 0;
        // The second throttled to every third step while unseen, the first
        // paused (MoveScene hides them).
        instanceDef.offscreen = i == 1   ? mvfx_offscreenThrottle
                                : i == 0 ? mvfx_offscreenPause
                                         : mvfx_offscreenSimulate;
        instanceDef.offscreenInterval = 3;
        mvfxInstanceId instance = {0};
        CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success,
              "scene instance");
        // The second sets a wall at its x = 1, and a box it keeps inside.
        static const mvfxCollider walls[2] = {
            {.kind = mvfx_colliderPlane,
             .space = mvfx_spaceLocal,
             .center = {1, 0, 0},
             .normal = {-1, 0, 0}},
            {.kind = mvfx_colliderBox,
             .space = mvfx_spaceLocal,
             .inside = true,
             .halfExtents = {3, 3, 1.6f},
             .rotation = {0, 0.19509032f, 0, 0.98078528f}},
        };
        int32_t wallCount = i == 1 ? 2 : 0;
        CHECK(mvfxInstance_SetColliders(context, instance, walls, wallCount) == mvfx_success,
              "scene walls");
    }
    BuildRigidScene(context);
    BuildGasScene(context);
    BuildLiquidScene(context);
}

// Moves every instance of the context before a step: along a fixed path
// of exact values (quaternions of exact floats, scales of 1 and 1.5), with
// a teleport far enough at step 10 for world-space anchors to rebase.
// A depth image of 32 pixels a side: a camera 6 above an instance looking
// down its -Z at a plane tilted along X through its origin, 90 degrees
// wide, depth from 0.1 to 100.
#define SCENE_DEPTH_SIDE 32
static float s_sceneDepth[SCENE_DEPTH_SIDE * SCENE_DEPTH_SIDE];

static inline mvfxDepthImage SceneDepth(const double origin[3])
{
    const float a = (float)(100.0 / (0.1 - 100.0));
    for (int32_t r = 0; r < SCENE_DEPTH_SIDE; ++r)
    {
        for (int32_t c = 0; c < SCENE_DEPTH_SIDE; ++c)
        {
            // Along the ray (x, y, -1) s from the camera: 6 - s = 0.4 x s.
            float x = ((float)c + 0.5f) / (float)SCENE_DEPTH_SIDE * 2.0f - 1.0f;
            float s = 6.0f / (1.0f + 0.4f * x);
            s_sceneDepth[r * SCENE_DEPTH_SIDE + c] = (a * -s + a * 0.1f) / s;
        }
    }
    mvfxDepthImage image = {
        .pixels = s_sceneDepth,
        .byteCount = (int64_t)sizeof s_sceneDepth,
        .width = SCENE_DEPTH_SIDE,
        .height = SCENE_DEPTH_SIDE,
        .stride = SCENE_DEPTH_SIDE * 4,
        .format = mvfx_depthFloat32,
        .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, a, -1, 0, 0, a * 0.1f, 0},
        .origin = {origin[0], origin[1], origin[2] + 6.0},
        .rotation = {0, 0, 0, 1},
    };
    return image;
}

// Answers every scene query of the context as a host's scene might: a
// wall halfway along the segment of every third particle by serial, of
// every other one from step 8, facing back along it; nothing for the rest.
static inline void AnswerScene(mvfxContext* context, int32_t step)
{
    uint32_t every = step < 8 ? 3u : 2u;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        const mvfxInstance* instance = context->instances.slots[i].object;
        if (instance == NULL)
        {
            continue;
        }
        mvfxInstanceId id = {i + 1, context->instances.slots[i].generation};
        for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
        {
            const mvfxSceneQuery* queries = NULL;
            int32_t count = 0;
            CHECK(mvfxInstance_GetSceneQueries(context, id, e, &queries, &count) == mvfx_success,
                  "scene queries");
            mvfxSceneAnswer answers[32] = {0};
            for (int32_t q = 0; q < count && q < 32; ++q)
            {
                const mvfxSceneQuery* query = &queries[q];
                const float* d = query->segment;
                answers[q] = (mvfxSceneAnswer){
                    .hit = query->serial % every == 0,
                    .point = {query->origin[0] + 0.5 * (double)d[0],
                              query->origin[1] + 0.5 * (double)d[1],
                              query->origin[2] + 0.5 * (double)d[2]},
                    .normal = {-d[0], -d[1], -d[2]},
                };
            }
            CHECK(mvfxInstance_AnswerSceneQueries(context, id, e, answers, count) == mvfx_success,
                  "scene answered");
        }
    }
}

static inline void MoveScene(mvfxContext* context, int32_t step)
{
    AnswerScene(context, step);
    static const float turns[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.6f, 0.8f}};
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        if (context->instances.slots[i].object == NULL)
        {
            continue;
        }
        mvfxInstanceId id = {i + 1, context->instances.slots[i].generation};
        double far = step >= 10 ? 3000.0 : 0.0;
        const double origin[3] = {far + 2.5 * step + (double)i, 0.125 * step * step, -1.0 * step};
        CHECK(mvfxInstance_SetTransform(context, id, origin, turns[step % 2],
                                        step % 3 == 0 ? 1.5f : 1.0f, step == 10) == mvfx_success,
              "scene moved");
        const mvfxEffect* effect =
            ((const mvfxInstance*)context->instances.slots[i].object)->effect;
        // The gases' host obstacle: a sphere in the instance's frame
        // crossing the 3D gas, so it moves through the box.
        if (effect->gasCount > 0)
        {
            const mvfxCollider ball = {.kind = mvfx_colliderSphere,
                                       .space = mvfx_spaceLocal,
                                       .center = {-0.3f + 0.04f * (float)step, 0.45f, 0.05f},
                                       .radius = 0.15f};
            CHECK(mvfxInstance_SetColliders(context, id, &ball, 1) == mvfx_success, "gas ball");
        }
        if (step == 5 && effect->emitterCount == 5 && effect->parameterCount > 0)
        {
            static const float aimed[4] = {-2.0f, 3.0f, 1.0f, 0.0f};
            CHECK(mvfxInstance_SetParameter(context, id, 0, aimed) == mvfx_success, "scene aim");
        }
        if (step == 6)
        {
            CHECK(mvfxInstance_BreakRibbons(context, id, -1) == mvfx_success, "scene break");
        }
        // Banners' frames: one swinging round the instance, one unset until
        // step 4.
        for (int32_t e = 0; e < effect->emitterCount; ++e)
        {
            if (effect->emitters[e].unit.attach != mvfx_attachHost)
            {
                continue;
            }
            const mvfxAttachFrame frames[2] = {
                {.position = {origin[0] + 1.0, origin[1] + 0.25 * step, origin[2]},
                 .rotation = {0.0f, 0.6f, 0.0f, 0.8f}},
                {.position = {origin[0] - 2.0, origin[1], origin[2] + 1.0},
                 .rotation = {0.0f, 0.0f, 0.0f, 1.0f}}};
            CHECK(mvfxInstance_SetAttachFrames(context, id, e, frames, step < 4 ? 1 : 2) ==
                      mvfx_success,
                  "scene frames");
            // A banner on frame 1 before it is set (held), and after.
            static const int32_t second = 1;
            mvfxSpawnRequest request = {
                .count = 1, .positions = frames[1].position, .attachFrames = &second};
            if (step == 2 || step == 6)
            {
                CHECK(mvfxInstance_Spawn(context, id, e, &request) == mvfx_success, "scene banner");
            }
        }
        // The throttled unseen over the longest step, the paused for three.
        const mvfxInstance* live = context->instances.slots[i].object;
        bool hidden = (live->offscreen == mvfx_offscreenThrottle && step >= 3 && step < 15) ||
                      (live->offscreen == mvfx_offscreenPause && step >= 7 && step < 10);
        CHECK(mvfxInstance_SetVisible(context, id, !hidden) == mvfx_success, "scene seen");
        mvfxDepthImage depth = SceneDepth(origin);
        CHECK(mvfxInstance_SetDepthImage(context, id, &depth) == mvfx_success, "scene depth");
    }
}

// A record of every field, in many formats: what the scene's output
// writes for a view.
static const mvfxLayoutField s_sceneFields[22] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 3, .offset = 12},
    {.field = mvfx_recordColor,
     .format = mvfx_formatUnorm8,
     .components = 4,
     .srgb = true,
     .offset = 18},
    {.field = mvfx_recordAngle, .format = mvfx_formatFloat32, .components = 1, .offset = 24},
    {.field = mvfx_recordOrientation, .format = mvfx_formatSnorm16, .components = 4, .offset = 28},
    {.field = mvfx_recordVelocity, .format = mvfx_formatFloat16, .components = 3, .offset = 36},
    {.field = mvfx_recordAge, .format = mvfx_formatUnorm16, .components = 1, .offset = 42},
    {.field = mvfx_recordFrame, .format = mvfx_formatUint32, .components = 1, .offset = 44},
    {.field = mvfx_recordCustom, .format = mvfx_formatSnorm8, .components = 4, .offset = 48},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 52},
    {.field = mvfx_recordMesh, .format = mvfx_formatUint32, .components = 1, .offset = 56},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat16, .components = 3, .offset = 60},
    {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 68},
    {.field = mvfx_recordLength, .format = mvfx_formatUnorm16, .components = 1, .offset = 72},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 76},
    {.field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 1, .offset = 80},
    {.field = mvfx_recordSide, .format = mvfx_formatSnorm16, .components = 3, .offset = 84},
    {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 3, .offset = 92},
    {.field = mvfx_recordCorner, .format = mvfx_formatUnorm16, .components = 2, .offset = 104},
    {.field = mvfx_recordNeighborCount,
     .format = mvfx_formatUint32,
     .components = 1,
     .offset = 108},
    {.field = mvfx_recordNeighborCenter,
     .format = mvfx_formatFloat32,
     .components = 3,
     .offset = 112},
    {.field = mvfx_recordNeighborVelocity,
     .format = mvfx_formatFloat16,
     .components = 3,
     .offset = 124},
};
#define SCENE_RECORD_BYTES 132
#define SCENE_RECORDS      128

// Hashes an emitter's ribbons for a view: their points and list.
static inline uint64_t HashRibbons(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                   mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                   uint64_t hash)
{
    // Room for every particle's segment cut into the most pieces.
    enum
    {
        POINTS = SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES
    };
    static unsigned char points[POINTS * SCENE_RECORD_BYTES];
    static mvfxRibbon ribbons[SCENE_RECORDS];
    mvfxRibbonBuffers buffers = {.points = points,
                                 .pointCapacity = POINTS,
                                 .ribbons = ribbons,
                                 .ribbonCapacity = SCENE_RECORDS,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    int64_t pointCount = 0;
    int64_t ribbonCount = 0;
    CHECK(mvfxInstance_WriteRibbons(context, id, emitter, layout, view, &buffers, &pointCount,
                                    &ribbonCount) == mvfx_success &&
              pointCount <= POINTS,
          "scene ribbons");
    hash = mvfxHash64(hash, points, (int32_t)pointCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, ribbons, (int32_t)(ribbonCount * (int64_t)sizeof ribbons[0]));
}

// Hashes an emitter's quads for a view: their vertices and indices.
static inline uint64_t HashQuads(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                 mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                 uint64_t hash)
{
    static unsigned char vertices[4 * SCENE_RECORDS * SCENE_RECORD_BYTES];
    static uint32_t indices[6 * SCENE_RECORDS];
    mvfxExpandBuffers buffers = {.vertices = vertices,
                                 .vertexCapacity = 4 * SCENE_RECORDS,
                                 .indices = indices,
                                 .indexCapacity = 6 * SCENE_RECORDS,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    int64_t count = 0;
    CHECK(mvfxInstance_ExpandSprites(context, id, emitter, layout, view, &buffers, &count) ==
                  mvfx_success &&
              count <= SCENE_RECORDS,
          "scene quads");
    hash = mvfxHash64(hash, vertices, (int32_t)(4 * count) * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, (int32_t)(6 * count * (int64_t)sizeof indices[0]));
}

// Hashes an emitter's ribbons expanded into strips for a view.
// Ribbons as capped tubes of three sides.
static inline uint64_t HashTubes(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                 mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                 uint64_t hash)
{
    enum
    {
        VERTICES = 4 * SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES + 10 * SCENE_RECORDS,
        INDICES = 18 * SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES + 18 * SCENE_RECORDS
    };
    static unsigned char vertices[VERTICES * SCENE_RECORD_BYTES];
    static uint32_t indices[INDICES];
    mvfxExpandBuffers buffers = {.vertices = vertices,
                                 .vertexCapacity = VERTICES,
                                 .indices = indices,
                                 .indexCapacity = INDICES,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    static const mvfxTubeDef tube = {.sides = 3, .caps = true};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandTubes(context, id, emitter, layout, view, &tube, &buffers,
                                   &vertexCount, &indexCount) == mvfx_success &&
              vertexCount <= VERTICES && indexCount <= INDICES,
          "scene tubes");
    hash = mvfxHash64(hash, vertices, (int32_t)vertexCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, (int32_t)(indexCount * (int64_t)sizeof indices[0]));
}

static inline uint64_t HashStrips(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                  mvfxLayoutId layout, const mvfxView* view, uint32_t* scratch,
                                  uint64_t hash)
{
    enum
    {
        VERTICES = 2 * SCENE_RECORDS * MVFX_MAX_RIBBON_PIECES
    };
    static unsigned char vertices[VERTICES * SCENE_RECORD_BYTES];
    static uint32_t indices[3 * VERTICES];
    mvfxExpandBuffers buffers = {.vertices = vertices,
                                 .vertexCapacity = VERTICES,
                                 .indices = indices,
                                 .indexCapacity = 3 * VERTICES,
                                 .scratch = scratch,
                                 .scratchBytes = SCENE_RECORDS * 16};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandRibbons(context, id, emitter, layout, view, &buffers, &vertexCount,
                                     &indexCount) == mvfx_success &&
              vertexCount <= VERTICES && indexCount <= 3 * VERTICES,
          "scene strips");
    hash = mvfxHash64(hash, vertices, (int32_t)vertexCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, (int32_t)(indexCount * (int64_t)sizeof indices[0]));
}

// Hashes an emitter's plexus links for a view, as points and as strips.
static inline uint64_t HashPlexus(mvfxContext* context, mvfxInstanceId id, int32_t emitter,
                                  mvfxLayoutId layout, const mvfxView* view, uint64_t hash)
{
    enum
    {
        LINKS = 64
    };
    static unsigned char points[4 * LINKS * SCENE_RECORD_BYTES];
    static mvfxRibbon ribbons[LINKS];
    static uint32_t indices[6 * LINKS];
    mvfxRibbonBuffers buffers = {
        .points = points, .pointCapacity = 2 * LINKS, .ribbons = ribbons, .ribbonCapacity = LINKS};
    int64_t pointCount = 0;
    int64_t linkCount = 0;
    // The layout writes its fields only: the bytes between them are the
    // buffer's, cleared so the hash is of what this call wrote (left from
    // earlier calls they differed by platform).
    memset(points, 0, sizeof points);
    memset(ribbons, 0, sizeof ribbons);
    CHECK(mvfxInstance_WritePlexus(context, id, emitter, layout, view, &buffers, &pointCount,
                                   &linkCount) == mvfx_success &&
              linkCount <= LINKS,
          "scene plexus");
    hash = mvfxHash64(hash, points, (int32_t)pointCount * SCENE_RECORD_BYTES);
    hash = mvfxHash64(hash, ribbons, (int32_t)(linkCount * (int64_t)sizeof ribbons[0]));
    memset(points, 0, sizeof points);
    mvfxExpandBuffers strips = {.vertices = points,
                                .vertexCapacity = 4 * LINKS,
                                .indices = indices,
                                .indexCapacity = 6 * LINKS};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandPlexus(context, id, emitter, layout, view, &strips, &vertexCount,
                                    &indexCount) == mvfx_success,
          "scene plexus strips");
    hash = mvfxHash64(hash, points, (int32_t)vertexCount * SCENE_RECORD_BYTES);
    return mvfxHash64(hash, indices, (int32_t)(indexCount * (int64_t)sizeof indices[0]));
}

// Hashes every emitter's records of an instance for a view at (1500, 3,
// -2), through a layout made on the context the first time.
static inline uint64_t HashOutput(mvfxContext* context, mvfxInstanceId id,
                                  const mvfxInstance* instance, uint64_t hash)
{
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = s_sceneFields;
    def.fieldCount = 22;
    def.stride = SCENE_RECORD_BYTES;
    mvfxLayoutId layout = {1, 1};
    if (!mvfxLayout_IsValid(context, layout))
    {
        CHECK(mvfxCreateLayout(context, &def, &layout) == mvfx_success, "scene layout");
    }
    // The eye among the first instances looks down and a little along x.
    static const mvfxView view = {.origin = {1500.0, 3.0, -2.0},
                                  .eye = {12.0, 1.0, 2.0},
                                  .forward = {0.25f, -0.125f, -1.0f},
                                  .up = {0.0f, 1.0f, 0.25f}};
    static unsigned char records[SCENE_RECORDS * SCENE_RECORD_BYTES];
    static uint32_t scratch[SCENE_RECORDS * 4];
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        int64_t count = 0;
        CHECK(mvfxInstance_WriteParticles(context, id, e, layout, &view, scratch, sizeof scratch,
                                          records, SCENE_RECORDS, &count) == mvfx_success &&
                  count <= SCENE_RECORDS,
              "scene output");
        hash = mvfxHash64(hash, records, (int32_t)count * SCENE_RECORD_BYTES);
        hash = HashRibbons(context, id, e, layout, &view, scratch, hash);
        hash = HashQuads(context, id, e, layout, &view, scratch, hash);
        hash = HashStrips(context, id, e, layout, &view, scratch, hash);
        hash = HashTubes(context, id, e, layout, &view, scratch, hash);
        hash = HashPlexus(context, id, e, layout, &view, hash);
        static mvfxLight lights[SCENE_RECORDS];
        CHECK(mvfxInstance_WriteLights(context, id, e, &view, lights, SCENE_RECORDS, &count) ==
                      mvfx_success &&
                  count <= SCENE_RECORDS,
              "scene lights");
        hash = mvfxHash64(hash, lights, (int32_t)(count * (int64_t)sizeof lights[0]));
    }
    return hash;
}

// A hash of every live instance of the context, in slot order, and of
// its output.
static inline uint64_t HashScene(mvfxContext* context)
{
    uint64_t hash = MVFX_HASH_INIT;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        const mvfxInstance* instance = context->instances.slots[i].object;
        if (instance != NULL)
        {
            hash = mvfxHashInstance(instance, hash);
            mvfxInstanceId id = {i + 1, context->instances.slots[i].generation};
            hash = HashOutput(context, id, instance, hash);
            mvfxBounds bounds = {0};
            CHECK(mvfxInstance_GetBounds(context, id, -1, &bounds) == mvfx_success, "scene bounds");
            hash = mvfxHash64(hash, &bounds.empty, 1);
            hash = mvfxHash64(hash, bounds.min, (int32_t)sizeof bounds.min);
            hash = mvfxHash64(hash, bounds.max, (int32_t)sizeof bounds.max);
        }
    }
    // The draw order for a view looking down +x past a plane at x = 1490.
    static const double eye[3] = {1500.0, 3.0, -2.0};
    const mvfxFrustum frustum = {.planes = {{1, 0, 0, -1490}, {0, 1, 0, 40}}, .planeCount = 2};
    static mvfxDrawItem items[64];
    int64_t count = 0;
    CHECK(mvfxContext_OrderEmitters(context, eye, &frustum, items, 64, &count) == mvfx_success,
          "scene order");
    // The twins' six copies hold more than the buffer: its first 64.
    int64_t kept = count < 64 ? count : 64;
    hash = mvfxHash64(hash, &count, (int32_t)sizeof count);
    return mvfxHash64(hash, items, (int32_t)(kept * (int64_t)sizeof items[0]));
}

#endif
