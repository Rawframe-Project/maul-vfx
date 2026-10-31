// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sheets by XPBD: units born row after row on their two directions,
// counted in units; a banner pinned by its first row hanging with every
// stretch and bending pair kept; stiff shear holding a pushed banner's
// squares where soft shear lets them lean; a world sheet's spacing
// scaling with its instance; the mesh expansion (vertices, indices
// counterclockwise from the face normal, corners, sheets cut whole); the
// checks.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 256

typedef struct Record
{
    float position[3];
    float normal[3];
    float corner[2];
    uint32_t serial;
    uint32_t sheet;
    float tangent[3];
    float side[3];
} Record;

static const mvfxLayoutField s_fields[7] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 24},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 32},
    {.field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 1, .offset = 36},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat32, .components = 3, .offset = 40},
    {.field = mvfx_recordSide, .format = mvfx_formatFloat32, .components = 3, .offset = 52},
};

static const mvfxView s_view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};

typedef struct Scene
{
    mvfxContext* context;
    mvfxEffectId effect;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
    Record records[MOST];
    int64_t count;
} Scene;

static Scene s_scene;

// Whether every byte of a buffer is value.
static bool Filled(const void* buffer, size_t size, unsigned char value)
{
    const unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != value)
        {
            return false;
        }
    }
    return true;
}

// A sheet of points across x by rows along z, 0.5 apart, rigid in
// stretch, shear and bending, eight substeps, under gravity, its first
// row pinned; room for four.
static mvfxEmitterDef Sheet(int32_t points, int32_t rows)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 4 * points * rows;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxMakeRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                                 .points = points,
                                 .rows = rows,
                                 .spacing = 0.5f,
                                 .direction = {1, 0, 0},
                                 .downDirection = {0, 0, 1},
                                 .substeps = 8,
                                 .damping = 2.0f,
                                 .pinFirstRow = true};
    return emitter;
}

static void Open(const mvfxEmitterDef* emitter, float scale)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    CHECK(mvfxCreateEffect(s_scene.context, &def, &s_scene.effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s_scene.effect;
    instanceDef.scale = scale;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 7;
    layoutDef.stride = (uint32_t)sizeof(Record);
    CHECK(mvfxCreateLayout(s_scene.context, &layoutDef, &s_scene.layout) == mvfx_success, "layout");
}

static void Spawn(int32_t units, const double* places)
{
    mvfxSpawnRequest request = {.count = units, .positions = places};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
}

static void Read(void)
{
    CHECK(mvfxInstance_WriteParticles(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                      NULL, 0, s_scene.records, MOST,
                                      &s_scene.count) == mvfx_success,
          "read");
}

static void Steps(int32_t n, int64_t ticks)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, ticks) == mvfx_success, "step");
    }
}

static mvfxEmitterStats Stats(void)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

static double Gap(int32_t a, int32_t b)
{
    double square = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = (double)s_scene.records[a].position[k] - (double)s_scene.records[b].position[k];
        square += d * d;
    }
    return sqrt(square);
}

// The worst relative error of the pairs from (c, r) to (c + dc, r + dr) in
// a sheet of width w and rows h against rest.
static double Worst(int32_t w, int32_t h, int32_t dc, int32_t dr, double rest)
{
    double worst = 0.0;
    for (int32_t r = 0; r + dr < h; ++r)
    {
        for (int32_t c = (dc < 0 ? -dc : 0); c < w && c + dc < w; ++c)
        {
            double off = fabs(Gap(r * w + c, (r + dr) * w + c + dc) - rest) / rest;
            worst = off > worst ? off : worst;
        }
    }
    return worst;
}

