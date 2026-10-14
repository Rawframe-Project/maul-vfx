# Maul VFX API reference

Generated from the public headers by `tools/gen_api.py`. The headers
are the source of truth; this file mirrors them.

## `base.h`

The base of the Maul VFX API: the library version, the export and attribute macros, and the result codes every fallible function returns.

```c
mvfxVersion mvfxGetVersion(void);
```
Returns the version of the library that was linked, which may differ from the MVFX_VERSION macros a program was compiled with.  @return The library version. @par Thread safety Safe from any thread.

```c
uint64_t mvfxHash64(uint64_t seed, const void* data, int32_t byteCount);
```
Hashes bytes into a 64-bit value, the hash the determinism checks and effect identities are built on: eight bytes a round, read in the host's byte order, xored in, multiplied by an odd constant and folded, the leftover bytes one at a time. Its constants are frozen.  @param seed       MVFX_HASH_INIT, or a hash to continue. @param data       The bytes. May be NULL when byteCount is 0. @param byteCount  The number of bytes, at least 0. @return The hash. @par Thread safety Safe from any thread.

```c
const char* mvfxResultName(mvfxResult result);
```
Returns the name of a result code, for diagnostics.  @param result  Any value; an unknown one is named as such. @return A static, NUL-terminated string such as "mvfx_errorCapacity". @par Thread safety Safe from any thread.

## `context.h`

The root of Maul VFX: a context holds the host's allocator and task hooks, and owns the effects and instances made through it.

```c
static inline mvfxContextDef mvfxDefaultContextDef(void);
```
Returns the default context def: the C library's allocator, serial work, 256 effects, 1,024 instances, 64 meshes and 64 grids.  The default is built in the program from the headers it includes, so the version it stamps is the program's.  @return The def, with a valid cookie and version. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateContext(const mvfxContextDef* def, mvfxContext** contextOut);
```
Creates a context.  @param def         The def, from mvfxDefaultContextDef. @param contextOut  Receives the context; set to NULL on failure. @return `mvfx_success`; `mvfx_errorVersion` for a def stamped with another major or minor version than the library's, checked before anything else of it; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, an allocator with one function, a task system with one hook or a limit out of its range; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void mvfxDestroyContext(mvfxContext* context);
```
Destroys a context and everything it owns.  @param context  The context. NULL does nothing. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
uint64_t mvfxContext_GetMisuse(const mvfxContext* context);
```
Returns how many calls the context refused as misuse: invalid arguments against it. Stale ids are not misuse.  @param context  The context. @return The count since the context was created. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `effect.h`

Effects: what an effect's emitters spawn and how their particles live, stated once in a def and compiled when the effect is created. An effect never changes after; instances of it play it.

```c
mvfxEmitterDef mvfxDefaultEmitterDef(void);
```
Returns the default emitter def: 1,024 particles in world space, 64 a second without end from the instance's start, none by distance, no bursts, each living one second, born at the emitter's origin at rest (a point shape, unturned, whole arcs, along the shape's normal at a speed of 0), with no gravity, no drag, no forces, and velocity modules that leave the motion as it is (a motion scale of 1, nothing added, no limit).  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
mvfxEffectDef mvfxDefaultEffectDef(void);
```
Returns the default effect def, with no emitters yet.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateEffect(mvfxContext* context, const mvfxEffectDef* def, mvfxEffectId* effectOut);
```
Creates an effect from a def: checks every field of it and of its emitters, then compiles it, allocating its compiled form in one block from the context's allocator.  @param context    The context that owns the effect. @param def        The def, from mvfxDefaultEffectDef. @param effectOut  Receives the effect's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def or an emitter def without its cookie, a field out of its range or an invalid curve, which counts as misuse when the context is given; `mvfx_errorStale` for a mesh shape's mesh that names no live mesh, `mvfx_errorInvalid` too for a mesh without the elements its mode needs; `mvfx_errorLimit` when the context holds its limit of effects; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyEffect(mvfxContext* context, mvfxEffectId effect);
```
Destroys an effect and returns its memory to the context's allocator.  @param context  The context that owns the effect. @param effect   The effect. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live effect (the null id included); `mvfx_errorBusy` while an instance plays it; `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxEffect_IsValid(const mvfxContext* context, mvfxEffectId effect);
```
Returns whether an id names a live effect of the context. A stale id is not misuse.  @param context  The context. @param effect   Any id. @return Whether the effect is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `grid.h`

Grids: values the host gives at the points of a regular 3D lattice, one to four components each, sampled trilinearly. A vector field force reads a grid of three components spanning a box; the host may give a grid new values between steps, as an animated field.

```c
mvfxGridDef mvfxDefaultGridDef(void);
```
Returns the default grid def: one point of one component, binary32, with no values yet.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateGrid(mvfxContext* context, const mvfxGridDef* def, mvfxGridId* gridOut);
```
Creates a grid: checks the def and copies its values.  @param context  The context that will own the grid. @param def      The def, from mvfxDefaultGridDef. @param gridOut  Receives the grid's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, a size or count out of range or a value out of range, which counts as misuse when the context is given; `mvfx_errorLimit` when the context holds its limit of grids; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyGrid(mvfxContext* context, mvfxGridId grid);
```
Destroys a grid.  @param context  The context that owns the grid. @param grid     The grid. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live grid; `mvfx_errorBusy` while an effect reads it; `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxGrid_IsValid(const mvfxContext* context, mvfxGridId grid);
```
Returns whether an id names a live grid of the context. A stale id is not misuse.  @param context  The context. @param grid     Any id. @return Whether the grid is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxGrid_SetValues(mvfxContext* context, mvfxGridId grid, const float* values, int64_t count);
```
Gives a grid new values for the same points, as an animated field; steps after read them.  @param context  The context that owns the grid. @param grid     The grid. @param values   The values, as in the grid def. @param count    How many: the grid's points times its components. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live grid; `mvfx_errorInvalid` for a NULL context or values, a count not the grid's or a value out of range, which counts as misuse when the context is given (the grid is then unchanged). @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `instance.h`

