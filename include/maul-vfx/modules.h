// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's modules beyond spawn and forces: values over a particle's
// life, the velocity modules a step applies (a scale of each particle's
// motion, an added velocity, orbit and radial velocity, a speed limit),
// and the values over life its particles show (colour, alpha, size,
// flipbook frame, rotation speed).

#ifndef MAUL_VFX_MODULES_H
#define MAUL_VFX_MODULES_H

#include "maul-vfx/curve.h"
#include "maul-vfx/grid.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most custom attributes an emitter declares.
#define MVFX_MAX_CUSTOM_ATTRIBUTES 8

    // A value over a particle's life: value times the curve at the
    // particle's normalized age (its age over its life, held to [0, 1]);
    // a curve with no keys is 1. value is finite and at most MVFX_MAX_VALUE
    // in size, the curve's keys' times in [0, 1]; the effect bakes it.
    typedef struct mvfxLifeCurve
    {
        float value;
        mvfxCurve curve;
    } mvfxLifeCurve;

    // The velocity modules, taken in the emitter's space (turning and
    // scaling with the instance), each at the particle's age at the step's
    // start (or its birth) and held over its span. The speed limit changes
    // a particle's velocity; the others move it over the step and are not
    // kept, so they never build up.
    typedef struct mvfxVelocityModules
    {
        // Scales each particle's own motion over a step (under its velocity,
        // gravity, drag and forces): 1 leaves it, 0 lets only the added
        // velocities below move it, as setting the velocity would.
        mvfxLifeCurve motionScale;
        // A velocity added to the motion: addedDirection (normalized; any
        // when the speed is 0) times addedSpeed.
        float addedDirection[3];
        mvfxLifeCurve addedSpeed;
        // Turns each particle about the axis through orbitCenter at
        // orbitRate radians a second, exactly (on its circle).
        float orbitAxis[3];
        float orbitCenter[3];
        mvfxLifeCurve orbitRate;
        // Moves each particle away from radialCenter at radialSpeed (toward
        // it when negative; none at the center).
        float radialCenter[3];
        mvfxLifeCurve radialSpeed;
        // Whether speed is limited: the speed above speedLimit (from 0) is
        // lost as 1 - e^(-limitDampen t), limitDampen above 0 and at most
        // MVFX_MAX_DRAG a second, so the limit holds whatever the steps.
        bool limitSpeed;
        mvfxLifeCurve speedLimit;
        float limitDampen;
    } mvfxVelocityModules;

    // What an over-life value is taken over: the particle's normalized age,
    // or its speed between speedRange's ends (in the emitter's units, 0 at
    // the first and 1 at the second, held to [0, 1]).
    typedef uint8_t mvfxOverLifeInput;
    enum
    {
        mvfx_overAge = 0,
        mvfx_overSpeed = 1,
    };

    // Colour over life: the particle's colour (its initial value, or white)
    // times the gradient, and its alpha times alpha over age besides
    // (Godot's split of alpha from colour). Both are taken where a colour is
    // read (output, events, ribbons), never written back.
    typedef struct mvfxColorOverLife
    {
        bool enabled;
        mvfxOverLifeInput input;
        // Finite, the first below the second, at most MVFX_MAX_EXTENT.
        float speedRange[2];
        // The colour's factor; no keys is white.
        mvfxGradient gradient;
        // Whether alpha is scaled over age too, by alpha.
        bool scaleAlpha;
        mvfxLifeCurve alpha;
    } mvfxColorOverLife;

    // Size over life: the particle's size (its initial value, or 1) times
    // size[0] on every axis, or size[0], [1] and [2] on X, Y and Z.
    typedef struct mvfxSizeOverLife
    {
        bool enabled;
        mvfxOverLifeInput input;
        float speedRange[2];
        bool perAxis;
        mvfxLifeCurve size[3];
    } mvfxSizeOverLife;

    // How a flipbook frame moves over life.
    typedef uint8_t mvfxFrameAnimation;
    enum
    {
        // The frame it starts at, always.
        mvfx_frameStill = 0,
        // Through the frames cycles times over its life.
        mvfx_frameOverLife = 1,
        // At frameRate frames a second of its age.
        mvfx_frameAtRate = 2,
    };

    // A flipbook over life: its frame is its start frame (or 0) plus the
    // frames it has moved, wrapped to the frames (looping) or held at the
    // last.
    typedef struct mvfxFrameOverLife
    {
        mvfxFrameAnimation animation;
        // The flipbook's frames, from 1 to MVFX_MAX_FRAMES.
        int32_t frameCount;
        // Over life: above 0 and at most 1,000. At a rate: above 0 and at most
        // 10,000 frames a second.
        float cycles;
        float frameRate;
        bool loop;
    } mvfxFrameOverLife;

    // A rotation speed over life: a sprite's spin and a mesh particle's
    // angular velocity times scale at its age, each step (an angle sums
    // over steps, so this one is not taken where it is read).
    typedef struct mvfxRotationOverLife
    {
        bool enabled;
        mvfxLifeCurve scale;
    } mvfxRotationOverLife;

    // The values over life an emitter's particles show.
    typedef struct mvfxOverLife
    {
        mvfxColorOverLife color;
        mvfxSizeOverLife size;
        mvfxFrameOverLife frame;
        mvfxRotationOverLife rotation;
    } mvfxOverLife;

