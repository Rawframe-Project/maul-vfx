// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Collision with a depth image, against a synthetic camera's exact
// geometry: a wall met and bounced off, in every pixel format and both
// depth ranges; the thickness (a start just behind put out, one far
// behind passing); a fast particle along the view ray and one across a
// thin strip caught; a tilted plane's normal made from the depth, and a
// normal image's taken instead; the background and outside the view (and
// killed there when asked); a local emitter under a scaled instance with
// a radius; and every refusal, an image a byte short among them.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

#define SIDE       64
#define NEAR_PLANE 0.1
#define FAR_PLANE  100.0

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
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
    return scene;
}

static mvfxEmitterDef Seeing(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.collision = (mvfxCollision){.enabled = true,
                                        .restitution = 1.0f,
                                        .depthImage = true,
                                        .depthThickness = 0.5f,
                                        .depthSamples = 64};
    emitter.events = mvfx_eventMaskCollision;
    emitter.eventCapacity = 8;
    return emitter;
}

static void Place(Scene* scene, double x, double y, double z, float vx, float vy, float vz)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, vy, vz};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
}

static TestParticle s_particles[8];

// Reads the particles into s_particles; returns how many.
static int32_t Read(const Scene* scene)
{
    return ReadParticles(scene->context, scene->instance, 0, s_particles, 8);
}

// The particle at i.
static TestParticle At(const Scene* scene, int32_t i)
{
    CHECK(i < Read(scene), "the particle read");
    return s_particles[i];
}

// The collisions of the last step.
static int32_t Collisions(const Scene* scene)
{
    const mvfxEvent* events = NULL;
    int32_t count = -1;
    CHECK(mvfxInstance_GetEvents(scene->context, scene->instance, 0, &events, &count) ==
              mvfx_success,
          "events");
    int32_t collisions = 0;
    for (int32_t e = 0; e < count; ++e)
    {
        collisions += events[e].kind == mvfx_eventCollision ? 1 : 0;
    }
    return collisions;
}

static bool Near(double a, double b, double within)
{
    return fabs(a - b) <= within;
}

