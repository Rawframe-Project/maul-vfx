// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Why an effect is refused: the field each check names, by its file tags
// from mvfxEffectDef down, at every depth and in every part of a def, the
// same from mvfxCreateEffect and mvfxWriteEffect; refusals of no field;
// and the path written as text.

#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/file.h"

#include <math.h>
#include <string.h>

// Whether a refusal names the field of a path, as text.
static bool IsPath(const mvfxEffectRefusal* refusal, const char* path)
{
    char text[256];
    int64_t length = -1;
    return mvfxGetEffectFieldPath(refusal, text, sizeof text, &length) == mvfx_success &&
           length == (int64_t)strlen(path) && memcmp(text, path, (size_t)length) == 0;
}

// The parts a case changes: two emitters, a gas, a water surface, two
// parameters and a quality level, each valid.
typedef struct Parts
{
    mvfxEmitterDef emitters[2];
    mvfxBurst bursts[2];
    mvfxCurveKey keys[3];
    mvfxForce forces[2];
    mvfxGasDef gas;
    mvfxGasSource source;
    mvfxWaterDef water;
    float parameters[8];
    mvfxQualityLevel level;
    mvfxEffectDef effect;
} Parts;

static void MakeParts(Parts* p, bool fluids)
{
    *p = (Parts){0};
    p->emitters[0] = mvfxDefaultEmitterDef();
    p->emitters[1] = mvfxDefaultEmitterDef();
    p->gas = mvfxDefaultGasDef();
    p->water = mvfxDefaultWaterDef();
    p->level = (mvfxQualityLevel){.capacityScale = 0.5f, .rateScale = 0.5f};
    p->effect = mvfxDefaultEffectDef();
    p->effect.emitters = p->emitters;
    p->effect.emitterCount = 2;
    p->effect.parameters = p->parameters;
    p->effect.parameterCount = 2;
    p->effect.qualityLevels = &p->level;
    p->effect.qualityLevelCount = 1;
    p->effect.gases = &p->gas;
    p->effect.gasCount = fluids ? 1 : 0;
    p->effect.waters = &p->water;
    p->effect.waterCount = fluids ? 1 : 0;
}

// A refused def: the status, the path of the field and the emitter, gas
// or water surface it is in.
typedef struct Case
{
    const char* what;
    mvfxResult status;
    const char* path;
    int32_t emitter;
    int32_t gas;
    int32_t water;
} Case;

