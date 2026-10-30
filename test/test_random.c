// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Random numbers where a host meets them: mvfxDrawModuleRandom's words
// against pcg4d from Jarzynski and Olano's listing, which is itself
// checked against the vectors tools/pcg4d_vectors.py prints, over many
// serials, streams and steps; and the uniform values a range draws for
// each particle, read from its record: multiples of 2^-24 below 1, with
// the mean, the variance and the bins of a uniform distribution.

#define PEEK_MOST 65536

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Words
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
} Words;

// pcg4d as the listing gives it: a linear congruential step on each word,
// a round in which each word gains the product of two others (reading the
// words already updated), a xor of each word's high half into its low
// half, and a second round.
static Words Round(Words v)
{
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

static Words Pcg4d(Words v)
{
    v.x = v.x * 1664525u + 1013904223u;
    v.y = v.y * 1664525u + 1013904223u;
    v.z = v.z * 1664525u + 1013904223u;
    v.w = v.w * 1664525u + 1013904223u;
    v = Round(v);
    v.x ^= v.x >> 16u;
    v.y ^= v.y >> 16u;
    v.z ^= v.z >> 16u;
    v.w ^= v.w >> 16u;
    return Round(v);
}

// Inputs and outputs printed by tools/pcg4d_vectors.py.
static const struct
{
    Words input;
    Words output;
} s_vectors[] = {
    {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
     {0x0F02F829u, 0x2D568769u, 0x32B0C43Bu, 0xD32548EAu}},
    {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u},
     {0x3622CD16u, 0xF11471D8u, 0xE1109B3Fu, 0x02B94C2Fu}},
    {{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
     {0x974ED892u, 0xC015DC67u, 0x9F955760u, 0xA1BBA208u}},
    {{0x12345678u, 0x9ABCDEF0u, 0x0F1E2D3Cu, 0x4B5A6978u},
     {0xE6333C67u, 0x765CF160u, 0x941B5A18u, 0xC1785759u}},
    {{0x0000002Au, 0x00000007u, 0x000F4240u, 0x00000003u},
     {0x169B6C0Bu, 0xBB313A1Au, 0x8CB5317Cu, 0x81B62DCBu}},
    {{0x80000000u, 0x00000001u, 0x7FFFFFFFu, 0xDEADBEEFu},
     {0xAF8ED48Fu, 0x32614288u, 0x065A27FDu, 0x47B54EF9u}},
};

static void TestTheListingMatchesTheVectors(void)
{
    bool same = true;
    for (size_t i = 0; i < sizeof s_vectors / sizeof s_vectors[0]; ++i)
    {
        Words out = Pcg4d(s_vectors[i].input);
        same = same && out.x == s_vectors[i].output.x && out.y == s_vectors[i].output.y &&
               out.z == s_vectors[i].output.z && out.w == s_vectors[i].output.w;
    }
    CHECK(same, "the listing gives the vectors");
}

// What the module saw: its calls, the words that differed from the
// listing's, the seeds it was given and whether every input bit reached
// every output word.
typedef struct Seen
{
    int32_t calls;
    int32_t differing;
    uint32_t seeds[2];
    int32_t unmoved;
} Seen;

// Draws for serials and streams whose bits span every position, at this
// step's end tick, and compares each with the listing's words of the
// module's seed, the serial, the stream and the tick.
static void Draw(const mvfxModuleCall* call, void* user)
{
    Seen* seen = user;
    seen->seeds[seen->calls % 2] = call->seed;
    seen->calls += 1;
    for (uint32_t k = 0; k < 64; ++k)
    {
        uint32_t serial = k < 32 ? 1u << k : (k - 32u) * 0x9E3779B9u;
        uint32_t stream = k < 32 ? ~(1u << k) : k;
        uint32_t out[4] = {0};
        mvfxDrawModuleRandom(call, serial, stream, out);
        Words want = Pcg4d((Words){call->seed, serial, stream, (uint32_t)call->endTick});
        seen->differing +=
            out[0] != want.x || out[1] != want.y || out[2] != want.z || out[3] != want.w ? 1 : 0;
        // One bit of the serial, then of the stream, changes every word.
        uint32_t moved[4] = {0};
        mvfxDrawModuleRandom(call, serial ^ 1u, stream, moved);
        uint32_t streamed[4] = {0};
        mvfxDrawModuleRandom(call, serial, stream ^ 1u, streamed);
        for (int32_t c = 0; c < 4; ++c)
        {
            seen->unmoved += moved[c] == out[c] || streamed[c] == out[c] ? 1 : 0;
        }
    }
}

static void TestModuleWords(void)
{
    // Two emitters, each running the module, stepped by uneven ticks so the
    // end tick takes many values (and passes 2^32 ticks, where only its
    // low 32 bits reach the hash).
    static Seen seen[2];
    mvfxHostModule modules[2] = {{mvfx_stageAfterMotion, Draw, &seen[0]},
                                 {mvfx_stageAfterMotion, Draw, &seen[1]}};
    mvfxEmitterDef emitters[2];
    for (int32_t e = 0; e < 2; ++e)
    {
        emitters[e] = mvfxDefaultEmitterDef();
        emitters[e].hostModules = &modules[e];
        emitters[e].hostModuleCount = 1;
    }
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 0xC0FFEEu;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    for (int32_t s = 0; s < 40; ++s)
    {
        CHECK(mvfxContext_StepTicks(context, 1 + 7919 * s) == mvfx_success, "stepped");
    }
    // Steps of a second, the longest, until the clock passes 2^32 ticks.
    int32_t steps = 40;
    for (int64_t clock = 0; clock <= (1LL << 32); clock += MVFX_TICKS_PER_SECOND)
    {
        CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND) == mvfx_success, "a second");
        steps += 1;
    }
    mvfxDestroyContext(context);
    CHECK(seen[0].calls == steps && seen[1].calls == steps, "the module ran each step");
    CHECK(seen[0].differing == 0 && seen[1].differing == 0, "the listing's words");
    CHECK(seen[0].unmoved == 0 && seen[1].unmoved == 0, "every coordinate reaches every word");
    CHECK(seen[0].seeds[0] == seen[0].seeds[1] && seen[0].seeds[0] != seen[1].seeds[0],
          "a seed for each module, the same each step");
}

