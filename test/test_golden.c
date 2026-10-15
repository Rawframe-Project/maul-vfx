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
    0xC1C672170FCCB607u, 0xDFD7362D13B18D21u, 0x1E0BD35E04EC5CD2u, 0x4FF4FA6483B07F6Cu,
    0xECA95180C6A64701u, 0xC9331FE0E233B1F6u, 0x84CB26464CEC97C7u, 0xDAF5CEFFAD822DD1u,
    0x0E53F6F773EE86EAu, 0x503F0C5DFD197EC7u, 0x59A081CD29E6B1EDu, 0x6C8E75221DBB3E7Cu,
    0xAD0C63F094E15F97u, 0x8764DF8C876C1465u, 0xDFDEEC32E25C9D8Cu, 0x17AF066C270D387Au,
    0x590716FC7A08D5F8u, 0xEBF9046B81A238F8u, 0x6434B032B226000Au, 0xE4F32679276C8BCFu,
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
        MoveScene(context, step);
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
