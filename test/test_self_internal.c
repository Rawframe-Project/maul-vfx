// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The self-collision module's own pass on a chain folded by hand: a
// point touching two others moved by their pairs' mean. A step runs its
// substeps with the chain's constraints around the passes, so no public
// reading shows one pass's move; self-collision through the step is
// tested in test_self.c.

#include "instance.h"
#include "instance_api.h"
#include "self.h"
#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 64

typedef struct Record
{
    float position[3];
    float velocity[3];
} Record;

static const mvfxLayoutField s_fields[2] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
    Record records[MOST];
    int64_t count;
} Scene;

static Scene s_scene;

// Kicks the points of serials from first on, once, before the motion of
// the step they are first seen in.
typedef struct Kick
{
    uint32_t first;
    float velocity[3];
    bool done;
} Kick;

static void KickOnce(const mvfxModuleCall* call, void* user)
{
    Kick* kick = user;
    for (int32_t i = 0; !kick->done && i < call->particles.count; ++i)
    {
        for (int32_t k = 0; call->particles.serial[i] >= kick->first && k < 3; ++k)
        {
            call->particles.velocity[k][i] += kick->velocity[k];
        }
    }
    kick->done = kick->done || call->particles.count > 0;
}

// Two four by four sheets lying flat, 0.5 apart, no gravity, a thickness
// of 0.2, eight substeps; the second kicked.
static mvfxEmitterDef Sheets(float thickness, Kick* kick)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 32;
    emitter.hostSpawnCapacity = 2;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                                 .points = 4,
                                 .rows = 4,
                                 .spacing = 0.5f,
                                 .direction = {1, 0, 0},
                                 .downDirection = {0, 0, 1},
                                 .substeps = 8,
                                 .selfThickness = thickness,
                                 .selfContacts = 8};
    static mvfxHostModule module;
    module = (mvfxHostModule){.stage = mvfx_stageBeforeMotion, .function = KickOnce, .user = kick};
    emitter.hostModules = &module;
    emitter.hostModuleCount = 1;
    return emitter;
}

static void Open(const mvfxEmitterDef* emitter, const double* places, int32_t units)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = (uint32_t)sizeof(Record);
    CHECK(mvfxCreateLayout(s_scene.context, &layoutDef, &s_scene.layout) == mvfx_success, "layout");
    mvfxSpawnRequest request = {.count = units, .positions = places};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
}

static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
}

static void TestMean(void)
{
    // A chain of six, a thickness of 0.3, folded so that its points 4 and
    // 5 both touch its first, 0.1 across and 0.1 above and below it: one
    // pass moves the first by its two pairs' mean, half of each overlap
    // along each line (the lines' cross parts cancel), not their sum.
    static Kick none;
    none = (Kick){.first = 1000};
    mvfxEmitterDef emitter = Sheets(0.3f, &none);
    emitter.unit.kind = mvfx_unitChain;
    emitter.unit.points = 6;
    emitter.capacity = 6;
    emitter.hostSpawnCapacity = 1;
    static const double root[3] = {0, 0, 0};
    Open(&emitter, root, 1);
    Steps(1);
    mvfxInstance* live = mvfxContextInstance(s_scene.context, s_scene.instance);
    float* x[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        x[k] = mvfxAttributeArray(live, 0, (mvfxAttribute)(mvfx_attributePositionX + k));
        float* v = mvfxAttributeArray(live, 0, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        memset(v, 0, 6 * sizeof(float));
    }
    static const float folded[6][3] = {{0, 0, 0},    {0.5f, 2, 0},    {1, 2, 0},
                                       {1.5f, 2, 0}, {0.1f, 0.1f, 0}, {0.1f, -0.1f, 0}};
    for (int32_t i = 0; i < 6; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            x[k][i] = folded[i][k];
        }
    }
    mvfxFindSelfContacts(live, 0, 0.0);
    mvfxSaveSelfStart(live, 0);
    mvfxSolveSelfContacts(live, 0);
    double length = sqrt(0.02);
    double move = -0.5 * (0.3 - length) / length * 0.1;
    CHECK(fabs((double)x[0][0] - move) < 1e-5 && fabs((double)x[1][0]) < 1e-5,
          "moved by its pairs' mean");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestMean();
    return s_failures == 0 ? 0 : 1;
}