static void Step(Scene* scene)
{
    CHECK(mvfxContext_StepTicks(scene->context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
}

// A camera at the world's (0, 0, 10) looking down -Z, 90 degrees wide,
// depth 0 at the near plane and 1 at the far (or reversed).
static mvfxDepthImage Camera(const void* pixels, mvfxDepthFormat format, bool reversed)
{
    double a =
        reversed ? NEAR_PLANE / (FAR_PLANE - NEAR_PLANE) : FAR_PLANE / (NEAR_PLANE - FAR_PLANE);
    double b = reversed ? FAR_PLANE * NEAR_PLANE / (FAR_PLANE - NEAR_PLANE) : a * NEAR_PLANE;
    int32_t bytes = format == mvfx_depthUnorm16 ? 2 : 4;
    mvfxDepthImage image = {
        .pixels = pixels,
        .byteCount = (int64_t)SIDE * SIDE * bytes,
        .width = SIDE,
        .height = SIDE,
        .stride = SIDE * bytes,
        .format = format,
        .reversed = reversed,
        .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, (float)a, -1, 0, 0, (float)b, 0},
        .origin = {0, 0, 10},
        .rotation = {0, 0, 0, 1},
    };
    return image;
}

// The device depth of a view depth (negative, in front) under Camera.
static double Device(double viewZ, bool reversed)
{
    double a =
        reversed ? NEAR_PLANE / (FAR_PLANE - NEAR_PLANE) : FAR_PLANE / (NEAR_PLANE - FAR_PLANE);
    double b = reversed ? FAR_PLANE * NEAR_PLANE / (FAR_PLANE - NEAR_PLANE) : a * NEAR_PLANE;
    return (a * viewZ + b) / -viewZ;
}

// The world's surface seen at a pixel: the plane z = slope x (a wall
// facing the camera at slope 0), within columns [from, to); the far
// plane elsewhere.
static double SurfaceDepth(int32_t column, int32_t row, double slope, int32_t from, int32_t to,
                           bool reversed)
{
    if (column < from || column >= to)
    {
        return reversed ? 0.0 : 1.0;
    }
    double x = ((double)column + 0.5) / SIDE * 2.0 - 1.0;
    (void)row;
    // Along the ray (x, y, -1) s from (0, 0, 10): 10 - s = slope x s.
    double s = 10.0 / (1.0 + slope * x);
    return Device(-s, reversed);
}

static float s_floats[SIDE * SIDE];
static uint16_t s_shorts[SIDE * SIDE];
static uint32_t s_words[SIDE * SIDE];

static void Paint(double slope, int32_t from, int32_t to, bool reversed)
{
    for (int32_t r = 0; r < SIDE; ++r)
    {
        for (int32_t c = 0; c < SIDE; ++c)
        {
            double d = SurfaceDepth(c, r, slope, from, to, reversed);
            s_floats[r * SIDE + c] = (float)d;
            s_shorts[r * SIDE + c] = (uint16_t)floor(d * 65535.0 + 0.5);
            // A stencil in the high byte, not read.
            s_words[r * SIDE + c] = (uint32_t)floor(d * 16777215.0 + 0.5) | 0xAB000000u;
        }
    }
}

static void Show(Scene* scene, const mvfxDepthImage* image)
{
    CHECK(mvfxInstance_SetDepthImage(scene->context, scene->instance, image) == mvfx_success,
          "image set");
}

static void TestWall(void)
{
    // From z = 1 toward the wall at 2: off it at z = 0, back to 1; in each
    // format and range.
    static const void* const pixels[3] = {s_floats, s_shorts, s_words};
    static const double within[3] = {1e-3, 3e-2, 1e-3};
    for (int32_t reversed = 0; reversed < 2; ++reversed)
    {
        Paint(0.0, 0, SIDE, reversed != 0);
        for (int32_t f = 0; f < 3; ++f)
        {
            mvfxEmitterDef emitter = Seeing();
            Scene scene = NewScene(&emitter);
            mvfxDepthImage image = Camera(pixels[f], (mvfxDepthFormat)f, reversed != 0);
            Show(&scene, &image);
            Place(&scene, 0.3, -0.2, 1, 0, 0, -2);
            Step(&scene);
            CHECK(Near((double)At(&scene, 0).position[2], 1.0, within[f]) &&
                      Near((double)At(&scene, 0).velocity[2], 2.0, 1e-3),
                  "off the wall");
            mvfxDestroyContext(scene.context);
        }
    }
}

static void TestThickness(void)
{
    Paint(0.0, 0, SIDE, false);
    mvfxDepthImage image = Camera(s_floats, mvfx_depthFloat32, false);
    mvfxEmitterDef emitter = Seeing();
    Scene scene = NewScene(&emitter);
    Show(&scene, &image);
    // Just behind: put out onto it. Far behind: behind an object, passing.
    Place(&scene, 0, 0, -0.2, 0, 0, 0);
    Place(&scene, 0, 0, -2, 0, 0, 0);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], 0.0, 1e-3), "put out of the wall");
    CHECK((double)At(&scene, 1).position[2] == -2.0, "far behind, untouched");
    mvfxDestroyContext(scene.context);

    // Fast along the view ray, 6 in a step against a thickness of 0.5: the
    // end far behind, a sample within it.
    scene = NewScene(&emitter);
    Show(&scene, &image);
    Place(&scene, 0, 0, 1, 0, 0, -6);
    Step(&scene);
    CHECK(Collisions(&scene) == 1 && Near((double)At(&scene, 0).velocity[2], 6.0, 1e-3),
          "fast along the ray, caught");
    mvfxDestroyContext(scene.context);

    // Across a strip four pixels wide just behind its surface, from the
    // background to the background: caught on the strip, moved out to it.
    Paint(0.0, 30, 34, false);
    scene = NewScene(&emitter);
    Show(&scene, &image);
    Place(&scene, -5, 0, -0.1, 10, 0, 0);
    Step(&scene);
    // Moved onto the surface along its view ray, its x a little out.
    CHECK(Collisions(&scene) == 1 && Near((double)At(&scene, 0).position[2], 0.0, 1e-3) &&
              Near((double)At(&scene, 0).position[0], 5.0, 1e-2),
          "across a thin strip, caught");
    mvfxDestroyContext(scene.context);
}

