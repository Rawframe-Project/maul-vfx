// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene the determinism tests step: an effect of three endless
// emitters over four instances, with full and churning emitters among
// them, and an effect of emitter lives (a rate curve, looping bursts,
// delays) over two, and an effect of mesh emitters over one; a fixed
// sequence of steps, and a hash of every instance's state.

#ifndef MAUL_VFX_TEST_VFX_SCENE_H
#define MAUL_VFX_TEST_VFX_SCENE_H

#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"

// The steps of the scene, in ticks: uneven, as a host's frames are.
static const int64_t s_sceneSteps[] = {17476, 17477, 16000, 1,     40000, 17476, 3,
                                       99999, 17476, 17476, 8192,  65536, 17477, 1048576,
                                       5,     17476, 30000, 17476, 17476, 2};

#define SCENE_STEP_COUNT ((int32_t)(sizeof s_sceneSteps / sizeof s_sceneSteps[0]))

// Creates the scene's effects and instances on a context.
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
    mvfxEmitterDef emitters[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef()};
    emitters[0].capacity = 100;
    emitters[0].rate = 37.5f;
    emitters[0].lifetime = mvfxRangeSource(0.25f, 1.5f);
    emitters[1].capacity = 64;
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
    // Kill rules, in the emitter's space: past a sphere on the first, past
    // a plane on the second.
    static const mvfxKillRule outside = {
        .kind = mvfx_killOutsideSphere, .space = mvfx_spaceLocal, .radius = 2.5f};
    static const mvfxKillRule plane = {.kind = mvfx_killPastPlane,
                                       .space = mvfx_spaceLocal,
                                       .center = {0, 0, 1.75f},
                                       .normal = {0.2f, 0, 1}};
    emitters[0].killRules = &outside;
    emitters[0].killRuleCount = 1;
    emitters[1].killRules = &plane;
    emitters[1].killRuleCount = 1;
    // Seven a second, so it spawns within the scene's 1.4 seconds and
    // fills.
    emitters[2].rate = 7.0f;
    emitters[2].lifetime = mvfxRangeSource(5.0f, 9.0f);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 3;
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
    for (int32_t i = 0; i < 4; ++i)
    {
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        instanceDef.origin[0] = 1000.0 * (double)i;
        instanceDef.seed = i == 3 ? 0 : 0xABCDu + (uint32_t)i;
        mvfxInstanceId instance = {0};
        CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success,
              "scene instance");
    }
}

// Moves every instance of the context before a step: along a fixed path
// of exact values (quaternions of exact floats, scales of 1 and 1.5), with
// a teleport far enough at step 10 for world-space anchors to rebase.
static inline void MoveScene(mvfxContext* context, int32_t step)
{
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
    }
}

// A hash of every live instance of the context, in slot order.
static inline uint64_t HashScene(const mvfxContext* context)
{
    uint64_t hash = MVFX_HASH_INIT;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        const mvfxInstance* instance = context->instances.slots[i].object;
        if (instance != NULL)
        {
            hash = mvfxHashInstance(instance, hash);
        }
    }
    return hash;
}

#endif
