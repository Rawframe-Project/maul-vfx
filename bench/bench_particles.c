// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's speed and memory against its targets, on one
// core, with fixed inputs. A typical sprite emitter (spawning at a rate
// from a sphere into a cone, under gravity and drag, coloured and sized
// over its life, dying of age) holds about 100,000 particles at 60 steps
// a second; each case prints the best of five runs of 60 steps after the
// emitter has filled, per particle (or per point, instance or emitter),
// with its target:
//   the sprite emitter's update; with one octave of turbulence; with
//   three planes and a sphere to collide with (the extra over the plain
//   emitter); packing its records (position, size, colour in 8 bits,
//   angle); a radix sort for a view (the extra over packing unsorted);
//   ribbon output with two pieces a segment, a point; an emitter with
//   nothing alive and nothing to spawn; a finished instance; then its
//   memory: bytes a particle, an instance's fixed bytes for its emitter,
//   and the compiled effect's bytes, through a counting allocator.
// Beside each time, the instructions it took (Linux's hardware counter,
// "n/a" where it cannot be opened), which a loaded machine does not
// change as it does the time.

// Before every include: Linux's syscall, for the instruction counter.
#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

enum
{
    RUNS = 5,
    STEPS = 60,
    // Steps before timing: the sprite's particles live 1.8 to 2.2 s.
    WARM = 150,
    // Instances for the idle and finished cases.
    MANY = 1000
};

static const int64_t s_step = MVFX_TICKS_PER_SECOND / 60;

// The time now in seconds.
static double Now(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + 1e-9 * (double)now.tv_nsec;
}

// A span of time and of instructions retired in user space.
typedef struct Measure
{
    double seconds;
    double instructions;
} Measure;

#if defined(__linux__)
// The counter of this thread's user-space instructions, -1 for none.
static int s_counter = -1;
#endif

static void OpenCounter(void)
{
#if defined(__linux__)
    struct perf_event_attr attr = {.type = PERF_TYPE_HARDWARE,
                                   .size = sizeof attr,
                                   .config = PERF_COUNT_HW_INSTRUCTIONS,
                                   .exclude_kernel = 1,
                                   .exclude_hv = 1};
    s_counter = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
#endif
}

// The instructions counted so far, -1 without a counter.
static double Instructions(void)
{
#if defined(__linux__)
    long long count = 0;
    if (s_counter >= 0 && read(s_counter, &count, sizeof count) == (ssize_t)sizeof count)
    {
        return (double)count;
    }
#endif
    return -1.0;
}

static Measure Mark(void)
{
    return (Measure){Now(), Instructions()};
}

// The span since a mark, divided over n.
static Measure Since(Measure start, double n)
{
    Measure now = Mark();
    return (Measure){(now.seconds - start.seconds) / n,
                     start.instructions < 0.0 ? -1.0 : (now.instructions - start.instructions) / n};
}

// The faster of two spans.
static Measure Best(Measure best, Measure each)
{
    return best.seconds < 0.0 || each.seconds < best.seconds ? each : best;
}

// The bytes a counting allocator holds, and their blocks' sizes kept
// before each block.
static int64_t s_held;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)context;
    size_t front = alignment > 16 ? alignment : 16;
    unsigned char* block = malloc(size + front + alignment);
    if (block == NULL)
    {
        return NULL;
    }
    uintptr_t start = ((uintptr_t)block + front + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)start)[-1] = block;
    s_held += (int64_t)size;
    return (void*)start;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    if (memory != NULL)
    {
        free(((void**)memory)[-1]);
        s_held -= (int64_t)size;
    }
}

static const mvfxColorKey s_colors[2] = {{0.0f, 1.0f, 0.9f, 0.6f}, {1.0f, 0.8f, 0.2f, 0.1f}};
static const mvfxAlphaKey s_alphas[2] = {{0.0f, 1.0f}, {1.0f, 0.0f}};
static const mvfxCurveKey s_growing[2] = {{.time = 0.0f, .value = 0.5f},
                                          {.time = 1.0f, .value = 1.5f}};

// The typical sprite emitter of capacity particles at rate a second.
static mvfxEmitterDef Sprite(int32_t capacity, float rate)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = capacity;
    emitter.rate = rate;
    emitter.lifetime = mvfxRangeSource(1.8f, 2.2f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapeSphere, .rotation = {0, 0, 0, 1}, .radius = 0.5f};
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {0, 1, 0},
                                      .coneAngle = 0.4f,
                                      .speed = {2.0f, 4.0f}};
    emitter.motion.gravity[1] = -9.8f;
    emitter.motion.drag = 0.5f;
    emitter.overLife.color = (mvfxColorOverLife){
        .enabled = true,
        .gradient = {
            .colorKeys = s_colors, .colorKeyCount = 2, .alphaKeys = s_alphas, .alphaKeyCount = 2}};
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 1.0f, .curve = {.keys = s_growing, .keyCount = 2}}}};
    return emitter;
}