static void TestNormals(void)
{
    // A plane z = x / 2: its normal (-1/2, 0, 1) made unit from the depth,
    // a fall at 2 bounces to (-1.6, 0, 1.2).
    Paint(0.5, 0, SIDE, false);
    mvfxDepthImage image = Camera(s_floats, mvfx_depthFloat32, false);
    mvfxEmitterDef emitter = Seeing();
    Scene scene = NewScene(&emitter);
    Show(&scene, &image);
    Place(&scene, 0, 0, 1, 0, 0, -2);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).velocity[0], -1.6, 2e-2) &&
              Near((double)At(&scene, 0).velocity[2], 1.2, 2e-2),
          "the normal from the depth");
    mvfxDestroyContext(scene.context);

    // A normal image saying (0, 0, 1) at every pixel is taken instead.
    static float normals[SIDE * SIDE * 3];
    for (int32_t p = 0; p < SIDE * SIDE; ++p)
    {
        normals[3 * p + 2] = 1.0f;
    }
    image.normals = normals;
    image.normalByteCount = (int64_t)sizeof normals;
    image.normalStride = SIDE * 12;
    scene = NewScene(&emitter);
    Show(&scene, &image);
    Place(&scene, 0, 0, 1, 0, 0, -2);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).velocity[0], 0.0, 1e-5) &&
              Near((double)At(&scene, 0).velocity[2], 2.0, 1e-3),
          "the normal image's");
    mvfxDestroyContext(scene.context);
}

static void TestOutside(void)
{
    // Background only: nothing met. Outside the view (x = 20 at z = 0 is
    // past the 90 degrees): nothing met, or killed when asked. Without
    // an image set, nothing either.
    Paint(0.0, 0, SIDE, false);
    mvfxDepthImage wall = Camera(s_floats, mvfx_depthFloat32, false);
    for (int32_t kill = 0; kill < 2; ++kill)
    {
        mvfxEmitterDef emitter = Seeing();
        emitter.collision.killOutsideView = kill != 0;
        Scene scene = NewScene(&emitter);
        Show(&scene, &wall);
        Place(&scene, 20, 0, 1, 0, 0, -2);
        Place(&scene, 0, 0, 1, 0, 0, -2);
        Step(&scene);
        CHECK(Read(&scene) == (kill != 0 ? 1 : 2), "outside the view");
        if (kill == 0)
        {
            CHECK(Near((double)At(&scene, 0).position[2], -1.0, 1e-4), "outside, passing");
        }
        CHECK(mvfxInstance_SetDepthImage(scene.context, scene.instance, NULL) == mvfx_success,
              "cleared");
        Place(&scene, 0, 0, 1, 0, 0, -2);
        Step(&scene);
        int32_t last = Read(&scene) - 1;
        CHECK(Near((double)At(&scene, last).position[2], -1.0, 1e-4), "no image, passing");
        mvfxDestroyContext(scene.context);
    }
    Paint(0.0, 0, 0, false);
    mvfxEmitterDef emitter = Seeing();
    Scene scene = NewScene(&emitter);
    Show(&scene, &wall);
    Place(&scene, 0, 0, 1, 0, 0, -2);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], -1.0, 1e-4), "the background, passing");
    mvfxDestroyContext(scene.context);
    // The far plane holds no surface, even for a particle whose radius
    // reaches it.
    emitter = Seeing();
    emitter.collision.radius = 0.5f;
    scene = NewScene(&emitter);
    Show(&scene, &wall);
    Place(&scene, 0, 0, -89.8, 0, 0, 0);
    Step(&scene);
    CHECK((double)At(&scene, 0).position[2] == (double)-89.8f, "no surface at the far plane");
    mvfxDestroyContext(scene.context);
}

static void TestLocal(void)
{
    // A local emitter under an instance at scale 2: from its z = 0.5 (the
    // world's 1) down 1 a second (the world's 2), its radius 0.1 (the
    // world's 0.2) keeps it to its z = 0.1, back up to 0.7 (the world's
    // 1.4).
    Paint(0.0, 0, SIDE, false);
    mvfxDepthImage image = Camera(s_floats, mvfx_depthFloat32, false);
    mvfxEmitterDef emitter = Seeing();
    emitter.space = mvfx_spaceLocal;
    emitter.collision.radius = 0.1f;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {0, 0, 0};
    static const float still[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, still, 2.0f, true) ==
              mvfx_success,
          "scaled");
    Show(&scene, &image);
    Place(&scene, 0, 0, 0.5, 0, 0, -1);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], 1.4, 2e-3), "scaled, with its radius");
    mvfxDestroyContext(scene.context);
}

// Paints the wall z = 0 in rows [from, to) only, the far plane elsewhere.
static void PaintRows(int32_t from, int32_t to)
{
    Paint(0.0, 0, SIDE, false);
    for (int32_t r = 0; r < SIDE; ++r)
    {
        if (r >= from && r < to)
        {
            continue;
        }
        for (int32_t c = 0; c < SIDE; ++c)
        {
            s_floats[r * SIDE + c] = 1.0f;
        }
    }
}