static void TestBirths(void)
{
    // Two sheets of four by three at a tick: each point at its root plus
    // its place across along x and its row along z, serials by sheet; a
    // third and fourth fit, a fifth is refused.
    mvfxEmitterDef emitter = Sheet(4, 3);
    emitter.motion.gravity[1] = 0.0f;
    Open(&emitter, 1.0f);
    static const double roots[15] = {0, 0, 0, 0, 5, 0, 0, 10, 0, 0, 15, 0, 0, 20, 0};
    Spawn(2, roots);
    Steps(1, 1);
    Read();
    bool placed = s_scene.count == 24;
    for (int32_t i = 0; placed && i < 24; ++i)
    {
        int32_t j = i % 12;
        placed = s_scene.records[i].serial == (uint32_t)i &&
                 fabsf(s_scene.records[i].position[0] - 0.5f * (float)(j % 4)) <= 1e-5f &&
                 fabsf(s_scene.records[i].position[1] - 5.0f * (float)(i / 12)) <= 1e-5f &&
                 fabsf(s_scene.records[i].position[2] - 0.5f * (float)(j / 4)) <= 1e-5f;
    }
    CHECK(placed && Stats().owed == 2 && Stats().live == 24, "sheets born row after row");
    Spawn(3, roots);
    Steps(1, 1);
    CHECK(Stats().live == 48 && Stats().birthsDropped == 1, "the capacity in sheets");
    mvfxDestroyContext(s_scene.context);
}

static void TestBanner(void)
{
    // Six by six lying flat, its first row pinned, five seconds in air
    // drag (damping along constraints does not slow its swing as a whole):
    // it hangs down from the row, every stretch and bending pair within one
    // percent of its rest, the diagonals too, the row held.
    mvfxEmitterDef emitter = Sheet(6, 6);
    emitter.motion.drag = 3.0f;
    Open(&emitter, 1.0f);
    static const double root[3] = {0, 0, 0};
    Spawn(1, root);
    Steps(300, MVFX_TICKS_PER_SECOND / 60);
    Read();
    bool held = true;
    for (int32_t c = 0; c < 6; ++c)
    {
        held = held && s_scene.records[c].position[0] == 0.5f * (float)c &&
               s_scene.records[c].position[1] == 0.0f && s_scene.records[c].position[2] == 0.0f;
    }
    double diagonal = 0.5 * sqrt(2.0);
    CHECK(held, "the first row held");
    CHECK(Worst(6, 6, 1, 0, 0.5) <= 0.01 && Worst(6, 6, 0, 1, 0.5) <= 0.01, "stretch kept");
    CHECK(Worst(6, 6, 1, 1, diagonal) <= 0.01 && Worst(6, 6, -1, 1, diagonal) <= 0.01,
          "shear kept");
    CHECK(Worst(6, 6, 2, 0, 1.0) <= 0.01 && Worst(6, 6, 0, 2, 1.0) <= 0.01, "bending kept");
    CHECK(s_scene.records[35].position[1] < -2.4f && fabsf(s_scene.records[35].position[2]) < 0.1f,
          "hanging down");
    mvfxDestroyContext(s_scene.context);
}