static const Case s_cases[] = {
    {"a capacity of 0", mvfx_errorInvalid, "emitters[0].capacity", 0, -1, -1},
    {"a capacity past its bound", mvfx_errorLimit, "emitters[1].capacity", 1, -1, -1},
    {"a burst's probability", mvfx_errorInvalid, "emitters[1].bursts[1].probability", 1, -1, -1},
    {"a key out of order seven steps down", mvfx_errorInvalid,
     "emitters[0].overLife.color.alpha.curve.keys[2].time", 0, -1, -1},
    {"a falloff's radius", mvfx_errorInvalid, "emitters[0].forces[1].falloff.radius", 0, -1, -1},
    {"a box's half extent", mvfx_errorInvalid, "emitters[0].shape.halfExtents[2]", 0, -1, -1},
    {"a gas source's half extent", mvfx_errorInvalid, "gases[0].sources[0].halfExtents[2]", -1, 0,
     -1},
    {"a water surface's size", mvfx_errorInvalid, "waters[0].size[1]", -1, -1, 0},
    {"a quality level's rate", mvfx_errorInvalid, "qualityLevels[0].rateScale", -1, -1, -1},
    {"a parameter not finite", mvfx_errorInvalid, "parameters[1]", -1, -1, -1},
    {"the def's cookie", mvfx_errorInvalid, "", -1, -1, -1},
    {"an emitter's cookie", mvfx_errorInvalid, "emitters[1]", 1, -1, -1},
    {"an event source of its own", mvfx_errorInvalid, "emitters[1].eventSpawn.source", 1, -1, -1},
    {"a life past its bound", mvfx_errorLimit, "emitters[0].lifetime.range.max", 0, -1, -1},
    {"a size past its bound", mvfx_errorLimit, "emitters[0].size.max[0]", 0, -1, -1},
    {"neighbors in a cycle", mvfx_errorInvalid, "emitters", -1, -1, -1},
    {"a source that does not write the kind", mvfx_errorInvalid, "emitters[1].eventSpawn.source", 1,
     -1, -1},
    {"a beam's points past the capacity", mvfx_errorInvalid, "emitters[0].capacity", 0, -1, -1},
    {"a beam's head pinned", mvfx_errorInvalid, "emitters[0].ribbon.pinHead", 0, -1, -1},
    {"a capacity not a multiple of a unit's points", mvfx_errorInvalid, "emitters[0].capacity", 0,
     -1, -1},
    {"a rigid sphere's size over life untied", mvfx_errorInvalid, "emitters[0].overLife.size", 0,
     -1, -1},
    {"a liquid gas with a source", mvfx_errorInvalid, "gases[0].sources", -1, 0, -1},
    {"a wind's strength below 0", mvfx_errorInvalid, "emitters[0].forces[0].strength.range.min", 0,
     -1, -1},
    {"a force's strength past its bound", mvfx_errorLimit,
     "emitters[0].forces[0].strength.range.max", 0, -1, -1},
    {"a wind's curve with a key below 0", mvfx_errorInvalid,
     "emitters[0].forces[0].strength.curve.keys[1].value", 0, -1, -1},
    {"a wind's cubic curve dipping below 0", mvfx_errorInvalid,
     "emitters[0].forces[0].strength.curve.keys[0].interpolation", 0, -1, -1},
};