// A custom attribute drawn uniformly from [0, 1) for each particle.
static TestParticle s_records[PEEK_MOST];

static void TestUniformDraws(void)
{
    // The values a range draws are min plus (max - min) times a uniform
    // value; from [0, 1) they are that value. Over 2^16 particles: every
    // one a multiple of 2^-24 below 1, and their mean, variance and counts
    // in 16 bins those of a uniform distribution within six standard
    // deviations of the estimate.
    enum
    {
        COUNT = PEEK_MOST,
        BINS = 16
    };
    mvfxCustomAttribute custom = {
        .components = 4,
        .initial = {.kind = mvfx_sourceRange, .min = {0, 0, 0, 0}, .max = {1, 1, 1, 1}}};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = COUNT;
    emitter.rate = 0.0f;
    mvfxBurst burst = {.minCount = COUNT, .maxCount = COUNT, .cycles = 1, .probability = 1.0f};
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.custom = &custom;
    emitter.customCount = 1;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 0xC0FFEEu;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "stepped");
    int32_t count = ReadParticles(context, instance, 0, s_records, COUNT);
    mvfxDestroyContext(context);
    CHECK(count == COUNT, "every particle born");
    double sum = 0;
    double squares = 0;
    int32_t bins[BINS] = {0};
    bool grid = true;
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            float value = s_records[i].custom[c];
            double steps = (double)value * 0x1p24;
            grid = grid && value >= 0.0f && value < 1.0f && steps == floor(steps);
            if (c == 0)
            {
                sum += (double)value;
                squares += (double)value * (double)value;
                bins[(int32_t)(value * BINS)] += 1;
            }
        }
    }
    CHECK(grid, "multiples of 2^-24 in [0, 1)");
    double mean = sum / COUNT;
    double variance = squares / COUNT - mean * mean;
    CHECK(fabs(mean - 0.5) < 6.0 * sqrt(1.0 / 12.0 / COUNT), "the mean of a uniform value");
    CHECK(fabs(variance - 1.0 / 12.0) < 6.0 * sqrt(1.0 / 180.0 / COUNT), "its variance");
    double expected = (double)COUNT / BINS;
    double chiSquare = 0;
    for (int32_t i = 0; i < BINS; ++i)
    {
        chiSquare += (bins[i] - expected) * (bins[i] - expected) / expected;
    }
    // 15 degrees of freedom: 37.7 is past the 0.999 quantile.
    CHECK(chiSquare < 37.7, "even bins");
}

int main(void)
{
    TestTheListingMatchesTheVectors();
    TestModuleWords();
    TestUniformDraws();
    return s_failures == 0 ? 0 : 1;
}
