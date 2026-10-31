// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Plexus links on a real device (lavapipe on CI) beside the CPU path,
// compared after every step (rhi_harness.h): places and velocities within
// the tolerance, and the links word for word with the step's drops, the
// CPU path's found again from the GPU's places at the step's end (links
// are decided on places, which the paths hold within the tolerance).
// Covered: links under no cap, under a particle's most, under the
// emitter's most, in the world's space under a moving instance, after
// contacts move the places and after kill rules thin the pool, and the
// drops started again by a clear. Then their output, the CPU path's from
// the GPU's places: points with the list and the counts (faded, and into
// buffers too small for all), and strips.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite emitter whose particles link within a distance, under caps.
static mvfxEmitterDef Linked(int32_t maxLinks, int32_t maxPerParticle)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.plexus = (mvfxPlexusDef){.enable = true,
                                     .distance = 0.15f,
                                     .maxLinks = maxLinks,
                                     .maxPerParticle = maxPerParticle,
                                     .width = 0.5f};
    return emitter;
}

static void TestCaps(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Linked(MVFX_MAX_PLEXUS_LINKS, MVFX_MAX_NEIGHBORS);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .leastLinks = 100},
            "links under no cap");
    emitter = Linked(MVFX_MAX_PLEXUS_LINKS, 2);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .leastLinks = 50, .leastDropped = 50},
            "links under a particle's most of two");
    emitter = Linked(40, 8);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .leastLinks = 40, .leastDropped = 50},
            "links under the emitter's most of forty");
}

static void TestOthers(Gpu* gpu, mvfxGpu* kernels)
{
    // Spread along the instance's path: a longer distance.
    mvfxEmitterDef emitter = Linked(MVFX_MAX_PLEXUS_LINKS, 16);
    emitter.space = mvfx_spaceWorld;
    emitter.plexus.distance = 0.5f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .leastLinks = 20},
            "links in the world, the instance moving");
    // Contacts move the places the links are found from.
    emitter = Linked(MVFX_MAX_PLEXUS_LINKS, 16);
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.04f, 0.04f, 0.04f}, .max = {0.1f, 0.1f, 0.1f}};
    emitter.contacts =
        (mvfxContactDef){.enable = true, .radiusScale = 0.5f, .iterations = 2, .maxContacts = 16};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .tolerance = 1e-3f,
                   .adopting = true,
                   .leastLinks = 50},
            "links after contacts");
    // Kill rules thin the pool the links are found in.
    static const mvfxKillRule rule = {.kind = mvfx_killSlower, .speed = 1.0f};
    emitter = Linked(MVFX_MAX_PLEXUS_LINKS, 16);
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .killing = true, .leastLinks = 20},
            "links after a kill rule");
    // A clear at half time starts the drops again on both paths.
    emitter = Linked(MVFX_MAX_PLEXUS_LINKS, 2);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .clearing = true,
                   .leastLinks = 50,
                   .leastDropped = 50},
            "links' drops after a clear");
}

// A point of the ribbon fields and its colour, corner and serial.
static const mvfxLayoutField s_fields[11] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 24},
    {.field = mvfx_recordLength, .format = mvfx_formatFloat32, .components = 1, .offset = 28},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 32},
    {.field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 1, .offset = 36},
    {.field = mvfx_recordSide, .format = mvfx_formatFloat32, .components = 3, .offset = 40},
    {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 3, .offset = 52},
    {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 64},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 80},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 88}};

static void TestOutput(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = s_fields;
    layout.fieldCount = 11;
    layout.stride = 92;
    // Spread along the moving instance's path: a longer distance.
    mvfxEmitterDef emitter = Linked(MVFX_MAX_PLEXUS_LINKS, 4);
    emitter.plexus.distance = 0.6f;
    emitter.plexus.fade = true;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = 2 * CAPACITY,
                   .ribbons = true,
                   .plexus = true,
                   .ribbonCapacity = 2048,
                   .leastLinks = 50},
            "links' points, faded");
    // Room for some of the links alone: those that fit whole, in order.
    emitter.plexus.fade = false;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .layout = &layout,
                   .recordCapacity = 61,
                   .ribbons = true,
                   .plexus = true,
                   .ribbonCapacity = 2048,
                   .leastLinks = 50},
            "links' points, the points too few");
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = 4 * CAPACITY,
                   .ribbons = true,
                   .strips = true,
                   .plexus = true,
                   .ribbonCapacity = 4000,
                   .leastLinks = 50},
            "links' strips");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestCaps(gpu, kernels);
    TestOthers(gpu, kernels);
    TestOutput(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