// Makes case k's def from valid parts.
static void Break(Parts* p, int32_t k)
{
    MakeParts(p, k == 6 || k == 7 || k == 21);
    mvfxEmitterDef* e = p->emitters;
    switch (k)
    {
    case 0:
        e[0].capacity = 0;
        break;
    case 1:
        e[1].capacity = MVFX_MAX_EMITTER_CAPACITY + 1;
        break;
    case 2:
        p->bursts[0] = (mvfxBurst){.minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1};
        p->bursts[1] = (mvfxBurst){.minCount = 1, .maxCount = 1, .cycles = 1, .probability = 2};
        e[1].bursts = p->bursts;
        e[1].burstCount = 2;
        break;
    case 3:
        p->keys[0] = (mvfxCurveKey){.time = 0.0f, .value = 1.0f};
        p->keys[1] = (mvfxCurveKey){.time = 0.6f, .value = 1.0f};
        p->keys[2] = (mvfxCurveKey){.time = 0.4f, .value = 1.0f};
        e[0].overLife.color.enable = true;
        e[0].overLife.color.scaleAlpha = true;
        e[0].overLife.color.alpha =
            (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = p->keys, .keyCount = 3}};
        break;
    case 4:
        p->forces[1].falloff = (mvfxFalloff){.kind = mvfx_falloffSphere, .radius = -1.0f};
        e[0].forces = p->forces;
        e[0].forceCount = 2;
        break;
    case 5:
        e[0].shape.kind = mvfx_shapeBox;
        e[0].shape.halfExtents[0] = 1.0f;
        e[0].shape.halfExtents[1] = 1.0f;
        e[0].shape.halfExtents[2] = -1.0f;
        break;
    case 6:
        p->source = (mvfxGasSource){.shape = mvfx_gasBox, .halfExtents = {1, 1, 0}};
        p->gas.sources = &p->source;
        p->gas.sourceCount = 1;
        break;
    case 7:
        p->water.size[1] = 1;
        break;
    case 8:
        p->level.rateScale = 2.0f;
        break;
    case 9:
        p->parameters[6] = NAN;
        break;
    case 10:
        p->effect.cookie += 1;
        break;
    case 11:
        e[1].cookie += 1;
        break;
    case 12:
        e[0].events = mvfx_eventMaskBirth;
        e[0].eventCapacity = 4;
        e[1].eventSpawn = (mvfxEventSpawn){
            .enable = true, .source = 1, .minCount = 1, .maxCount = 1, .capacity = 4};
        break;
    case 13:
        e[0].lifetime.range.max = 2.0f * MVFX_MAX_LIFETIME;
        break;
    case 14:
        e[0].size = (mvfxVectorSource){
            .kind = mvfx_sourceRange, .min = {1, 1, 1}, .max = {2.0f * MVFX_MAX_VALUE, 1, 1}};
        break;
    case 15:
        for (int32_t i = 0; i < 2; ++i)
        {
            e[i].neighbors = (mvfxNeighborDef){
                .enable = true, .source = 1 - i, .radius = 1.0f, .maxNeighbors = 4};
        }
        break;
    case 16:
        e[0].events = mvfx_eventMaskBirth;
        e[0].eventCapacity = 4;
        e[1].eventSpawn = (mvfxEventSpawn){.enable = true,
                                           .source = 0,
                                           .kind = mvfx_eventDeath,
                                           .minCount = 1,
                                           .maxCount = 1,
                                           .capacity = 4};
        break;
    case 17:
    case 18:
        e[0].rate = 0.0f;
        e[0].capacity = k == 17 ? 4 : 16;
        e[0].ribbon.ids = mvfx_ribbonOne;
        e[0].ribbon.pinHead = k == 18;
        e[0].beam = (mvfxBeamDef){.enable = true, .segments = 8};
        break;
    case 19:
        e[0].unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                  .points = 3,
                                  .spacing = 0.1f,
                                  .direction = {1, 0, 0},
                                  .substeps = 1};
        break;
    case 20:
        e[0].rigid.enable = true;
        e[0].rigid.shape = mvfx_rigidSphere;
        e[0].rigid.density = 1.0f;
        e[0].rigid.substeps = 1;
        e[0].overLife.size.enable = true;
        e[0].overLife.size.perAxis = true;
        e[0].overLife.size.size[0].value = 1.0f;
        e[0].overLife.size.size[1].value = 2.0f;
        e[0].overLife.size.size[2].value = 1.0f;
        break;
    case 22:
    case 23:
        p->forces[0] =
            (mvfxForce){.kind = k == 22 ? mvfx_forceWind : mvfx_forceConstant,
                        .strength = {.kind = mvfx_sourceRange,
                                     .range = {k == 22 ? -1.0f : 0.0f,
                                               k == 22 ? 1.0f : 2.0f * MVFX_MAX_EXTENT}}};
        e[0].forces = p->forces;
        e[0].forceCount = 1;
        break;
    case 24:
    case 25:
        // Keys 1 then -1, or two of 0.5 whose tangents dip to -0.5 between.
        p->keys[0] = (mvfxCurveKey){.time = 0.0f,
                                    .value = k == 24 ? 1.0f : 0.5f,
                                    .outTangent = -4.0f,
                                    .interpolation = k == 24 ? mvfx_interpolationLinear
                                                             : mvfx_interpolationCubic};
        p->keys[1] =
            (mvfxCurveKey){.time = 1.0f, .value = k == 24 ? -1.0f : 0.5f, .inTangent = 4.0f};
        p->forces[0] = (mvfxForce){.kind = mvfx_forceWind,
                                   .strength = {.kind = mvfx_sourceCurve,
                                                .range = {1.0f, 1.0f},
                                                .curve = {.keys = p->keys, .keyCount = 2}}};
        e[0].forces = p->forces;
        e[0].forceCount = 1;
        break;
    default:
        p->source = (mvfxGasSource){.shape = mvfx_gasBox, .halfExtents = {1, 1, 1}};
        p->gas.kind = mvfx_gasLiquid;
        p->gas.sources = &p->source;
        p->gas.sourceCount = 1;
        break;
    }
}

