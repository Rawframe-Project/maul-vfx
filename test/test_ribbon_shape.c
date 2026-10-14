// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbon shape: segments cut into pieces along the centripetal
// Catmull-Rom spline (on a line, through a corner, at coincident points),
// fewer pieces by a bend angle; values between particles interpolated,
// counts from the nearer; the texture coordinate stretched, per segment,
// by a distance stored at birth, scrolled; facing the camera or a normal
// turned with a local emitter; the def's and the layout's checks.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
} Scene;

// A point: position, side, face normal, along, length, colour, serial.
typedef struct Point
{
    float position[3];
    float side[3];
    float face[3];
    float along;
    float length;
    float color[4];
    uint32_t serial;
} Point;

static const mvfxLayoutField s_fields[7] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordSide, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 3, .offset = 24},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 36},
    {.field = mvfx_recordLength, .format = mvfx_formatFloat32, .components = 1, .offset = 40},
    {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 44},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 60},
};

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Scene scene = {0};
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 7;
    layoutDef.stride = (uint32_t)sizeof(Point);
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    return scene;
}

static mvfxEmitterDef Ribbon(int32_t pieces)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(9.0f, 9.0f);
    emitter.color =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1, 1}, .max = {1, 1, 1, 1}};
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.ribbon.pieces = pieces;
    return emitter;
}

static void Spawn(Scene* scene, const double* points, const float* colors, int32_t count,
                  int64_t ticks)
{
    mvfxSpawnRequest request = {.count = count, .positions = points, .colors = colors};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "spawned");
    CHECK(mvfxContext_StepTicks(scene->context, ticks) == mvfx_success, "step");
}

typedef struct Written
{
    Point points[32];
    int64_t count;
} Written;

static Written Write(Scene* scene, const mvfxView* view)
{
    static Written written;
    static uint32_t scratch[8 * 4];
    static mvfxRibbon ribbons[2];
    memset(&written, 0, sizeof written);
    mvfxRibbonBuffers buffers = {.points = written.points,
                                 .pointCapacity = 32,
                                 .ribbons = ribbons,
                                 .ribbonCapacity = 2,
                                 .scratch = scratch,
                                 .scratchBytes = sizeof scratch};
    int64_t ribbonCount = 0;
    CHECK(mvfxInstance_WriteRibbons(scene->context, scene->instance, 0, scene->layout, view,
                                    &buffers, &written.count, &ribbonCount) == mvfx_success,
          "written");
    return written;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f;
}

static const mvfxView s_view = {.eye = {0, 0, 10}};

static void TestLine(void)
{
    // Even points on a line: the spline is the line, four pieces each.
    mvfxEmitterDef emitter = Ribbon(4);
    Scene scene = NewScene(&emitter);
    static const double points[9] = {0, 0, 0, 1, 0, 0, 2, 0, 0};
    Spawn(&scene, points, NULL, 3, 1);
    Written w = Write(&scene, &s_view);
    bool line = w.count == 9;
    for (int32_t g = 0; line && g < 9; ++g)
    {
        line = Near(w.points[g].position[0], 0.25f * (float)g) &&
               Near(w.points[g].position[1], 0.0f) && Near(w.points[g].length, 0.125f * (float)g);
    }
    CHECK(line, "nine points on the line, lengths along it");
    CHECK(w.points[2].serial == 1 && w.points[1].serial == 0 && w.points[8].serial == 2,
          "counts from the nearer particle");
    // Facing the eye above: the side is -y, the face +z.
    CHECK(Near(w.points[3].side[1], -1.0f) && Near(w.points[3].face[2], 1.0f) &&
              Near(w.points[3].side[0], 0.0f),
          "facing the camera");
    mvfxDestroyContext(scene.context);
}

static void TestCorner(void)
{
    // Through a corner: the spline passes every particle and bows off the
    // straight segments between them.
    mvfxEmitterDef emitter = Ribbon(2);
    Scene scene = NewScene(&emitter);
    static const double points[9] = {0, 0, 0, 1, 0, 0, 1, 1, 0};
    Spawn(&scene, points, NULL, 3, 1);
    Written w = Write(&scene, &s_view);
    CHECK(w.count == 5 && w.points[2].position[0] == 1.0f && w.points[2].position[1] == 0.0f &&
              w.points[4].position[1] == 1.0f,
          "through every particle");
    CHECK(w.points[1].position[1] != 0.0f && fabsf(w.points[1].position[1]) < 0.2f &&
              w.points[3].position[0] != 1.0f && fabsf(w.points[3].position[0] - 1.0f) < 0.2f,
          "bowed between them");
    mvfxDestroyContext(scene.context);
}