// A context, an effect of one emitter and n instances of it.
typedef struct Scene
{
    mvfxContext* context;
    mvfxEffectId effect;
    mvfxInstanceId instances[MANY];
    int32_t count;
} Scene;

static bool Open(Scene* scene, const mvfxEmitterDef* emitter, int32_t count)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.limits.instances = MANY;
    def.allocator = (mvfxAllocator){.alloc = Allocate, .free = Free};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    *scene = (Scene){.count = count};
    if (mvfxCreateContext(&def, &scene->context) != mvfx_success ||
        mvfxCreateEffect(scene->context, &effectDef, &scene->effect) != mvfx_success)
    {
        return false;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = scene->effect;
        instanceDef.seed = 7u;
        if (mvfxCreateInstance(scene->context, &instanceDef, &scene->instances[i]) != mvfx_success)
        {
            return false;
        }
    }
    return true;
}

static bool Steps(Scene* scene, int32_t steps)
{
    for (int32_t s = 0; s < steps; ++s)
    {
        if (mvfxContext_StepTicks(scene->context, s_step) != mvfx_success)
        {
            return false;
        }
    }
    return true;
}

// The particles alive in the scene's first instance.
static int32_t Live(Scene* scene)
{
    mvfxEmitterStats stats = {0};
    return mvfxInstance_GetEmitterStats(scene->context, scene->instances[0], 0, &stats) ==
                   mvfx_success
               ? stats.live
               : 0;
}

// A step of a scene of count instances of an emitter, the best of the
// runs, after warm steps; the particles of its first instance.
static Measure TimeSteps(const mvfxEmitterDef* emitter, int32_t count, int32_t warm, int32_t* live)
{
    Measure best = {-1.0, -1.0};
    for (int32_t r = 0; r < RUNS; ++r)
    {
        Scene scene;
        if (!Open(&scene, emitter, count) || !Steps(&scene, warm))
        {
            return (Measure){-1.0, -1.0};
        }
        Measure start = Mark();
        if (!Steps(&scene, STEPS))
        {
            return (Measure){-1.0, -1.0};
        }
        best = Best(best, Since(start, STEPS));
        *live = Live(&scene);
        mvfxDestroyContext(scene.context);
    }
    return best;
}

// A measure over n: a particle's, a point's, an instance's.
static Measure Per(Measure measure, double n)
{
    return (Measure){measure.seconds / n,
                     measure.instructions < 0.0 ? -1.0 : measure.instructions / n};
}

// Prints a case's time and instructions, less a base's (none: zeros), and
// its time's target.
static void Report(const char* name, Measure value, Measure base, const char* unit, double target)
{
    char instructions[32] = "n/a";
    if (value.instructions >= 0.0)
    {
        snprintf(instructions, sizeof instructions, "%.1f",
                 value.instructions - (base.instructions > 0.0 ? base.instructions : 0.0));
    }
    printf("%-36s %10.2f %-14s %10s instructions (target %g)\n", name,
           1e9 * (value.seconds - base.seconds), unit, instructions, target);
}

// Prints a figure that is no time, such as bytes, and its target.
static void ReportBytes(const char* name, double value, double target)
{
    printf("%-36s %10.2f %-14s (target %g)\n", name, value, "bytes", target);
}

// The update's cases: a particle's step of the sprite, with turbulence,
// and with collision (less the plain one's).
static int Updates(void)
{
    static const Measure none = {0.0, 0.0};
    mvfxEmitterDef sprite = Sprite(110000, 50000.0f);
    int32_t live = 0;
    Measure plain = Per(TimeSteps(&sprite, 1, WARM, &live), live);
    if (plain.seconds < 0.0 || live == 0)
    {
        return 1;
    }
    printf("# the sprite emitter: %d particles alive\n", live);
    Report("sprite update", plain, none, "ns a particle", 10);

    static const mvfxForce turbulence = {
        .kind = mvfx_forceTurbulence,
        .strength = 2.0f,
        .noise = {.frequency = 1.0f, .octaves = 1, .lacunarity = 2.0f, .gain = 0.5f}};
    mvfxEmitterDef noisy = sprite;
    noisy.forces = &turbulence;
    noisy.forceCount = 1;
    Measure measure = Per(TimeSteps(&noisy, 1, WARM, &live), live);
    if (measure.seconds < 0.0 || live == 0)
    {
        return 1;
    }
    Report("sprite update, turbulence", measure, none, "ns a particle", 40);

    static const mvfxCollider walls[4] = {
        {.kind = mvfx_colliderPlane,
         .center = {0, -1, 0},
         .normal = {0, 1, 0},
         .rotation = {0, 0, 0, 1}},
        {.kind = mvfx_colliderPlane,
         .center = {-3, 0, 0},
         .normal = {1, 0, 0},
         .rotation = {0, 0, 0, 1}},
        {.kind = mvfx_colliderPlane,
         .center = {3, 0, 0},
         .normal = {-1, 0, 0},
         .rotation = {0, 0, 0, 1}},
        {.kind = mvfx_colliderSphere,
         .center = {0, 1.5f, 0},
         .radius = 0.4f,
         .rotation = {0, 0, 0, 1}},
    };
    mvfxEmitterDef colliding = sprite;
    colliding.colliders = walls;
    colliding.colliderCount = 4;
    colliding.collision.enabled = true;
    colliding.collision.radius = 0.02f;
    colliding.collision.restitution = 0.5f;
    measure = Per(TimeSteps(&colliding, 1, WARM, &live), live);
    if (measure.seconds < 0.0 || live == 0)
    {
        return 1;
    }
    Report("collision, 3 planes and a sphere", measure, plain, "ns more", 8);
    return 0;
}