static void TestFieldsNamed(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static Parts parts;
    MakeParts(&parts, true);
    mvfxEffectId effect = {0};
    mvfxEffectRefusal refusal = {.result = mvfx_errorLimit, .tagCount = 3, .emitter = 7};
    CHECK(mvfxCreateEffect(context, &parts.effect, &effect, &refusal) == mvfx_success &&
              refusal.result == mvfx_success && refusal.tagCount == 0 && refusal.emitter == -1 &&
              refusal.gas == -1 && refusal.water == -1 && IsPath(&refusal, ""),
          "the valid parts: success, and no field");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "destroyed");
    for (int32_t k = 0; k < (int32_t)(sizeof s_cases / sizeof s_cases[0]); ++k)
    {
        const Case* c = &s_cases[k];
        Break(&parts, k);
        mvfxEffectRefusal made = {0};
        mvfxResult result = mvfxCreateEffect(context, &parts.effect, &effect, &made);
        CHECK(result == c->status && made.result == c->status && IsPath(&made, c->path) &&
                  made.emitter == c->emitter && made.gas == c->gas && made.water == c->water,
              c->what);
        // The writer runs the same checks and names the same field.
        mvfxEffectRefusal written = {0};
        int64_t size = 0;
        mvfxFileResult file = mvfxWriteEffect(&parts.effect, NULL, 0, NULL, 0, &size, &written);
        CHECK(file.result == c->status && memcmp(&written, &made, sizeof made) == 0,
              "the same refusal from the writer");
    }
    mvfxDestroyContext(context);
}

static void TestRefusalsOfNoField(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.limits.effects = 1;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static Parts parts;
    MakeParts(&parts, false);
    mvfxEffectRefusal refusal = {0};
    CHECK(mvfxCreateEffect(context, &parts.effect, NULL, &refusal) == mvfx_errorInvalid &&
              refusal.result == mvfx_errorInvalid && refusal.tagCount == 0,
          "a NULL out-parameter");
    CHECK(mvfxCreateEffect(NULL, &parts.effect, NULL, &refusal) == mvfx_errorInvalid &&
              refusal.tagCount == 0 && refusal.emitter == -1,
          "a NULL context");
    CHECK(mvfxCreateEffect(context, NULL, NULL, &refusal) == mvfx_errorInvalid &&
              refusal.tagCount == 0,
          "a NULL def");
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &parts.effect, &effect, &refusal) == mvfx_success,
          "the one effect it holds");
    mvfxEffectId more = {1, 1};
    CHECK(mvfxCreateEffect(context, &parts.effect, &more, &refusal) == mvfx_errorCapacity &&
              refusal.result == mvfx_errorCapacity && refusal.tagCount == 0 &&
              refusal.emitter == -1 && more.index1 == 0,
          "the context full: no field");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "destroyed");
    // A mesh no longer alive: the field that names it.
    parts.emitters[1].shape.kind = mvfx_shapeMesh;
    parts.emitters[1].shape.mesh = (mvfxMeshId){5, 1};
    CHECK(mvfxCreateEffect(context, &parts.effect, &effect, &refusal) == mvfx_errorStale &&
              refusal.result == mvfx_errorStale && IsPath(&refusal, "emitters[1].shape.mesh") &&
              refusal.emitter == 1,
          "a stale mesh");
    mvfxDestroyContext(context);
}

// Writes a refusal's path into text of a capacity, prefilled with 'x';
// the status, the length given and whether every byte past the length is
// still 'x' (on success) or the whole capacity zero (on failure).
static mvfxResult PathInto(const mvfxEffectRefusal* refusal, int64_t capacity, int64_t* length,
                           bool* clean)
{
    char text[64];
    memset(text, 'x', sizeof text);
    *length = -1;
    mvfxResult result =
        mvfxGetEffectFieldPath(refusal, capacity > 0 ? text : NULL, capacity, length);
    *clean = true;
    for (int64_t i = 0; i < capacity; ++i)
    {
        bool zero = text[i] == 0;
        *clean = *clean && (result == mvfx_success ? i < *length || text[i] == 'x' : zero);
    }
    return result;
}

