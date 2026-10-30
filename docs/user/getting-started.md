# Getting started

Maul VFX simulates visual effects and hands a renderer what to draw as data. A program creates a context, makes effects in it from defs or files, plays them as instances, steps the context, and writes each emitter's particles into its own buffers in the layout its renderer reads. This page covers building the library, the rules every call follows, and a first effect.

## Contents

- [Building and linking](#building-and-linking)
- [The rules every call follows](#the-rules-every-call-follows)
- [A first effect](#a-first-effect)
- [Samples](#samples)

## Building and linking

Maul VFX needs CMake 3.25 and GCC 14 or Clang 19 or newer, or Apple Clang; on Windows, `clang-cl` (Visual Studio's "C++ Clang tools for Windows"). On the web, Emscripten builds it: configure through `emcmake` (`emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release`), and the samples and tests run under Node.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix <prefix>
```

From CMake:

```cmake
find_package(maul-vfx REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-vfx::maul-vfx)
```

or through pkg-config as `maul-vfx`. The public headers compile as C17, C23 and C++17. The library has no dependency beyond the C library.

| Option | Default | What it does |
|---|---|---|
| `MAUL_VFX_BUILD_SHARED` | off | A shared library instead of a static one. |
| `MAUL_VFX_RHI` | off | Builds `maul-vfx-rhi`, the GPU part, beside the core; see [the GPU part](gpu.md). |
| `MAUL_VFX_BUILD_TESTS`, `MAUL_VFX_BUILD_SAMPLES`, `MAUL_VFX_BUILD_BENCH` | on at the top level | The tests, the samples and the benchmarks. |
| `MAUL_VFX_INSTALL` | on at the top level | Install rules for the library, its headers, the CMake package and the pkg-config file. |

The installed package holds the core. A program that uses the GPU part adds the source tree with `add_subdirectory` or `FetchContent` and links `maul-vfx-rhi::maul-vfx-rhi`.

`mvfxGetVersion` gives the version of the library that was linked, which a program can compare with the `MVFX_VERSION_*` macros it was compiled with.

## The rules every call follows

**A context holds everything.** `mvfxCreateContext` takes the program's allocator, its task hooks and named limits. Effects, instances, layouts, grids and meshes live in the context, and destroying it destroys them all. Each object takes its memory when it is made: stepping, writing output and reading results allocate nothing.

**Defs.** Every creation takes a def. Start each from its `mvfxDefault...Def` function and change fields: the default sets a cookie, and a def without it is refused rather than read. `mvfxDefaultContextDef` is built into the program from the headers it includes and stamps the def with their ABI version, so a context refuses a program built against another release with `mvfx_errorVersion` instead of reading a def of another layout.

**Ids.** The context is a pointer. Everything it owns is a small id struct with a generation: an id outlives its object harmlessly, and using it afterwards answers `mvfx_errorStale`.

**Results.** Every fallible function returns an `mvfxResult`; `mvfx_success` is zero. `mvfx_errorInvalid` means an argument the call can never take and also counts one misuse on the context (`mvfxContext_GetMisuse`), so a program can catch its bugs in a release build. `mvfx_errorLimit` means a value past one of the library's fixed bounds, each a named constant (`MVFX_MAX_*`, `MVFX_MIN_*`): a program may design around it. Past a bound in a def it counts no misuse; past one in a direct argument to a call on the context (a parameter, colliders, a step) it counts one. Bad untrusted data (an effect file, or a model's mesh with no hull) is refused with its status and counts no misuse, since a correct program meets it. The other errors are named in `maul-vfx/base.h`, and `mvfxGetResultName` names any value. Outputs are cleared when a call fails.

**Time is ticks.** A second is `MVFX_TICKS_PER_SECOND` (2^20) ticks. `mvfxContext_StepTicks` advances by a whole number of them, and `mvfxContext_Step` by seconds rounded to ticks. An effect's times are rounded to ticks once, when it is made, so a repeating burst never drifts and the particles do not depend on how a span of time is split into steps.

**The same bits everywhere.** The simulation gives the same particles on every platform, compiler and thread count for the same effects, seeds, inputs and steps. [Troubleshooting](troubleshooting.md#results-that-differ) lists what breaks that promise.

**Threads are the program's.** A context runs its work on the calling thread unless its def gives task hooks (`enqueueTask` and `finishTask`), which spread a step's instances over the program's own task system with the same results. A context is used by one thread at a time. The allocator, the task hooks and host modules must not call a function that changes the context: such a call is refused with `mvfx_errorBusy`.

**The library draws nothing.** It writes records, vertices and indices into the program's buffers, in layouts the program defines.

## A first effect

A fountain: an emitter spraying particles upward under gravity, stepped for a second, its particles written as positions and colours. With `maul-vfx/context.h`, `effect.h`, `instance.h` and `output.h` included:

```c
mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}

// One emitter: 50 particles a second, each living two seconds, thrown
// up at 3 to 4 units a second under gravity.
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 128;
emitter.rate = 50.0f;
emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                  .direction = {0.0f, 1.0f, 0.0f},
                                  .coneAngle = 0.2f,
                                  .speed = {3.0f, 4.0f}};
emitter.motion.gravity[1] = -9.8f;

mvfxEffectDef effectDef = mvfxDefaultEffectDef();
effectDef.emitters = &emitter;
effectDef.emitterCount = 1;
mvfxEffectId effect = {0};
mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
mvfxInstanceId instance = {0};
bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
instanceDef.effect = effect;
made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;

// A second in steps of a sixtieth.
for (int32_t step = 0; made && step < 60; ++step)
{
    made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
}

// Each particle as 28 bytes: its place, then its colour.
static const mvfxLayoutField fields[2] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
    {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 12},
};
mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
layoutDef.fields = fields;
layoutDef.fieldCount = 2;
layoutDef.stride = 28;
mvfxLayoutId layout = {0};
made = made && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success;

static float records[128][7];
const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
int64_t count = 0;
made = made && mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0,
                                           records, 128, &count) == mvfx_success;
// About 50 live: a second at 50 a second, none yet old enough to die.
bool fountain = made && count >= 45 && count <= 55;
mvfxDestroyContext(context);
return fountain;
```

[Effects](effects.md) shows what an emitter can do, [instances](instances.md) how an effect plays, and [output](output.md) what a renderer gets.

## Samples

`samples/` holds small complete programs, built when the library is the top-level project. The library draws nothing, so each writes what a renderer would draw, checks it, and runs as a test.

| Program | What it shows |
|---|---|
| `fountain.c` | A spray in a cone under gravity and wind, fading and growing over life, bouncing on the ground, written sorted and as quads. |
| `trails.c` | A tapering trail pinned to a moving instance, and a beam aimed through an instance parameter. |
| `fireworks.c` | Rockets whose deaths spawn sparks through events. |
| `flock.c` | Birds that flock, follow a path and avoid pillars. |
| `cloth.c` | A sheet of points dropped onto a ball, expanded into a mesh. |
| `smoke.c` | A 3D gas over a fire, read out as a volume. |
| `liquid.c` | Particles poured into a tank as a liquid, meshed by marching cubes. |
| `pond.c` | A water surface with floating leaves, a stone thrown in and a boat that bobs. |
| `effect_file.c` | An effect written as a file with a named grid, read back and played to the byte. |
| `gpu.c` | The fountain on the CPU and on a device side by side (with `MAUL_VFX_RHI`). |
| `minimal/` | A separate project that uses only the installed package, in C17. |

Build the minimal sample against an install:

```sh
cmake --install build --prefix "$PWD/package"
cmake -S samples/minimal -B build-minimal -DCMAKE_PREFIX_PATH="$PWD/package"
cmake --build build-minimal
./build-minimal/minimal
```