static void TestCoincident(void)
{
    // Two particles at one place: the line between them, nothing not
    // finite.
    mvfxEmitterDef emitter = Ribbon(2);
    Scene scene = NewScene(&emitter);
    static const double points[9] = {0, 0, 0, 0, 0, 0, 2, 0, 0};
    Spawn(&scene, points, NULL, 3, 1);
    Written w = Write(&scene, &s_view);
    bool finite = w.count == 5;
    for (int32_t g = 0; finite && g < 5; ++g)
    {
        finite = isfinite(w.points[g].position[0]) && isfinite(w.points[g].side[0]) &&
                 isfinite(w.points[g].length);
    }
    CHECK(finite && w.points[1].position[0] == 0.0f && w.points[3].position[0] == 1.0f,
          "coincident points");
    mvfxDestroyContext(scene.context);
}

static void TestBend(void)
{
    // A bend of 0.5 radians a piece: a line takes one piece a segment, a
    // right angle four (the fewest whose turns reach a quarter).
    mvfxEmitterDef emitter = Ribbon(8);
    emitter.ribbon.bendAngle = 0.5f;
    Scene scene = NewScene(&emitter);
    static const double line[9] = {0, 0, 0, 1, 0, 0, 2, 0, 0};
    Spawn(&scene, line, NULL, 3, 1);
    CHECK(Write(&scene, &s_view).count == 3, "a line, one piece a segment");
    mvfxDestroyContext(scene.context);
    scene = NewScene(&emitter);
    static const double corner[9] = {0, 0, 0, 1, 0, 0, 1, 1, 0};
    Spawn(&scene, corner, NULL, 3, 1);
    CHECK(Write(&scene, &s_view).count == 9, "a right angle, four pieces a segment");
    mvfxDestroyContext(scene.context);
    // A segment whose way in has no length takes one piece: both here.
    scene = NewScene(&emitter);
    static const double repeated[9] = {0, 0, 0, 0, 0, 0, 1, 1, 0};
    Spawn(&scene, repeated, NULL, 3, 1);
    CHECK(Write(&scene, &s_view).count == 3, "no way in, one piece a segment");
    mvfxDestroyContext(scene.context);
    // A turn back at 3 radians a piece: two pieces each, the second's
    // allowance held at a half turn.
    emitter = Ribbon(4);
    emitter.ribbon.bendAngle = 3.0f;
    scene = NewScene(&emitter);
    static const double back[9] = {0, 0, 0, 1, 0, 0, 0, 0, 0};
    Spawn(&scene, back, NULL, 3, 1);
    CHECK(Write(&scene, &s_view).count == 5, "a turn back, two pieces a segment");
    mvfxDestroyContext(scene.context);
}

static void TestBetween(void)
{
    // Colours from red to blue: halfway between, both halves.
    mvfxEmitterDef emitter = Ribbon(2);
    Scene scene = NewScene(&emitter);
    static const double points[6] = {0, 0, 0, 1, 0, 0};
    static const float colors[8] = {1, 0, 0, 1, 0, 0, 1, 0.5f};
    Spawn(&scene, points, colors, 2, 1);
    Written w = Write(&scene, &s_view);
    CHECK(w.count == 3 && Near(w.points[1].color[0], 0.5f) && Near(w.points[1].color[2], 0.5f) &&
              Near(w.points[1].color[3], 0.75f) && w.points[2].color[2] == 1.0f,
          "values between particles");
    mvfxDestroyContext(scene.context);
}