// The most kill rules an emitter has.
#define MVFX_MAX_KILL_RULES 8

    // What a kill rule kills. Region rules test the straight segment a
    // particle covered over a step, so a particle is caught however fast it
    // went; how a curving path is cut into steps can still decide it.
    typedef uint8_t mvfxKillKind;
    enum
    {
        // Passing through a sphere of radius about center.
        mvfx_killInsideSphere = 0,
        // Ending a step outside it.
        mvfx_killOutsideSphere = 1,
        // Passing through a box of halfExtents about center, turned by
        // rotation.
        mvfx_killInsideBox = 2,
        // Ending a step outside it.
        mvfx_killOutsideBox = 3,
        // Ending a step past the plane through center, on the side normal
        // points to.
        mvfx_killPastPlane = 4,
        // Ending a step slower than speed, once older than minimumAge
        // seconds.
        mvfx_killSlower = 5,
        // Having collided collisions times (an emitter that collides).
        mvfx_killCollisions = 6,
    };

    // A kill rule, tested at the end of each step; a particle it kills is
    // gone from then. Its places and lengths are in its space, the world's or
    // the emitter's (turning and scaling with the instance); each value
    // finite and at most MVFX_MAX_EXTENT in size, fields its kind does not
    // read not checked.
    typedef struct mvfxKillRule
    {
        mvfxKillKind kind;
        // A mvfxSimulationSpace: 0 the world's, 1 the emitter's.
        uint8_t space;
        float center[3];
        // A sphere's radius, above 0.
        float radius;
        // A box's half sizes, each above 0, and its rotation, a unit
        // quaternion within 0.001.
        float halfExtents[3];
        float rotation[4];
        // A plane's normal, not of length 0.
        float normal[3];
        // The speed, at least 0, and the age before which it does not kill,
        // from 0 to MVFX_MAX_LIFETIME seconds.
        float speed;
        float minimumAge;
        // The collisions that kill, from 1.
        int32_t collisions;
    } mvfxKillRule;

// The most colliders an emitter has, and an instance the host sets.
#define MVFX_MAX_COLLIDERS 8

// The most scene queries an emitter writes a step.
#define MVFX_MAX_SCENE_QUERIES 65536

// The most points a step of a particle is tested at against a depth image.
#define MVFX_MAX_DEPTH_SAMPLES 64

// The most events an emitter writes a step.
#define MVFX_MAX_EVENTS 65536

    // What happened to a particle.
    typedef uint8_t mvfxEventKind;
    enum
    {
        // It was born.
        mvfx_eventBirth = 0,
        // Its life ended, or it was killed (by a kill rule, a collision, a
        // life cut by collisions or a host module).
        mvfx_eventDeath = 1,
        // It collided, at its step's first contact.
        mvfx_eventCollision = 2,
    };

    // The kinds an emitter writes, as a mask.
    enum
    {
        mvfx_eventMaskBirth = 1u << mvfx_eventBirth,
        mvfx_eventMaskDeath = 1u << mvfx_eventDeath,
        mvfx_eventMaskCollision = 1u << mvfx_eventCollision,
    };