static void TestShear(void)
{
    // Hanging, pushed along its row: stiff shear keeps its squares, soft
    // shear lets them lean.
    double worst[2] = {0.0, 0.0};
    for (int32_t soft = 0; soft < 2; ++soft)
    {
        mvfxEmitterDef emitter = Sheet(5, 5);
        emitter.unit.downDirection[1] = -1.0f;
        emitter.unit.downDirection[2] = 0.0f;
        emitter.unit.shearCompliance = soft ? 1.0f : 0.0f;
        emitter.motion.gravity[0] = 6.0f;
        Open(&emitter, 1.0f);
        static const double root[3] = {0, 0, 0};
        Spawn(1, root);
        Steps(120, MVFX_TICKS_PER_SECOND / 60);
        Read();
        double diagonal = 0.5 * sqrt(2.0);
        double a = Worst(5, 5, 1, 1, diagonal);
        double b = Worst(5, 5, -1, 1, diagonal);
        worst[soft] = a > b ? a : b;
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(worst[0] <= 0.02 && worst[1] >= 0.1, "shear's compliance");
}

static void TestScale(void)
{
    // A world sheet of an instance at scale 2 is born 1 apart and keeps
    // it; one of a local emitter keeps 0.5 in its space.
    for (int32_t local = 0; local < 2; ++local)
    {
        mvfxEmitterDef emitter = Sheet(3, 3);
        emitter.motion.gravity[1] = 0.0f;
        emitter.space = local ? mvfx_spaceLocal : mvfx_spaceWorld;
        emitter.unit.pinFirstRow = false;
        Open(&emitter, 2.0f);
        static const double root[3] = {0, 0, 0};
        Spawn(1, root);
        Steps(30, MVFX_TICKS_PER_SECOND / 60);
        Read();
        // Written in the world: the local sheet's 0.5 apart there, times 2.
        double rest = 1.0;
        CHECK(Worst(3, 3, 1, 0, rest) <= 1e-4 && Worst(3, 3, 0, 1, rest) <= 1e-4 &&
                  Worst(3, 3, 1, 1, rest * sqrt(2.0)) <= 1e-4,
              "the spacing with the instance's scale");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestSkewed(void)
{
    // Directions 60 degrees apart, no gravity, a second: the diagonals'
    // rests differ (0.5 sqrt 3 and 0.5) and every pair keeps its birth
    // length.
    mvfxEmitterDef emitter = Sheet(4, 4);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.pinFirstRow = false;
    emitter.unit.downDirection[0] = 0.5f;
    emitter.unit.downDirection[2] = sqrtf(3.0f) / 2.0f;
    Open(&emitter, 1.0f);
    static const double root[3] = {0, 0, 0};
    Spawn(1, root);
    Steps(60, MVFX_TICKS_PER_SECOND / 60);
    Read();
    CHECK(Worst(4, 4, 1, 1, 0.5 * sqrt(3.0)) <= 1e-4 && Worst(4, 4, -1, 1, 0.5) <= 1e-4 &&
              Worst(4, 4, 1, 0, 0.5) <= 1e-4 && Worst(4, 4, 0, 1, 0.5) <= 1e-4,
          "a skewed sheet's rests");
    mvfxDestroyContext(s_scene.context);
}

static void TestMesh(void)
{
    // Two flat sheets of four by three, no gravity: twelve vertices and
    // 36 indices each, the first quad {0, 1, 4, 4, 1, 5}, each triangle
    // counterclockwise seen from its normal's side, the normal across (x)
    // crossed with down (z), -y; corners by place and row, each vertex's
    // sheet; room for one and a half takes one.
    mvfxEmitterDef emitter = Sheet(4, 3);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.pinFirstRow = false;
    Open(&emitter, 1.0f);
    static const double roots[6] = {0, 0, 0, 0, 3, 0};
    Spawn(2, roots);
    Steps(2, MVFX_TICKS_PER_SECOND / 60);
    static Record vertices[MOST];
    static uint32_t indices[MOST];
    mvfxExpandBuffers buffers = {
        .vertices = vertices, .vertexCapacity = MOST, .indices = indices, .indexCapacity = MOST};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, &vertexCount, &indexCount) == mvfx_success &&
              vertexCount == 24 && indexCount == 72,
          "the counts");
    static const uint32_t quad[6] = {0, 1, 4, 4, 1, 5};
    bool first = true;
    for (int32_t k = 0; k < 6; ++k)
    {
        first = first && indices[k] == quad[k] && indices[36 + k] == quad[k] + 12;
    }
    CHECK(first, "the first quads");
    bool wound = true;
    for (int32_t t = 0; t < 24; ++t)
    {
        const Record* p[3] = {&vertices[indices[3 * t]], &vertices[indices[3 * t + 1]],
                              &vertices[indices[3 * t + 2]]};
        float e[2][3];
        for (int32_t k = 0; k < 3; ++k)
        {
            e[0][k] = p[1]->position[k] - p[0]->position[k];
            e[1][k] = p[2]->position[k] - p[0]->position[k];
        }
        float n[3] = {e[0][1] * e[1][2] - e[0][2] * e[1][1], e[0][2] * e[1][0] - e[0][0] * e[1][2],
                      e[0][0] * e[1][1] - e[0][1] * e[1][0]};
        float dot = n[0] * p[0]->normal[0] + n[1] * p[0]->normal[1] + n[2] * p[0]->normal[2];
        wound = wound && dot > 0.0f;
    }
    CHECK(wound, "counterclockwise from the normal");
    bool fields = true;
    for (int32_t i = 0; i < 24; ++i)
    {
        int32_t j = i % 12;
        fields = fields && fabsf(vertices[i].corner[0] - (float)(j % 4) / 3.0f) <= 1e-6f &&
                 fabsf(vertices[i].corner[1] - (float)(j / 4) / 2.0f) <= 1e-6f &&
                 vertices[i].sheet == (uint32_t)(i / 12) && vertices[i].serial == (uint32_t)i &&
                 fabsf(vertices[i].normal[1] + 1.0f) <= 1e-5f &&
                 fabsf(vertices[i].tangent[0] - 1.0f) <= 1e-5f &&
                 fabsf(vertices[i].side[2] - 1.0f) <= 1e-5f;
    }
    CHECK(fields, "corners, sheets, normals, tangents and sides");
    buffers.vertexCapacity = 18;
    memset(indices, 0, sizeof indices);
    CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, &vertexCount, &indexCount) == mvfx_success &&
              vertexCount == 24 && indexCount == 72 && indices[35] == 11 && indices[36] == 0,
          "the first whole sheet");
    buffers.vertexCapacity = MOST;
    buffers.indexCapacity = 40;
    memset(indices, 0, sizeof indices);
    CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, &vertexCount, &indexCount) == mvfx_success &&
              indices[35] == 11 && indices[36] == 0,
          "indices for one");
    CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, NULL, &indexCount) == mvfx_errorInvalid &&
              mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 1, s_scene.layout,
                                        &s_view, &buffers, &vertexCount,
                                        &indexCount) == mvfx_errorInvalid,
          "misuse");
    // What a refusal leaves: the counts 0 and the indices zeroed.
    indexCount = 7;
    CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, NULL, &indexCount) == mvfx_errorInvalid &&
              indexCount == 0,
          "no vertex count: the index count 0");
    vertexCount = 7;
    indexCount = 7;
    memset(indices, 0xEE, sizeof indices);
    CHECK(mvfxInstance_ExpandSheets(NULL, s_scene.instance, 0, s_scene.layout, &s_view, &buffers,
                                    &vertexCount, &indexCount) == mvfx_errorInvalid &&
              vertexCount == 0 && indexCount == 0 && Filled(indices, 40 * sizeof indices[0], 0),
          "no context: the counts 0, the indices zeroed");
    mvfxDestroyContext(s_scene.context);
    // A chain is no sheet.
    emitter = Sheet(4, 3);
    emitter.unit.kind = mvfx_unitChain;
    Open(&emitter, 1.0f);
    Spawn(1, roots);
    Steps(1, 1);
    CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, &vertexCount, &indexCount) == mvfx_success &&
              vertexCount == 0 && indexCount == 0,
          "a chain is no sheet");
    mvfxDestroyContext(s_scene.context);
}