static void TestOrientation(void)
{
    // Random orientations: each point between two particles is their
    // quaternions' sum along the shorter way, renormalized.
    mvfxEmitterDef emitter = Ribbon(2);
    emitter.orientationMode = mvfx_orientationRandom;
    Scene scene = NewScene(&emitter);
    static const mvfxLayoutField field = {
        .field = mvfx_recordOrientation, .format = mvfx_formatFloat32, .components = 4};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &field;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 16;
    mvfxLayoutId layout = {0};
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &layout) == mvfx_success, "layout");
    static const double points[24] = {0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0,
                                      4, 0, 0, 5, 0, 0, 6, 0, 0, 7, 0, 0};
    Spawn(&scene, points, NULL, 8, 1);
    static float q[15][4];
    static uint32_t scratch[8 * 4];
    static mvfxRibbon ribbons[1];
    mvfxRibbonBuffers buffers = {.points = q,
                                 .pointCapacity = 15,
                                 .ribbons = ribbons,
                                 .ribbonCapacity = 1,
                                 .scratch = scratch,
                                 .scratchBytes = sizeof scratch};
    int64_t count = 0;
    int64_t ribbonCount = 0;
    CHECK(mvfxInstance_WriteRibbons(scene.context, scene.instance, 0, layout, &s_view, &buffers,
                                    &count, &ribbonCount) == mvfx_success &&
              count == 15,
          "fifteen points");
    bool between = true;
    int32_t flipped = 0;
    for (int32_t s = 0; s < 7; ++s)
    {
        const float* a = q[2 * s];
        const float* b = q[2 * s + 2];
        float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        float sign = dot < 0.0f ? -1.0f : 1.0f;
        flipped += dot < 0.0f ? 1 : 0;
        float sum[4];
        float square = 0.0f;
        for (int32_t k = 0; k < 4; ++k)
        {
            sum[k] = a[k] + sign * b[k];
            square += sum[k] * sum[k];
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            between = between && fabsf(q[2 * s + 1][k] - sum[k] / sqrtf(square)) <= 1e-5f;
        }
    }
    CHECK(between && flipped > 0, "the shorter way, renormalized");
    mvfxDestroyContext(scene.context);
}

static void TestTexture(void)
{
    // Born at x of 0, 3 and 7 one after another: distances 0, 3 and 7,
    // over tiles of 2; per segment 0, 0.5, 1, ...; scrolled by 2 a second.
    mvfxEmitterDef emitter = Ribbon(2);
    emitter.ribbon.texture = mvfx_ribbonTileByDistance;
    emitter.ribbon.tileLength = 2.0f;
    Scene scene = NewScene(&emitter);
    static const double first[3] = {0, 0, 0};
    static const double second[3] = {3, 0, 0};
    static const double third[3] = {7, 0, 0};
    Spawn(&scene, first, NULL, 1, 1);
    Spawn(&scene, second, NULL, 1, 1);
    Spawn(&scene, third, NULL, 1, 1);
    Written w = Write(&scene, &s_view);
    CHECK(w.count == 5 && w.points[0].along == 0.0f && Near(w.points[1].along, 0.75f) &&
              Near(w.points[2].along, 1.5f) && Near(w.points[4].along, 3.5f),
          "tiled by the distance at birth");
    mvfxDestroyContext(scene.context);
    // Ribbons per batch: the second's distance starts at 0, not from the
    // first's last point.
    emitter.ribbon.ids = mvfx_ribbonPerBatch;
    scene = NewScene(&emitter);
    static const double batchA[6] = {0, 0, 0, 1, 0, 0};
    static const double batchB[6] = {10, 0, 0, 12, 0, 0};
    Spawn(&scene, batchA, NULL, 2, 1);
    Spawn(&scene, batchB, NULL, 2, 1);
    w = Write(&scene, &s_view);
    CHECK(w.count == 6 && w.points[3].along == 0.0f && Near(w.points[5].along, 1.0f),
          "a distance of its own ribbon");
    mvfxDestroyContext(scene.context);
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.ribbon.texture = mvfx_ribbonTilePerSegment;
    emitter.ribbon.scroll = 2.0f;
    scene = NewScene(&emitter);
    Spawn(&scene, first, NULL, 1, MVFX_TICKS_PER_SECOND / 4);
    Spawn(&scene, second, NULL, 1, MVFX_TICKS_PER_SECOND / 4);
    w = Write(&scene, &s_view);
    CHECK(w.count == 3 && Near(w.points[0].along, 1.0f) && Near(w.points[1].along, 1.5f) &&
              Near(w.points[2].along, 2.0f),
          "per segment, scrolled");
    mvfxDestroyContext(scene.context);
    emitter.ribbon.texture = mvfx_ribbonStretch;
    emitter.ribbon.scroll = 0.0f;
    scene = NewScene(&emitter);
    static const double line[9] = {0, 0, 0, 1, 0, 0, 3, 0, 0};
    Spawn(&scene, line, NULL, 3, 1);
    w = Write(&scene, &s_view);
    CHECK(w.count == 5 && Near(w.points[2].along, 1.0f / 3.0f) &&
              w.points[2].along == w.points[2].length,
          "stretched");
    mvfxDestroyContext(scene.context);
}