// A call to write the first instance's records, the best of
// RUNS * 4 calls; the records into count.
static Measure TimeWrite(Scene* scene, mvfxLayoutId layout, void* scratch, int64_t scratchBytes,
                         void* records, int64_t capacity, int64_t* count)
{
    static const mvfxView view = {.eye = {0, 1, 8}, .forward = {0, 0, -1}, .up = {0, 1, 0}};
    Measure best = {-1.0, -1.0};
    for (int32_t r = 0; r < RUNS * 4; ++r)
    {
        Measure start = Mark();
        if (mvfxInstance_WriteParticles(scene->context, scene->instances[0], 0, layout, &view,
                                        scratch, scratchBytes, records, capacity,
                                        count) != mvfx_success)
        {
            return (Measure){-1.0, -1.0};
        }
        best = Best(best, Since(start, 1.0));
    }
    return best;
}

// Packing and sorting: a scene of the sprite unsorted and one sorted back
// to front, stepped alike, writing the same records.
static int Outputs(void)
{
    static const mvfxLayoutField fields[4] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat32, .components = 1, .offset = 12},
        {.field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 4, .offset = 16},
        {.field = mvfx_recordAngle, .format = mvfx_formatFloat32, .components = 1, .offset = 20},
    };
    Measure seconds[2] = {0};
    int64_t count = 0;
    for (int32_t sorted = 0; sorted < 2; ++sorted)
    {
        mvfxEmitterDef sprite = Sprite(110000, 50000.0f);
        sprite.sortMode = sorted ? mvfx_sortBackToFront : mvfx_sortNone;
        Scene scene;
        mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
        layoutDef.fields = fields;
        layoutDef.fieldCount = 4;
        layoutDef.stride = 24;
        mvfxLayoutId layout = {0};
        int64_t scratchBytes = 0;
        if (!Open(&scene, &sprite, 1) || !Steps(&scene, WARM) ||
            mvfxCreateLayout(scene.context, &layoutDef, &layout) != mvfx_success ||
            mvfxInstance_GetSortScratchBytes(scene.context, scene.instances[0], 0, &scratchBytes) !=
                mvfx_success)
        {
            return 1;
        }
        void* scratch = scratchBytes > 0 ? malloc((size_t)scratchBytes) : NULL;
        void* records = malloc(110000 * 24);
        seconds[sorted] = records != NULL ? TimeWrite(&scene, layout, scratch, scratchBytes,
                                                      records, 110000, &count)
                                          : (Measure){-1.0, -1.0};
        free(records);
        free(scratch);
        mvfxDestroyContext(scene.context);
        if (seconds[sorted].seconds < 0.0 || count == 0)
        {
            return 1;
        }
    }
    static const Measure none = {0.0, 0.0};
    Measure packing = Per(seconds[0], (double)count);
    Report("packing records", packing, none, "ns a particle", 3);
    Report("radix sort for a view", Per(seconds[1], (double)count), packing, "ns a particle", 10);
    return 0;
}

