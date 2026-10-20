// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_colour.h"

#include "random.h"

// The salt of a run's priority (the letters "pair").
#define PAIR_SALT 0x70616972u

// Moves v[root] down the heap of v[0, end) to its place.
static void SiftKeys(uint64_t* v, int32_t root, int32_t end)
{
    for (int32_t child = 2 * root + 1; child < end; child = 2 * root + 1)
    {
        child += child + 1 < end && v[child + 1] > v[child] ? 1 : 0;
        if (v[root] >= v[child])
        {
            return;
        }
        uint64_t swap = v[root];
        v[root] = v[child];
        v[child] = swap;
        root = child;
    }
}

// Sorts n keys ascending by heapsort.
static void SortKeys(uint64_t* v, int32_t n)
{
    for (int32_t start = n / 2 - 1; start >= 0; --start)
    {
        SiftKeys(v, start, n);
    }
    for (int32_t end = n - 1; end > 0; --end)
    {
        uint64_t top = v[0];
        v[0] = v[end];
        v[end] = top;
        SiftKeys(v, 0, end);
    }
}

// The runs: each pair's contacts in a row of the pool; their count.
static int32_t FindRuns(mvfxEmitterState* state)
{
    const mvfxPairContact* pairs = state->rigidPairs;
    int32_t runs = 0;
    for (int32_t i = 0; i < state->rigidPairCount; ++i)
    {
        if (i == 0 || pairs[i].a != pairs[i - 1].a || pairs[i].b != pairs[i - 1].b)
        {
            state->rigidRunStart[runs++] = i;
        }
    }
    return runs;
}

// Each body's runs, in run order: where its list starts (count + 1
// starts), then the lists.
static void ListBodyRuns(mvfxEmitterState* state, int32_t count, int32_t runs)
{
    int32_t* at = state->rigidBodyRunsAt;
    for (int32_t i = 0; i <= count; ++i)
    {
        at[i] = 0;
    }
    for (int32_t r = 0; r < runs; ++r)
    {
        const mvfxPairContact* c = &state->rigidPairs[state->rigidRunStart[r]];
        at[c->a + 1] += 1;
        at[c->b + 1] += 1;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        at[i + 1] += at[i];
    }
    // Filled in run order, each start a cursor that ends at the next
    // list's start; then the starts moved back by one.
    for (int32_t r = 0; r < runs; ++r)
    {
        const mvfxPairContact* c = &state->rigidPairs[state->rigidRunStart[r]];
        state->rigidBodyRuns[at[c->a]++] = r;
        state->rigidBodyRuns[at[c->b]++] = r;
    }
    for (int32_t i = count; i > 0; --i)
    {
        at[i] = at[i - 1];
    }
    at[0] = 0;
}

// The least colour none of body's coloured runs has, run r's stamp
// marking those taken.
static void StampBody(mvfxEmitterState* state, int32_t body, int32_t r)
{
    for (int32_t j = state->rigidBodyRunsAt[body]; j < state->rigidBodyRunsAt[body + 1]; ++j)
    {
        int32_t colour = state->rigidRunColours[state->rigidBodyRuns[j]];
        if (colour >= 0)
        {
            state->rigidStamps[colour] = r + 1;
        }
    }
}

void mvfxColourBodyPairs(mvfxEmitterState* state, int32_t count)
{
    int32_t runs = FindRuns(state);
    state->rigidRunCount = runs;
    state->rigidColourCount = 0;
    if (runs == 0)
    {
        return;
    }
    ListBodyRuns(state, count, runs);
    // Priorities: the hash above, the lower index first on a tie.
    for (int32_t r = 0; r < runs; ++r)
    {
        const mvfxPairContact* c = &state->rigidPairs[state->rigidRunStart[r]];
        mvfxWords4 h = mvfxPcg4d((mvfxWords4){(uint32_t)c->a, (uint32_t)c->b, PAIR_SALT, 0u});
        state->rigidRunKeys[r] = (uint64_t)h.x << 32u | (uint64_t)(0xFFFFFFFFu - (uint32_t)r);
        state->rigidRunColours[r] = -1;
        state->rigidStamps[r] = 0;
    }
    state->rigidStamps[runs] = 0;
    SortKeys(state->rigidRunKeys, runs);
    int32_t colours = 0;
    for (int32_t k = runs - 1; k >= 0; --k)
    {
        int32_t r = (int32_t)(0xFFFFFFFFu - (uint32_t)state->rigidRunKeys[k]);
        const mvfxPairContact* c = &state->rigidPairs[state->rigidRunStart[r]];
        StampBody(state, c->a, r);
        StampBody(state, c->b, r);
        int32_t colour = 0;
        while (state->rigidStamps[colour] == r + 1)
        {
            ++colour;
        }
        state->rigidRunColours[r] = colour;
        colours = colour + 1 > colours ? colour + 1 : colours;
    }
    state->rigidColourCount = colours;
    // The passes' order: by colour, then pool order (a counting sort in
    // the stamps, each colour's first place).
    for (int32_t k = 0; k <= colours; ++k)
    {
        state->rigidStamps[k] = 0;
    }
    for (int32_t r = 0; r < runs; ++r)
    {
        state->rigidStamps[state->rigidRunColours[r] + 1] += 1;
    }
    for (int32_t k = 0; k < colours; ++k)
    {
        state->rigidStamps[k + 1] += state->rigidStamps[k];
    }
    for (int32_t r = 0; r < runs; ++r)
    {
        state->rigidRunOrder[state->rigidStamps[state->rigidRunColours[r]]++] = r;
    }
}
