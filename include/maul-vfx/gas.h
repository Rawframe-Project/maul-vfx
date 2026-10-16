// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gases: smoke and fire on a grid in a box, by Stam's stable fluids. An
// effect's gas is a box of cells holding a velocity (at the cells'
// faces) and density, temperature and fuel (at their centers), stepped
// with its instance: sources add to it, its velocity carries it and
// itself, and each field may decay. A host reads its fields to draw them
// (ray marching, a flipbook bake): the library draws no volume.

#ifndef MAUL_VFX_GAS_H
#define MAUL_VFX_GAS_H

#include "maul-vfx/context.h"
#include "maul-vfx/modules.h"
#include "maul-vfx/shape.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most gases an effect has.
#define MVFX_MAX_GASES 4

// The most cells a gas has along an axis.
#define MVFX_MAX_GAS_SIZE 512

// The most cells a gas has.
#define MVFX_MAX_GAS_CELLS (1 << 21)

// The most sources a gas has.
#define MVFX_MAX_GAS_SOURCES 8

// The most substeps a gas takes in a step.
#define MVFX_MAX_GAS_SUBSTEPS 8

// The most V cycles a gas's pressure solve takes a substep.
#define MVFX_MAX_GAS_CYCLES 16

// The most a surface's kernel reaches, in its particles' radii.
#define MVFX_MAX_SURFACE_REACH 8.0f