// The most particles one event spawns.
#define MVFX_MAX_EVENT_SPAWN 1024

    // An emitter spawning from another's events (a sub-emitter): for each
    // event of a kind its source wrote in the step, from minCount to
    // maxCount particles (drawn for the event), born at its tick and its
    // place, with the emitter's own velocity plus inheritVelocity of the
    // event's, and its colour and size when asked. The source steps first
    // (an effect's emitters step in an order its links make; a cycle is
    // refused); the source's capacity of events bounds the spawns, and so
    // do capacity a step and the emitter's own room, the rest counted
    // refused. Spawns follow the emitter's delay, loops and a stop, as its
    // rate does.
    typedef struct mvfxEventSpawn
    {
        bool enabled;
        // The source's index in the effect's emitters, not its own; it must
        // write the kind.
        int32_t source;
        mvfxEventKind kind;
        // From 0 to MVFX_MAX_EVENT_SPAWN, minCount at most maxCount.
        int32_t minCount;
        int32_t maxCount;
        // From 0 to 1.
        float inheritVelocity;
        bool inheritColor;
        bool inheritSize;
        // The most births from events a step, from 1 to
        // MVFX_MAX_EMITTER_CAPACITY; past it the latest by tick are refused.
        int32_t capacity;
    } mvfxEventSpawn;

    // An event: what happened, to which particle, when and where.
    typedef struct mvfxEvent
    {
        mvfxEventKind kind;
        // A death by a kill rather than by age.
        bool killed;
        uint32_t serial;
        // The instance's tick it happened at: a birth's tick, an age's
        // death tick, a killed particle's step end, a contact's tick.
        int64_t tick;
        // In the world: its place (an age's death: where it was at the
        // step's start, its last; a collision: the contact), its velocity
        // (a collision's: after the contact), and a collision's surface
        // normal (0 otherwise).
        double place[3];
        float velocity[3];
        float normal[3];
        // Its colour and size then, with the values over life (1 where the
        // emitter stores none).
        float color[4];
        float size[3];
    } mvfxEvent;

    // What a collider is. Particles stay on its allowed side: above a plane,
    // outside a sphere or a box, or inside one (inside).
    typedef uint8_t mvfxColliderKind;
    enum
    {
        // The plane through center, its allowed side the one normal points to.
        mvfx_colliderPlane = 0,
        // A sphere of radius about center.
        mvfx_colliderSphere = 1,
        // A box of halfExtents about center, turned by rotation.
        mvfx_colliderBox = 2,
        // A grid of heights over a box's X and Z (halfExtents[0] and [2],
        // about center, turned by rotation): a grid of one component and
        // one point along Y, its values heights along the box's Y above
        // center. Particles keep above it (below it, inside); outside the
        // box's X and Z it does nothing.
        mvfx_colliderHeightfield = 3,
        // A grid of signed distances spanning a box (halfExtents about
        // center, turned by rotation), one component, negative within the
        // solid. Particles keep where it is positive (negative, inside);
        // outside the box it does nothing.
        mvfx_colliderDistance = 4,
    };

    // A collider. Its places and lengths are in its space, the world's or the
    // emitter's (turning and scaling with the instance); each value finite
    // and at most MVFX_MAX_EXTENT in size, fields its kind does not read not
    // checked. A particle's step is tested as the segment it covered, so it
    // never passes through a primitive however long the step; a grid is
    // sampled along the segment every half cell (at most 256 times within
    // its box, more coarsely past that), so nothing thicker than a cell is
    // passed through. One that starts a step on the wrong side is put back
    // on the surface (a grid's: along its gradient).
    typedef struct mvfxCollider
    {
        mvfxColliderKind kind;
        // A mvfxSimulationSpace: 0 the world's, 1 the emitter's.
        uint8_t space;
        // A sphere's or a box's particles kept inside it.
        bool inside;
        float center[3];
        // A plane's normal, not of length 0.
        float normal[3];
        // A sphere's radius, above 0.
        float radius;
        // A box's or a grid's half sizes, each above 0 (a heightfield's Y not
        // read), and its rotation, a unit quaternion within 0.001.
        float halfExtents[3];
        float rotation[4];
        // A heightfield's or a distance grid's grid, its points spread over
        // the box from face to face; values in the collider's space. It may
        // not be destroyed while an effect or an instance holds it.
        mvfxGridId grid;
    } mvfxCollider;

    // How an emitter's particles collide.
    typedef struct mvfxCollision
    {
        // Whether they collide at all (with the emitter's colliders, and with
        // the instance's when hostColliders is set).
        bool enabled;
        bool hostColliders;
        // The share of the speed into a surface it bounces back with, and the
        // share of the speed along it a collision takes, each 0 to 1.
        float restitution;
        float friction;
        // The share of its life left a collision takes, 0 to 1 (1 kills).
        float lifeLoss;
        // Whether a collision kills it.
        bool killOnCollision;
        // Below this speed after a collision it stops where it hit, from 0 to
        // MVFX_MAX_EXTENT.
        float restSpeed;
        // Its radius, from 0 to MVFX_MAX_EXTENT: colliders grow by it (a
        // box's corners stay square).
        float radius;
        // Whether they collide with the host's scene. Each step ends with a
        // query for each moving particle, up to queryCapacity (from 1 to
        // MVFX_MAX_SCENE_QUERIES; the rest counted dropped): its place and
        // the segment it covers in lookahead seconds (above 0, at most 1) at
        // its velocity. The host answers each with a contact plane or none
        // (mvfxInstance_AnswerSceneQueries) before the next step, which
        // collides the particle's segment with its plane; a particle keeps
        // its last plane until an answer replaces it.
        bool sceneQueries;
        int32_t queryCapacity;
        float lookahead;
        // Whether they collide with the depth image the host sets on the
        // instance (mvfxInstance_SetDepthImage): a particle behind the
        // image's surface by less than depthThickness (above 0, at most
        // MVFX_MAX_EXTENT, in the world) has met it. A step is tested at
        // its segment's end and at up to depthSamples points along it (from
        // 1 to MVFX_MAX_DEPTH_SAMPLES), one a pixel it crosses, so a fast
        // particle does not pass a thin wall. Outside the image (or behind
        // the camera) there is no depth and they do not collide, or are
        // killed there when killOutsideView is set.
        bool depthImage;
        float depthThickness;
        int32_t depthSamples;
        bool killOutsideView;
    } mvfxCollision;

