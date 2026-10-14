// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene's state hashed after every step against values committed here
// (record mvfx-0001: the same bits on every platform). CI runs it on every
// platform of its matrix; one hash differing anywhere fails. A change meant
// to change results updates the values and says so in the changelog. Run
// it with --print to print the values this build computes.

#include "vfx_scene.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const uint64_t s_golden[SCENE_STEP_COUNT] = {
    0x70966FEDE2D13D2Bu, 0xA512C9D2F7064FCBu, 0xDE7A0F3AD8906CCCu, 0x9790FE81D96C7388u,
    0x0B5ED2117282330Fu, 0xB4FE7CBB20A66FB1u, 0x7278043455678E02u, 0x323839B8D5805847u,
    0xA3D034B739884680u, 0xC01D1E5060E40E5Eu, 0x14ADF79BD848FF72u, 0x5333B517D106550Eu,
    0xDBFC4AC39E0871D6u, 0x1A64C55733DBCBE6u, 0xED6CB28D62955EA9u, 0x2F56E8B0BA4D87F3u,
    0xD3B59CAB41EADC0Fu, 0x741BCD21A6A7420Au, 0x2509722F39D3C81Du, 0xAF49EDDBD71000DEu,
};

int main(int argc, char** argv)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    BuildScene(context);
    bool print = argc > 1 && strcmp(argv[1], "--print") == 0;
    for (int32_t step = 0; step < SCENE_STEP_COUNT; ++step)
    {
        CHECK(mvfxContext_StepTicks(context, s_sceneSteps[step]) == mvfx_success, "step");
        uint64_t hash = HashScene(context);
        if (print)
        {
            printf("    0x%016" PRIX64 "u,\n", hash);
        }
        CHECK(hash == s_golden[step], "the state matches the committed hash");
    }
    mvfxDestroyContext(context);
    return s_failures == 0 ? 0 : 1;
}