static void TestPastTheCount(void)
{
    // Two sheets, 24 vertices and 72 indices, into room for more: the
    // vertices and indices past them untouched.
    mvfxEmitterDef emitter = Sheet(4, 3);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.pinFirstRow = false;
    Open(&emitter, 1.0f);
    static const double roots[6] = {0, 0, 0, 0, 3, 0};
    Spawn(2, roots);
    Steps(2, MVFX_TICKS_PER_SECOND / 60);
    static Record vertices[32];
    static uint32_t indices[96];
    memset(vertices, 0xEE, sizeof vertices);
    memset(indices, 0xEE, sizeof indices);
    mvfxExpandBuffers buffers = {
        .vertices = vertices, .vertexCapacity = 32, .indices = indices, .indexCapacity = 96};
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    CHECK(mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &buffers, &vertexCount, &indexCount) == mvfx_success &&
              vertexCount == 24 && indexCount == 72 &&
              Filled(&vertices[24], 8 * sizeof vertices[0], 0xEE) &&
              Filled(&indices[72], 24 * sizeof indices[0], 0xEE),
          "the vertices and indices past the counts untouched");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Sheet(4, 3);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 14; ++rule)
    {
        emitter = Sheet(4, 3);
        mvfxUnitDef* u = &emitter.unit;
        switch (rule)
        {
        case 0:
            u->rows = 1;
            break;
        case 1:
            u->rows = MVFX_MAX_SHEET_SIDE + 1;
            break;
        case 2:
            u->points = MVFX_MAX_SHEET_SIDE + 1;
            break;
        case 3:
            u->downDirection[2] = 0.0f;
            break;
        case 4:
            u->downDirection[0] = 1.0f;
            u->downDirection[2] = 0.0f;
            break;
        case 5:
            // 2 degrees apart.
            u->downDirection[0] = cosf(0.0349f);
            u->downDirection[2] = sinf(0.0349f);
            break;
        case 6:
            u->downDirection[0] = -cosf(0.0349f);
            u->downDirection[2] = sinf(0.0349f);
            break;
        case 7:
            u->shearCompliance = -1.0f;
            break;
        case 8:
            u->shearCompliance = NAN;
            break;
        case 9:
            emitter.ribbon.ids = mvfx_ribbonOne;
            break;
        case 10:
            emitter.capacity = 4 * 12 + 4;
            break;
        case 11:
            u->downDirection[1] = NAN;
            break;
        case 12:
            // Sides whose product overflows, refused before it is taken.
            u->points = 1 << 20;
            u->rows = 1 << 20;
            break;
        default:
            emitter.hostSpawnCapacity = 5;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want =
            rule == 1 || rule == 2 || rule == 12 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect, NULL) == want;
    }
    CHECK(refused == 14, "every check");
    emitter = Sheet(MVFX_MAX_SHEET_SIDE, MVFX_MAX_SHEET_SIDE);
    emitter.capacity = MVFX_MAX_SHEET_SIDE * MVFX_MAX_SHEET_SIDE;
    emitter.hostSpawnCapacity = 1;
    // 4 degrees apart.
    emitter.unit.downDirection[0] = cosf(0.0698f);
    emitter.unit.downDirection[2] = sinf(0.0698f);
    emitter.unit.shearCompliance = MVFX_MAX_EXTENT;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "the bounds");
    // What a chain does not read.
    emitter = Sheet(4, 3);
    emitter.unit.kind = mvfx_unitChain;
    emitter.unit.rows = -7;
    emitter.unit.downDirection[0] = NAN;
    emitter.unit.shearCompliance = -1.0f;
    emitter.ribbon.ids = mvfx_ribbonOne;
    emitter.capacity = 16;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "a chain's unread");
    mvfxDestroyContext(context);
    // A chain pins no row: unpinned and launched sideways, it flies whole.
    emitter = Sheet(4, 3);
    emitter.unit.kind = mvfx_unitChain;
    emitter.capacity = 16;
    Open(&emitter, 1.0f);
    static const double root[3] = {0, 0, 0};
    static const float launch[3] = {3, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = root, .velocities = launch};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "launched");
    Steps(30, MVFX_TICKS_PER_SECOND / 60);
    Read();
    bool fell = s_scene.count == 4;
    for (int32_t i = 0; fell && i < 4; ++i)
    {
        fell = s_scene.records[i].position[1] < -1.0f &&
               s_scene.records[i].position[0] > 0.5f * (float)i + 1.4f;
    }
    CHECK(fell, "a chain pins no row");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestBirths();
    TestBanner();
    TestShear();
    TestScale();
    TestSkewed();
    TestMesh();
    TestPastTheCount();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
