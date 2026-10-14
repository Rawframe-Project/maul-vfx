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
    0xB5E4D4C6B816E46Du, 0xA9DD9FE586CE4191u, 0xEC5BE6E8A14BCE3Bu, 0x13B05CDD75EA30A8u,
    0x7C761C60CB5845D8u, 0x2AEBFEFE22307513u, 0x1B70DC3481D6035Au, 0xB90163627B35B5C4u,
    0x6A2B98EE345D482Au, 0x14498C1096409E6Eu, 0xD601A0AE3FB96D95u, 0x704002F8AC15ABBAu,
    0xE6749A0C3CCAF23Bu, 0x100E3FD8032AA792u, 0x1EEE7EABCC6D993Bu, 0xDE8AFB74E7F97AECu,
    0xBA36F6F1AAC09914u, 0xFE046BEB5017AB42u, 0xB25433BCDC8D4751u, 0x45517EF91EC0680Fu,
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