// The most smoothing passes a surface takes.
#define MVFX_MAX_SURFACE_SMOOTHING 8

    // A gas box's walls, as bits: each closed (nothing passes it, the gas
    // slides along it) or, its bit clear, open (the gas flows in and out
    // freely, the pressure outside 0).
    typedef uint8_t mvfxGasWalls;
    enum
    {
        mvfx_gasWallMinX = 1,
        mvfx_gasWallMaxX = 2,
        mvfx_gasWallMinY = 4,
        mvfx_gasWallMaxY = 8,
        mvfx_gasWallMinZ = 16,
        mvfx_gasWallMaxZ = 32,
    };

    // What a gas is.
    typedef uint8_t mvfxGasKind;
    enum
    {
        // Smoke and fire: sources, advected scalars, buoyancy, combustion.
        mvfx_gasSmoke = 0,
        // A liquid's grid: the particles of the one emitter whose liquid
        // names it (mvfxLiquidDef.gas, solver mvfx_liquidGrid) give it its
        // velocity each step (APIC: Jiang et al., "The Affine
        // Particle-In-Cell Method", SIGGRAPH 2015); cells holding a
        // particle are liquid, the others air at pressure 0; the projected
        // velocity, extended two cells into the air, goes back to the
        // particles with each one's affine matrix. Its density is the share
        // of each cell the particles fill (each a cube, or a square, of
        // the liquid's spacing), its other scalars 0. It has no sources and
        // no forces of its own (the particles carry gravity and forces) and
        // one substep; its advection and decays are not read.
        mvfx_gasLiquid = 1,
        // A surface: its one field, mvfx_gasDistance, is the signed
        // distance to the surface of one emitter's particles
        // (mvfxGasDef.surface), negative inside, made anew each step (Zhu
        // and Bridson, "Animating Sand as a Fluid", SIGGRAPH 2005, with
        // Solenthaler, Schlafli and Pajarola's correction, CAVW 2007). Any
        // emitter serves: a liquid of either solver, or plain particles.
        // It has no sources, forces or obstacles, one substep, and no
        // emitter splats into it or follows it.
        mvfx_gasSurface = 2,
    };

    // Where a gas's box is.
    typedef uint8_t mvfxGasSpace;
    enum
    {
        // In the instance's frame: the box moves with the instance, and its
        // gas with it (a torch's flame).
        mvfx_gasLocal = 0,
        // Where the instance was created: the box stays, and the gas is
        // left behind as the instance moves (smoke).
        mvfx_gasWorld = 1,
    };

    // A gas's fields.
    typedef uint8_t mvfxGasField;
    enum
    {
        mvfx_gasDensity = 0,
        mvfx_gasTemperature = 1,
        mvfx_gasFuel = 2,
        // The velocity's components along the box's axes, in lengths a
        // second.
        mvfx_gasVelocityX = 3,
        mvfx_gasVelocityY = 4,
        mvfx_gasVelocityZ = 5,
        // A surface gas's signed distance, in the box's lengths; its one
        // field, read from no other gas.
        mvfx_gasDistance = 6,
    };

    // How a gas carries its fields.
    typedef uint8_t mvfxGasAdvection;
    enum
    {
        // Each point traced back over the substep and the field sampled
        // there: stable at any step, smoothing small detail.
        mvfx_gasSemiLagrangian = 0,
        // MacCormack's correction (Selle et al. 2008): the trace forward
        // from the result estimates its error, half of which is taken back,
        // falling back to the plain sample where that leaves the values it
        // was sampled from. Sharper, for three traces instead of one.
        mvfx_gasMacCormack = 1,
    };

    // A source's shape.
    typedef uint8_t mvfxGasShape;
    enum
    {
        mvfx_gasSphere = 0,
        mvfx_gasBox = 1,
    };

    // A source: a sphere or a box in the gas's box frame (from its
    // center, along its axes) that adds density, temperature and fuel to
    // the cells whose centers it holds, each at a rate a second, finite
    // and at most MVFX_MAX_EXTENT in size; with setsVelocity, the faces it
    // holds take its velocity, each component finite and at most
    // MVFX_MAX_EXTENT in size.
    typedef struct mvfxGasSource
    {
        mvfxGasShape shape;
        float center[3];
        // A sphere's radius, a box's half sizes, each above 0 and at most
        // MVFX_MAX_EXTENT.
        float radius;
        float halfExtents[3];
        float density;
        float temperature;
        float fuel;
        bool setsVelocity;
        float velocity[3];
    } mvfxGasSource;

    // A force (maul-vfx/effect.h).
    struct mvfxForce;

    // A surface gas's particles and field: at a point x, phi(x) = |x - m| -
    // radius f, m the mean of the particles within R = reach radius of x
    // weighed by (1 - s^2)^3 (s their distance over R), and f 1 where the
    // largest eigenvalue E of m's Jacobian (its symmetric part's) is below
    // low, else g^3 - 3 g^2 + 3 g with g = (high - E) / (high - low),
    // held within 0 and 1: where the mean moves faster than the point (a
    // concavity, a gap between drops), the surface draws in. A cell
    // without a particle within R holds R (phi is below R elsewhere); then
    // each smoothing pass takes the mean of each cell and its neighbors
    // along the box's axes, then that mean again only where it is lower. A
    // liquid's spacing as the radius, reach 4, low 0.4 and high 2.0
    // (Solenthaler et al.'s) keep two balls, or disks in 2D, a spacing
    // apart from joining and put a jittered block's flat face about a
    // tenth of a spacing inside its place; high 2.5 sets the face nearer
    // (0.06 in) but joins such disks.
    typedef struct mvfxGasSurface
    {
        // The emitter whose particles it wraps, an index into the effect's
        // emitters.
        int32_t emitter;
        // Each particle's radius, above 0 and at most MVFX_MAX_EXTENT, in
        // the instance's lengths.
        float radius;
        // The kernel's reach over the radius, from 1 to
        // MVFX_MAX_SURFACE_REACH.
        float reach;
        // The eigenvalue's thresholds, finite and at least 0; low at or
        // above high turns the correction off (f 1 everywhere).
        float low;
        float high;
        // From 0 to MVFX_MAX_SURFACE_SMOOTHING.
        int32_t smoothing;
    } mvfxGasSurface;

    // A gas. Build it with mvfxDefaultGasDef.
    typedef struct mvfxGasDef
    {
        uint32_t cookie;
        // Its cells along X, Y and Z, each from 1 to MVFX_MAX_GAS_SIZE, at
        // most MVFX_MAX_GAS_CELLS in all; one along Z for a gas in the XY
        // plane, each at least 2 along the others.
        int32_t size[3];
        // A cell's edge, above 0 and at most MVFX_MAX_EXTENT, in the
        // instance's lengths.
        float cellSize;
        // The box's center in the instance's frame, each component finite
        // and at most MVFX_MAX_EXTENT in size.
        float center[3];
        mvfxGasSpace space;
        // Smoke (the default) or a liquid's grid.
        mvfxGasKind kind;
        // From 1 to MVFX_MAX_GAS_SUBSTEPS: each a share of the step.
        int32_t substeps;
        // Rates a second at which density, temperature, fuel and velocity
        // decay (each multiplied by exp(-rate h) over a substep of h), each
        // from 0 to MVFX_MAX_DRAG.
        float densityDecay;
        float temperatureDecay;
        float fuelDecay;
        float velocityDecay;
        // Its sources, from 0 to MVFX_MAX_GAS_SOURCES, NULL when
        // sourceCount is 0. The effect copies them.
        const mvfxGasSource* sources;
        int32_t sourceCount;
        // Its closed walls (mvfxGasWalls); the walls of a 2D gas's Z are
        // never met.
        mvfxGasWalls closedWalls;
        // The pressure projection that keeps it from compressing, each
        // substep after it is carried: a Poisson solve by geometric
        // multigrid, this many V cycles (Jacobi smoothing, a fixed count
        // for the same bits on every platform), from 0 (no projection) to
        // MVFX_MAX_GAS_CYCLES.
        int32_t pressureCycles;
        mvfxGasAdvection advection;
        // Buoyancy: each face sped by gravity times (densityWeight density -
        // heatLift temperature), its temperature the excess over the
        // surroundings (so dense smoke sinks and heat rises), each weight
        // from 0 to MVFX_MAX_EXTENT. gravity, each component finite and at
        // most MVFX_MAX_EXTENT in size, is in gravitySpace: mvfx_gasWorld
        // for the world's, turned into the box's frame as the box turns, or
        // mvfx_gasLocal for the box's own.
        float gravity[3];
        mvfxGasSpace gravitySpace;
        float densityWeight;
        float heatLift;
        // Vorticity confinement's strength a second, from 0 to
        // MVFX_MAX_EXTENT: it restores the small swirls the advection
        // smooths away.
        float vorticity;
        // Combustion: where the temperature is at least
        // ignitionTemperature (finite, at most MVFX_MAX_EXTENT in size), a
        // share 1 - exp(-burnRate h) of the fuel burns each substep of h,
        // each unit adding burnHeat to the temperature and burnSmoke to the
        // density, and growing its cell by burnExpansion of its volume
        // (the gas pushed out of a flame's front). Each rate from 0 to
        // MVFX_MAX_EXTENT.
        float ignitionTemperature;
        float burnRate;
        float burnHeat;
        float burnSmoke;
        float burnExpansion;
        // Its obstacles, from 0 to MVFX_MAX_COLLIDERS colliders (NULL when
        // obstacleCount is 0), in the world or the instance's frame, and
        // with hostObstacles the instance's colliders
        // (mvfxInstance_SetColliders) too. A cell whose center is on a
        // collider's wrong side (as a particle would be) is solid: it holds
        // nothing, and the faces beside it take the obstacle's velocity
        // across them, the obstacle's motion since the step before, and let
        // nothing through. The effect copies them and holds their grids.
        const mvfxCollider* obstacles;
        int32_t obstacleCount;
        bool hostObstacles;
        // The forces on it, from 0 to MVFX_MAX_FORCES (NULL when forceCount
        // is 0), each as on an emitter's particles, in the world or the
        // instance's frame: taken at each face, its acceleration across the
        // face, a wind's or a velocity field's pull exact over a substep.
        // An attractor's kill radius does nothing to a gas. The effect
        // copies them and holds their grids.
        const struct mvfxForce* forces;
        int32_t forceCount;
        // A surface gas's particles and field; not read for another kind.
        mvfxGasSurface surface;
    } mvfxGasDef;

// The cookie of a gas def, which its default sets and mvfxCreateEffect
// checks.
#define MVFX_GAS_DEF_COOKIE 0x6D766761u

    /// Returns the default gas def: a 2D gas of 64 by 64 cells of 0.1, in
    /// local space, one substep, nothing decaying, no sources, its walls
    /// open, 4 V cycles of pressure, semi-Lagrangian; gravity (0, -9.81, 0)
    /// in the world, with no buoyancy, no confinement and inert fuel; its
    /// surface of emitter 0's particles of radius 0.1, reach 4, thresholds
    /// 0.4 and 2.0 and one smoothing pass.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_API mvfxGasDef mvfxDefaultGasDef(void);

#ifdef __cplusplus
}
#endif

#endif
