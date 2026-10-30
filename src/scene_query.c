// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "scene_query.h"

#include "context.h"
#include "direction.h"
#include "fpenv.h"
#include "instance_api.h"
#include "space.h"

#include <math.h>

void mvfxWriteSceneQueries(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    state->queryCount = 0;
    if (!compiled->collision.enable || !compiled->collision.enableSceneQueries)
    {
        return;
    }
    state->queryMap = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &instance->to);
    float lookahead = compiled->collision.lookahead;
    const uint32_t* serial = mvfxAttributeArray(instance, emitter, mvfx_attributeSerial);
    const float* p[3];
    const float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    for (int32_t i = 0; i < state->count; ++i)
    {
        float travel[3] = {v[0][i] * lookahead, v[1][i] * lookahead, v[2][i] * lookahead};
        if (travel[0] == 0.0f && travel[1] == 0.0f && travel[2] == 0.0f)
        {
            continue;
        }
        if (state->queryCount == compiled->collision.queryCapacity)
        {
            state->queriesDropped = mvfxSaturatingAdd(state->queriesDropped, 1);
            continue;
        }
        mvfxSceneQuery* query = &state->queries[state->queryCount++];
        const float place[3] = {p[0][i], p[1][i], p[2][i]};
        *query = (mvfxSceneQuery){.particle = i, .serial = serial[i]};
        mvfxUnmapPoint(&state->queryMap, place, query->origin);
        mvfxUnmapVector(&state->queryMap, travel, query->segment);
    }
}

mvfxResult mvfxInstance_GetSceneQueries(mvfxContext* context, mvfxInstanceId instance,
                                        int32_t emitter, const mvfxSceneQuery** queriesOut,
                                        int32_t* countOut)
{
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    if (queriesOut != nullptr)
    {
        *queriesOut = nullptr;
    }
    if (countOut != nullptr)
    {
        *countOut = 0;
    }
    if (context == nullptr || queriesOut == nullptr || countOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitter(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    const mvfxEmitterState* state = &live->emitters[emitter];
    *queriesOut = state->queryCount > 0 ? state->queries : nullptr;
    *countOut = state->queryCount;
    return mvfx_success;
}

static bool IsAnswerValid(const mvfxSceneAnswer* answer)
{
    if (!answer->hit)
    {
        return true;
    }
    double square = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (!(fabs(answer->point[k]) <= 0x1p52) || !isfinite(answer->normal[k]))
        {
            return false;
        }
        square += (double)answer->normal[k] * (double)answer->normal[k];
    }
    return square > 0.0 && isfinite(square);
}

// Gives particle i the plane of an answer, mapped into its space, or none.
static void TakeAnswer(mvfxInstance* instance, int32_t emitter, int32_t i,
                       const mvfxSceneAnswer* answer)
{
    const mvfxSpaceMap* map = &instance->emitters[emitter].queryMap;
    float point[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 0.0f};
    if (answer->hit)
    {
        mvfxMapPointDouble(map, answer->point, point);
        mvfxMapVector(map, answer->normal, 1.0f, normal);
        (void)mvfxNormalize(normal);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        float* points =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeScenePointX + k));
        float* normals =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeSceneNormalX + k));
        points[i] = point[k];
        normals[i] = normal[k];
    }
}

static MVFX_NOINLINE mvfxResult mvfxInstance_AnswerSceneQueriesWork(mvfxContext* context,
                                                                    mvfxInstanceId instance,
                                                                    int32_t emitter,
                                                                    const mvfxSceneAnswer* answers,
                                                                    int32_t count)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitter(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    bool valid = count == state->queryCount && (count == 0 || answers != nullptr);
    for (int32_t a = 0; valid && a < count; ++a)
    {
        valid = IsAnswerValid(&answers[a]);
    }
    if (!valid)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    for (int32_t a = 0; a < count; ++a)
    {
        TakeAnswer(live, emitter, state->queries[a].particle, &answers[a]);
    }
    state->queryCount = 0;
    return mvfx_success;
}

mvfxResult mvfxInstance_AnswerSceneQueries(mvfxContext* context, mvfxInstanceId instance,
                                           int32_t emitter, const mvfxSceneAnswer* answers,
                                           int32_t count)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result =
        mvfxInstance_AnswerSceneQueriesWork(context, instance, emitter, answers, count);
    mvfxFloatEnvLeave(env);
    return result;
}