static mvfxEmitterDef SeeingStill(void)
{
    mvfxEmitterDef emitter = Seeing();
    emitter.collision.restitution = 0.0f;
    emitter.collision.friction = 1.0f;
    return emitter;
}

static void TestImageEdges(void)
{
    // A projection rolled a quarter (x from the view's y, y from its -x)
    // needs its inverse pivoted: the wall is met the same.
    Paint(0.0, 0, SIDE, false);
    mvfxDepthImage rolled = Camera(s_floats, mvfx_depthFloat32, false);
    static const float roll[8] = {0, -1, 0, 0, 1, 0, 0, 0};
    memcpy(rolled.projection, roll, sizeof roll);
    mvfxEmitterDef emitter = Seeing();
    Scene scene = NewScene(&emitter);
    Show(&scene, &rolled);
    Place(&scene, 0.3, -0.2, 1, 0, 0, -2);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], 1.0, 1e-3), "a rolled projection");
    mvfxDestroyContext(scene.context);

    // Rows from the image's top: a wall in the top half meets a particle
    // above the view's axis, not one below it.
    PaintRows(0, SIDE / 2);
    mvfxDepthImage top = Camera(s_floats, mvfx_depthFloat32, false);
    scene = NewScene(&emitter);
    Show(&scene, &top);
    Place(&scene, 0, 0.3, 1, 0, 0, -2);
    Place(&scene, 0, -0.3, 1, 0, 0, -2);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], 1.0, 1e-3) &&
              Near((double)At(&scene, 1).position[2], -1.0, 1e-4),
          "rows from the top");
    mvfxDestroyContext(scene.context);

    // At the image's very right edge (x / w = 1): its last column.
    Paint(0.0, 0, SIDE, false);
    mvfxDepthImage wall = Camera(s_floats, mvfx_depthFloat32, false);
    emitter = SeeingStill();
    scene = NewScene(&emitter);
    Show(&scene, &wall);
    Place(&scene, 10.25, 0, -0.25, 0, 0, 0);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], 0.0, 1e-3), "the right edge's column");
    mvfxDestroyContext(scene.context);

    // Beside a step down (z = 0 left of the middle, -3 right of it) the
    // normal takes the nearer neighbor: straight back.
    for (int32_t r = 0; r < SIDE; ++r)
    {
        for (int32_t c = 0; c < SIDE; ++c)
        {
            s_floats[r * SIDE + c] = (float)Device(c < SIDE / 2 ? -10.0 : -13.0, false);
        }
    }
    emitter = Seeing();
    scene = NewScene(&emitter);
    Show(&scene, &wall);
    Place(&scene, -0.15625, 0, 1, 0, 0, -2);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).velocity[0], 0.0, 1e-3) &&
              Near((double)At(&scene, 0).velocity[2], 2.0, 1e-3),
          "beside a step, the nearer neighbor");
    mvfxDestroyContext(scene.context);

    // A strip one pixel wide against a thickness of 4: samples by the
    // pixels crossed, not only by the thickness, meet it.
    Paint(0.0, SIDE / 2, SIDE / 2 + 1, false);
    emitter = Seeing();
    emitter.collision.depthThickness = 4.0f;
    scene = NewScene(&emitter);
    Show(&scene, &wall);
    Place(&scene, -5, 0, -0.1, 10, 0, 0);
    Step(&scene);
    CHECK(Collisions(&scene) == 1, "a pixel-wide strip, sampled by pixels");
    mvfxDestroyContext(scene.context);
}