static void TestPathText(void)
{
    // emitters[63].bursts[11].probability: items at two depths, two
    // digits each.
    mvfxEffectRefusal refusal = {.result = mvfx_errorInvalid, .tagCount = 3};
    refusal.tags[0] = 1;
    refusal.items[0] = 63;
    refusal.tags[1] = 4;
    refusal.items[1] = 11;
    refusal.tags[2] = 6;
    refusal.items[2] = -1;
    const char* path = "emitters[63].bursts[11].probability";
    int64_t length = 0;
    bool clean = false;
    CHECK(IsPath(&refusal, path), "items at two depths");
    CHECK(PathInto(&refusal, (int64_t)strlen(path), &length, &clean) == mvfx_success &&
              length == (int64_t)strlen(path) && clean,
          "exactly the room it needs, not terminated");
    CHECK(PathInto(&refusal, 10, &length, &clean) == mvfx_errorCapacity &&
              length == (int64_t)strlen(path) && clean,
          "too little room: the length needed, the room zeroed");
    CHECK(PathInto(&refusal, 0, &length, &clean) == mvfx_errorCapacity &&
              length == (int64_t)strlen(path),
          "measured with no room");
    CHECK(mvfxGetEffectFieldPath(NULL, NULL, 0, &length) == mvfx_errorInvalid && length == 0,
          "a NULL refusal");
    char text[8] = "xxxxxxx";
    CHECK(mvfxGetEffectFieldPath(&refusal, text, sizeof text, NULL) == mvfx_errorInvalid &&
              text[0] == 0 && text[7] == 0,
          "a NULL length, the room zeroed");
    CHECK(mvfxGetEffectFieldPath(&refusal, NULL, 4, &length) == mvfx_errorInvalid &&
              mvfxGetEffectFieldPath(&refusal, text, -1, &length) == mvfx_errorInvalid,
          "no room for a capacity, a capacity below 0");
    // A whole array: its count refused.
    refusal.tagCount = 2;
    refusal.items[1] = -1;
    CHECK(IsPath(&refusal, "emitters[63].bursts"), "a whole array at the end");

    // Paths that name no field of this version give no text.
    mvfxEffectRefusal stale[7];
    for (int32_t k = 0; k < 7; ++k)
    {
        stale[k] = (mvfxEffectRefusal){.tagCount = 2, .tags = {1, 1}, .items = {0, -1}};
    }
    stale[0].tags[1] = 99;
    stale[1].items[1] = 0;
    stale[2].items[0] = -1;
    stale[3].items[0] = MVFX_MAX_EMITTERS;
    stale[4].tagCount = MVFX_MAX_FIELD_DEPTH + 1;
    stale[5].tagCount = -1;
    stale[6].tags[1] = 0;
    const char* why[7] = {"an unknown tag",
                          "an item where the step names none",
                          "no item where a step names one",
                          "an item past the cap",
                          "too many steps",
                          "a count below 0",
                          "a cookie's tag"};
    for (int32_t k = 0; k < 7; ++k)
    {
        CHECK(PathInto(&stale[k], 32, &length, &clean) == mvfx_errorInvalid && length == 0 && clean,
              why[k]);
    }
    CHECK(IsPath(&(mvfxEffectRefusal){.tagCount = 2, .tags = {1, 1}, .items = {0, -1}},
                 "emitters[0].capacity"),
          "the stale paths' valid form");
    mvfxEffectRefusal none = {.result = mvfx_errorCapacity};
    CHECK(PathInto(&none, 16, &length, &clean) == mvfx_success && length == 0 && clean,
          "a refusal of no field: empty");
}

int main(void)
{
    TestFieldsNamed();
    TestRefusalsOfNoField();
    TestPathText();
    return s_failures == 0 ? 0 : 1;
}