// Ribbon output: one ribbon through about 20,000 particles, two pieces a
// segment, its points' position, width, colour and coordinate along.
static int Ribbons(void)
{
    static const mvfxLayoutField fields[4] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
        {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 12},
        {.field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 4, .offset = 16},
        {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 20},
    };
    mvfxEmitterDef trail = Sprite(22000, 10000.0f);
    trail.ribbon.ids = mvfx_ribbonOne;
    trail.ribbon.pieces = 2;
    Scene scene;
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 4;
    layoutDef.stride = 24;
    mvfxLayoutId layout = {0};
    int64_t scratchBytes = 0;
    if (!Open(&scene, &trail, 1) || !Steps(&scene, WARM) ||
        mvfxCreateLayout(scene.context, &layoutDef, &layout) != mvfx_success ||
        mvfxInstance_GetSortScratchBytes(scene.context, scene.instances[0], 0, &scratchBytes) !=
            mvfx_success)
    {
        return 1;
    }
    static const mvfxView view = {.eye = {0, 1, 8}, .forward = {0, 0, -1}, .up = {0, 1, 0}};
    int64_t capacity = 22000 * 2 + 2;
    mvfxRibbonBuffers buffers = {.points = malloc((size_t)capacity * 24),
                                 .pointCapacity = capacity,
                                 .ribbons = malloc(sizeof(mvfxRibbon) * 16),
                                 .ribbonCapacity = 16,
                                 .scratch = malloc((size_t)scratchBytes),
                                 .scratchBytes = scratchBytes};
    Measure best = {-1.0, -1.0};
    int64_t points = 0;
    int64_t ribbons = 0;
    for (int32_t r = 0; r < RUNS * 4 && buffers.points != NULL && buffers.ribbons != NULL &&
                        buffers.scratch != NULL;
         ++r)
    {
        Measure start = Mark();
        if (mvfxInstance_WriteRibbons(scene.context, scene.instances[0], 0, layout, &view, &buffers,
                                      &points, &ribbons) != mvfx_success)
        {
            break;
        }
        best = Best(best, Since(start, 1.0));
    }
    free(buffers.points);
    free(buffers.ribbons);
    free(buffers.scratch);
    mvfxDestroyContext(scene.context);
    if (best.seconds < 0.0 || points == 0)
    {
        return 1;
    }
    printf("# the ribbon: %lld points in %lld ribbons\n", (long long)points, (long long)ribbons);
    static const Measure none = {0.0, 0.0};
    Report("ribbon output, 2 pieces", Per(best, (double)points), none, "ns a point", 15);
    return 0;
}

// An emitter with nothing alive and nothing to spawn, and a finished
// instance: MANY instances of each, a step's time over them.
static int Idle(void)
{
    mvfxEmitterDef idle = Sprite(16, 0.0f);
    int32_t live = 0;
    static const Measure none = {0.0, 0.0};
    Measure seconds = TimeSteps(&idle, MANY, 10, &live);
    if (seconds.seconds < 0.0 || live != 0)
    {
        return 1;
    }
    Report("an idle emitter", Per(seconds, MANY), none, "ns a step", 50);

    mvfxEmitterDef brief = Sprite(16, 100.0f);
    brief.duration = 0.05f;
    brief.loops = 1;
    brief.lifetime = mvfxRangeSource(0.05f, 0.05f);
    seconds = TimeSteps(&brief, MANY, 60, &live);
    if (seconds.seconds < 0.0 || live != 0)
    {
        return 1;
    }
    Report("a finished instance", Per(seconds, MANY), none, "ns a step", 20);
    return 0;
}

// Memory: the bytes the counting allocator holds for an instance of the
// sprite at two capacities, and for its compiled effect.
static int Memory(void)
{
    int64_t held[2] = {0};
    int64_t effect = 0;
    for (int32_t k = 0; k < 2; ++k)
    {
        mvfxEmitterDef sprite = Sprite(k == 0 ? 10000 : 20000, 100.0f);
        Scene scene;
        int64_t before = s_held;
        if (!Open(&scene, &sprite, 0))
        {
            return 1;
        }
        int64_t opened = s_held;
        mvfxInstanceDef def = mvfxDefaultInstanceDef();
        def.effect = scene.effect;
        mvfxInstanceId instance = {0};
        if (mvfxCreateInstance(scene.context, &def, &instance) != mvfx_success)
        {
            return 1;
        }
        held[k] = s_held - opened;
        (void)before;
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &sprite;
        effectDef.emitterCount = 1;
        mvfxEffectId second = {0};
        int64_t start = s_held;
        if (mvfxCreateEffect(scene.context, &effectDef, &second) != mvfx_success)
        {
            return 1;
        }
        effect = s_held - start;
        mvfxDestroyContext(scene.context);
    }
    double perParticle = (double)(held[1] - held[0]) / 10000.0;
    ReportBytes("a sprite particle", perParticle, 64);
    ReportBytes("an instance's fixed bytes, 1 emitter", (double)held[0] - perParticle * 10000.0,
                1024);
    ReportBytes("the compiled effect", (double)effect, 16384);
    return 0;
}

int main(void)
{
    OpenCounter();
    int failed = Updates();
    failed |= Outputs();
    failed |= Ribbons();
    failed |= Idle();
    failed |= Memory();
    if (failed)
    {
        printf("a case failed\n");
    }
    return failed;
}