static void TestTurnedAndPlanes(void)
{
    // A camera at the world's (10, 0, 0) turned to look down -X sees the
    // wall x = 0: met there, its normal turned into the world.
    Paint(0.0, 0, SIDE, false);
    mvfxDepthImage turned = Camera(s_floats, mvfx_depthFloat32, false);
    turned.origin[2] = 0.0;
    turned.origin[0] = 10.0;
    turned.rotation[1] = 0.70710678f;
    turned.rotation[3] = 0.70710678f;
    mvfxEmitterDef emitter = Seeing();
    Scene scene = NewScene(&emitter);
    Show(&scene, &turned);
    Place(&scene, 1, 0, 0.3, -2, 0, 0);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[0], 1.0, 1e-3) &&
              Near((double)At(&scene, 0).velocity[0], 2.0, 1e-3),
          "a turned camera");
    mvfxDestroyContext(scene.context);

    // With a scene plane at z = -0.3 too, the wall at z = 0 comes first.
    Paint(0.0, 0, SIDE, false);
    mvfxDepthImage wall = Camera(s_floats, mvfx_depthFloat32, false);
    emitter = Seeing();
    emitter.collision.sceneQueries = true;
    emitter.collision.queryCapacity = 4;
    emitter.collision.lookahead = 0.5f;
    scene = NewScene(&emitter);
    Show(&scene, &wall);
    Place(&scene, 0, 0, 1, 0, 0, -2);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    mvfxSceneAnswer answer = {.hit = true, .point = {0, 0, -0.3}, .normal = {0, 0, 1}};
    CHECK(mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, &answer, 1) ==
              mvfx_success,
          "answered");
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], 1.0, 1e-3), "the earlier contact");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    Paint(0.0, 0, SIDE, false);
    mvfxEmitterDef emitter = Seeing();
    Scene scene = NewScene(&emitter);
    mvfxDepthImage good = Camera(s_floats, mvfx_depthFloat32, false);
    Show(&scene, &good);
    uint64_t misuse = mvfxGetContextMisuse(scene.context);
    static float normals[SIDE * SIDE * 3];
    for (int32_t field = 0; field < 14; ++field)
    {
        mvfxDepthImage bad = good;
        switch (field)
        {
        case 0:
            bad.pixels = NULL;
            break;
        case 1:
            bad.width = 0;
            break;
        case 2:
            bad.height = MVFX_MAX_DEPTH_SIZE + 1;
            break;
        case 3:
            bad.stride = SIDE * 4 - 1;
            break;
        case 4:
            bad.byteCount -= 1;
            break;
        case 5:
            bad.format = 3;
            break;
        case 6:
            bad.projection[5] = (float)NAN;
            break;
        case 7:
            memset(bad.projection, 0, sizeof bad.projection);
            break;
        case 8:
            bad.rotation[3] = 0.9f;
            break;
        case 9:
            bad.origin[0] = 0x1p53;
            break;
        case 10:
            bad.normals = normals;
            bad.normalByteCount = (int64_t)sizeof normals;
            bad.normalStride = SIDE * 12 - 1;
            break;
        case 11:
            bad.normals = normals;
            bad.normalByteCount = (int64_t)sizeof normals - 1;
            bad.normalStride = SIDE * 12;
            break;
        case 12:
            bad.format = mvfx_depthUnorm16;
            bad.byteCount = (int64_t)SIDE * SIDE * 2 - 1;
            bad.stride = SIDE * 2;
            break;
        default:
            bad.origin[2] = (double)NAN;
            break;
        }
        CHECK(mvfxInstance_SetDepthImage(scene.context, scene.instance, &bad) == mvfx_errorInvalid,
              "an image out of range");
    }
    CHECK(mvfxGetContextMisuse(scene.context) == misuse + 14, "each counted");
    // The good image kept: the wall still met.
    Place(&scene, 0, 0, 1, 0, 0, -2);
    Step(&scene);
    CHECK(Near((double)At(&scene, 0).position[2], 1.0, 1e-3), "the last image kept");
    // A stride wider than a row, the last row's end exact, is in range.
    static float wide[SIDE * (SIDE + 3)];
    mvfxDepthImage padded = good;
    padded.pixels = wide;
    padded.stride = (SIDE + 3) * 4;
    padded.byteCount = (int64_t)(SIDE - 1) * padded.stride + SIDE * 4;
    CHECK(mvfxInstance_SetDepthImage(scene.context, scene.instance, &padded) == mvfx_success,
          "a padded stride");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_SetDepthImage(scene.context, stale, &good) == mvfx_errorStale &&
              mvfxInstance_SetDepthImage(NULL, scene.instance, &good) == mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(scene.context);

    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 4; ++field)
    {
        // Past a named bound: mvfx_errorLimit, data a correct program may
        // pass, so no misuse.
        mvfxResult want = field == 3 ? mvfx_errorLimit : mvfx_errorInvalid;
        mvfxEmitterDef def = Seeing();
        switch (field)
        {
        case 0:
            def.collision.depthThickness = 0.0f;
            break;
        case 1:
            def.collision.depthThickness = (float)NAN;
            break;
        case 2:
            def.collision.depthSamples = 0;
            break;
        default:
            def.collision.depthSamples = MVFX_MAX_DEPTH_SAMPLES + 1;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &def;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == want, "refused");
    }
    mvfxDestroyContext(context);
}

int main(void)
{
    TestWall();
    TestThickness();
    TestNormals();
    TestOutside();
    TestLocal();
    TestImageEdges();
    TestTurnedAndPlanes();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