static void TestNormal(void)
{
    // Facing across (1, 0, 1) in a local emitter turned a quarter about Z:
    // the ribbon along the world's y, the normal the world's (0, 1, 1), the
    // side +x and the face +z (unturned, the side would lean to -z).
    mvfxEmitterDef emitter = Ribbon(1);
    emitter.space = mvfx_spaceLocal;
    emitter.ribbon.facing = mvfx_ribbonFaceNormal;
    emitter.ribbon.normal[0] = 1.0f;
    emitter.ribbon.normal[1] = 0.0f;
    emitter.ribbon.normal[2] = 1.0f;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {0, 0, 0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 1.0f, true) ==
              mvfx_success,
          "turned");
    static const double points[6] = {0, 0, 0, 1, 0, 0};
    Spawn(&scene, points, NULL, 2, 1);
    Written w = Write(&scene, &s_view);
    CHECK(w.count == 2 && Near(w.points[0].side[0], 1.0f) && Near(w.points[0].side[2], 0.0f) &&
              Near(w.points[0].face[2], 1.0f) && Near(w.points[0].face[0], 0.0f),
          "facing a turned normal");
    mvfxDestroyContext(scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Ribbon(1);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 12; ++rule)
    {
        emitter = Ribbon(1);
        mvfxRibbonDef* r = &emitter.ribbon;
        switch (rule)
        {
        case 0:
            r->pieces = 0;
            break;
        case 1:
            r->pieces = MVFX_MAX_RIBBON_PIECES + 1;
            break;
        case 2:
            r->bendAngle = -0.1f;
            break;
        case 3:
            r->bendAngle = 3.2f;
            break;
        case 4:
            r->texture = 3;
            break;
        case 5:
            r->tileLength = 0.0f;
            break;
        case 6:
            r->tileLength = 2e6f;
            break;
        case 7:
            r->scroll = (float)NAN;
            break;
        case 8:
            r->facing = 2;
            break;
        case 9:
            r->normal[0] = r->normal[1] = r->normal[2] = 0.0f;
            break;
        case 10:
            r->normal[1] = (float)NAN;
            break;
        default:
            r->scroll = -2e6f;
            break;
        }
        refused += mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid;
    }
    CHECK(refused == 12, "every check");
    emitter = Ribbon(MVFX_MAX_RIBBON_PIECES);
    emitter.ribbon.bendAngle = 3.14159265f;
    emitter.ribbon.facing = mvfx_ribbonFaceNormal;
    emitter.ribbon.texture = mvfx_ribbonTileByDistance;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the largest");
    emitter = Ribbon(0);
    emitter.ribbon.ids = mvfx_ribbonNone;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "ignored without ribbons");
    // A side and a face normal have 3 components.
    const mvfxLayoutField wide[2] = {
        {.field = mvfx_recordSide, .format = mvfx_formatFloat32, .components = 4},
        {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 4}};
    int32_t fields = 0;
    for (int32_t f = 0; f < 2; ++f)
    {
        mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
        layoutDef.fields = &wide[f];
        layoutDef.fieldCount = 1;
        layoutDef.stride = 16;
        mvfxLayoutId layout = {0};
        fields += mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_errorInvalid;
    }
    CHECK(fields == 2, "the fields' components");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestLine();
    TestCorner();
    TestCoincident();
    TestBend();
    TestBetween();
    TestOrientation();
    TestTexture();
    TestNormal();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