// The most host modules an emitter has.
#define MVFX_MAX_HOST_MODULES 8

    // When in a step a host module runs.
    typedef uint8_t mvfxModuleStage;
    enum
    {
        // On the particles alive at the step's start (its ended ones taken
        // out), before they move: what it writes to a velocity moves them.
        mvfx_stageBeforeMotion = 0,
        // On every particle at the step's end (the step's births among
        // them), before the killed are taken out: it may kill.
        mvfx_stageAfterMotion = 1,
    };

    // An emitter's particle arrays as a host module sees them: one value a
    // particle in each, count long; NULL for what the emitter does not
    // store. Places are in the emitter's space: offsets from anchor for a
    // world-space emitter, the instance's own space for a local one.
    typedef struct mvfxParticleArrays
    {
        int32_t count;
        float* position[3];
        float* velocity[3];
        float* color[4];
        float* size[3];
        float* angle;
        float* spin;
        float* orientation[4];
        float* angularVelocity[3];
        uint32_t* frame;
        // Custom attribute i's components, the unused ones NULL.
        float* custom[MVFX_MAX_CUSTOM_ATTRIBUTES][4];
        // Read only: each particle's serial, birth tick (low and high words)
        // and inverse life in seconds.
        const uint32_t* serial;
        const uint32_t* birthLow;
        const uint32_t* birthHigh;
        const float* inverseLife;
        // Each particle's collisions so far, for an emitter that collides.
        uint32_t* collisions;
        // After motion: set a particle's byte to 1 to kill it (it is counted
        // killed); NULL before motion.
        uint8_t* kill;
    } mvfxParticleArrays;

    // What a host module is called with.
    typedef struct mvfxModuleCall
    {
        mvfxParticleArrays particles;
        mvfxModuleStage stage;
        // The emitter's index in its effect and the module's in the emitter.
        int32_t emitter;
        int32_t module;
        // The step: its length in seconds, and the instance's clock in ticks
        // at its start and its end.
        double seconds;
        int64_t startTick;
        int64_t endTick;
        // A world-space emitter's anchor, which places are offsets from.
        double anchor[3];
        bool worldSpace;
        // The seed of this module's random stream in this instance
        // (mvfxModule_Random).
        uint32_t seed;
    } mvfxModuleCall;

    // A host module: a function over an emitter's particles. It runs inside
    // the step on the thread stepping the instance (instances may step on
    // several threads at once), so it must be safe to run beside itself;
    // and the step stays deterministic only if the module is, which is the
    // host's to keep (mvfxModule_Random gives it a stream that is).
    typedef void mvfxModuleFn(const mvfxModuleCall* call, void* user);

    typedef struct mvfxHostModule
    {
        mvfxModuleStage stage;
        mvfxModuleFn* function;
        void* user;
    } mvfxHostModule;

#ifdef __cplusplus
}
#endif

#endif