Instances: one playing of an effect, at a place, with a seed. A context's step advances every instance it owns.

```c
mvfxInstanceDef mvfxDefaultInstanceDef(void);
```
Returns the default instance def: no effect yet, at the world's origin, unturned and at scale 1, seeded by its order of creation.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateInstance(mvfxContext* context, const mvfxInstanceDef* def, mvfxInstanceId* instanceOut);
```
Creates an instance of an effect, allocating every particle it can hold in one block from the context's allocator. Its clock starts at 0 and its emitters start spawning with the next step.  @param context      The context that owns the effect and will own the instance. @param def          The def, from mvfxDefaultInstanceDef. @param instanceOut  Receives the instance's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, or an origin, rotation or scale out of range, which counts as misuse when the context is given; `mvfx_errorStale` for an effect id that names no live effect; `mvfx_errorLimit` when the context holds its limit of instances; `mvfx_errorCapacity` when the allocator fails or the block's size does not fit. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyInstance(mvfxContext* context, mvfxInstanceId instance);
```
Destroys an instance and its particles.  @param context   The context that owns the instance. @param instance  The instance. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance (the null id included); `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxInstance_IsValid(const mvfxContext* context, mvfxInstanceId instance);
```
Returns whether an id names a live instance of the context. A stale id is not misuse.  @param context   The context. @param instance  Any id. @return Whether the instance is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetTransform(mvfxContext* context, mvfxInstanceId instance, const double origin[3], const float rotation[4], float scale, bool teleport);
```
Sets where an instance is at the end of the next step. Over that step it moves in a line from where it was (the rotation by normalized interpolation, the scale linearly), and births within it take its place at their ticks; its travel feeds spawning by distance and its velocity what emitters inherit. A teleport moves it at once instead, with no travel and no velocity. How births along the way fall depends on how often the host moves it, which is the host's to keep the same.  @param context   The context that owns the instance. @param instance  The instance. @param origin    Its place in the world, each value finite and at most 2^52 in size. @param rotation  A unit quaternion (x, y, z, w), within 0.001 of unit length. @param scale     From MVFX_MIN_SCALE to MVFX_MAX_SCALE. @param teleport  Whether it jumps there. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or a value out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Spawn(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxSpawnRequest* request);
```
Queues particles for an emitter of an instance: they are born at the start of the next step, before its other births, each taking a serial and room like any birth (refused, and counted, when the emitter is full). The values are copied: the arrays may be released at once.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def. @param request   The particles. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, an emitter index out of range, a count below 1, a value out of range, or a color or size for an emitter that does not store it, which counts as misuse when the context is given; `mvfx_errorCapacity` when the emitter's host queue lacks the room (none of the particles is queued). @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Prewarm(mvfxContext* context, mvfxInstanceId instance, double seconds, double stepSeconds, int32_t maxSteps, double* remainingOut);
```
Advances one instance alone toward a span, in steps of a given length (the last shorter), at most a number of steps a call, so a host can spread a long prewarm over frames: it calls again with the span left until none is. The same calls reach the same state. Spans and steps are rounded once to ticks.  @param context       The context that owns the instance. @param instance      The instance. @param seconds       The span left, from 0 to 1e9. @param stepSeconds   Each step's length, above 0 and at most MVFX_MAX_STEP_SECONDS, one tick at least. @param maxSteps      At least 1. @param remainingOut  Receives the span left, exactly as ticks; may be NULL. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context or a value out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
void mvfxModule_Random(const mvfxModuleCall* call, uint32_t serial, uint32_t stream, uint32_t out[4]);
```
Draws four random words for a host module: pcg4d (Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 2020) of the module's seed, a particle's serial, a stream number the module picks and the step's end tick, so the same call in the same step gives the same words however the work is split.  @param call    The module's call. @param serial  A particle's serial (or any number). @param stream  The module's own number for this draw. @param out     Receives the four words. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetEmitterMesh(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxMeshId mesh);
```
Makes an emitter of an instance spawn from another mesh, as each character's skinned mesh feeds its own instance. The mesh may not be destroyed while the instance is bound to it.  @param context   The context that owns the instance and the mesh. @param instance  The instance. @param emitter   The index, in the effect's def, of an emitter with a mesh shape. @param mesh      The mesh, with the triangles or tetrahedra the emitter's mesh mode needs. @return `mvfx_success`; `mvfx_errorStale` for an instance or a mesh id that names nothing live; `mvfx_errorInvalid` for a NULL context, an emitter index out of range or without a mesh shape, or a mesh without the elements its mode needs, which counts as misuse. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetParameter(mvfxContext* context, mvfxInstanceId instance, int32_t parameter, const float value[4]);
```
Sets a parameter of an instance; births from the next step read it. A restart keeps it.  @param context    The context that owns the instance. @param instance   The instance. @param parameter  An index below the effect's count of parameters. @param value      Four finite floats. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, an index out of range or a value not finite, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetParameter(mvfxContext* context, mvfxInstanceId instance, int32_t parameter, float valueOut[4]);
```
Reads a parameter of an instance.  @param context    The context that owns the instance. @param instance   The instance. @param parameter  An index below the effect's count of parameters. @param valueOut   Receives four floats; zeroed on failure. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or an index out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterStats(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxEmitterStats* statsOut);
```
Reads what an emitter of an instance has done.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def. @param statsOut  Receives the counts; zeroed on failure. @return `mvfx_success`; `mvfx_errorStale` for an instance id that names no live instance; `mvfx_errorInvalid` for a NULL argument or an emitter index out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxInstance_IsFinished(const mvfxContext* context, mvfxInstanceId instance);
```
Returns whether every emitter of an instance is stopped: it will spawn nothing more and holds no particles, and stepping it costs nothing. A stale id is not misuse.  @param context   The context. @param instance  Any id. @return Whether the instance is alive and finished. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Stop(mvfxContext* context, mvfxInstanceId instance, mvfxStopMode mode);
```
Stops every emitter of an instance from spawning past its clock.  @param context   The context that owns the instance. @param instance  The instance. @param mode      What becomes of its particles. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context or an unknown mode, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Restart(mvfxContext* context, mvfxInstanceId instance);
```
Plays an instance again from its start: its particles are removed, its clock and counts return to 0 and a stop is lifted. Its seed is kept, so the same steps play the same particles again.  @param context   The context that owns the instance. @param instance  The instance. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxContext_Step(mvfxContext* context, double seconds);
```
Advances every instance of the context by a span of time: each emitter spawns what it owes by the new time, each particle ages from its birth, and particles whose life has ended are removed. The span is rounded once to ticks (MVFX_TICKS_PER_SECOND), so spans whose ticks sum to the same clock reach the same state.  @param context  The context. @param seconds  The span, from 0 to MVFX_MAX_STEP_SECONDS. @return `mvfx_success`, or `mvfx_errorInvalid` for a NULL context or a span out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time. Its work runs on the host's task hooks when the context has them.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxContext_StepTicks(mvfxContext* context, int64_t ticks);
```
Advances every instance of the context by a number of ticks, as mvfxContext_Step does without the rounding.  @param context  The context. @param ticks    The span in ticks, from 0 to MVFX_MAX_STEP_SECONDS seconds' worth. @return `mvfx_success`, or `mvfx_errorInvalid` for a NULL context or a span out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time. Its work runs on the host's task hooks when the context has them.

## `mesh.h`

Meshes to spawn from: vertices, triangles and tetrahedra the host gives. A mesh shape samples its vertices, its edges by length, its surface by area or its volume (the tetrahedra) by volume. A skinned mesh is given new positions between steps.

```c
mvfxMeshDef mvfxDefaultMeshDef(void);
```
Returns the default mesh def, with no vertices yet.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateMesh(mvfxContext* context, const mvfxMeshDef* def, mvfxMeshId* meshOut);
```
Creates a mesh: checks the def, copies it, and builds its edges (each pair of vertices a triangle joins, once), its normals unless given, and the sums that choose an element by length, area or volume.  @param context  The context that will own the mesh. @param def      The def, from mvfxDefaultMeshDef. @param meshOut  Receives the mesh's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, a count out of range, an index past the vertices or a value out of range, which counts as misuse when the context is given; `mvfx_errorLimit` when the context holds its limit of meshes; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyMesh(mvfxContext* context, mvfxMeshId mesh);
```
Destroys a mesh.  @param context  The context that owns the mesh. @param mesh     The mesh. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live mesh; `mvfx_errorBusy` while an effect or an instance spawns from it; `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxMesh_IsValid(const mvfxContext* context, mvfxMeshId mesh);
```
Returns whether an id names a live mesh of the context. A stale id is not misuse.  @param context  The context. @param mesh     Any id. @return Whether the mesh is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_SetPositions(mvfxContext* context, mvfxMeshId mesh, const float* positions, const float* normals, int32_t vertexCount);
```
Gives a mesh new positions (and normals) for the same vertices, as a skinned mesh moves, and rebuilds what is drawn from them. Steps after spawn from the new positions.  @param context      The context that owns the mesh. @param mesh         The mesh. @param positions    x, y and z for each vertex, as in the mesh def. @param normals      Unit normals for each vertex, or NULL to compute them. @param vertexCount  The mesh's count of vertices. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live mesh; `mvfx_errorInvalid` for a NULL context or positions, a count not the mesh's or a value out of range, which counts as misuse when the context is given (the mesh is then unchanged). @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `value.h`

Value sources: how a particle's initial values are drawn. Every source is a base times a uniform draw between min and max: the base is 1, a curve or a gradient over the emitter's age, a gradient at a random point, or a parameter of the instance.

```c
static inline mvfxScalarSource mvfxRangeSource(float min, float max);
```
Returns a scalar source uniform between min and max.  @param min  The least value. @param max  The greatest value. @return The source. @par Thread safety Safe from any thread.

---

40 functions across 10 headers.
