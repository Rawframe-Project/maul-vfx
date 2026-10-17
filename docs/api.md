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
Returns the default context def: the C library's allocator, serial work, 256 effects, 1,024 instances, 64 meshes, 64 grids and 64 layouts.  The default is built in the program from the headers it includes, so the version it stamps is the program's.  @return The def, with a valid cookie and version. @par Thread safety Safe from any thread.

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

```c
int64_t mvfxContext_GetParticles(const mvfxContext* context);
```
Returns the particles the context's instances hold against its budget: the sum of their emitters' capacities at their quality levels.  @param context  The context. @return The particles held now. @par Thread safety Safe from any thread; the context is used by one thread at a time.

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

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxBakeCurlNoise(const mvfxNoise* noise, int32_t period, const int32_t size[3], float* values, int64_t valueCount);
```
Bakes a turbulence's curl noise into a grid's values over one period of a periodic lattice, for a vector field force that repeats it without a seam: the grid's n points along an axis at i / n of the period, as the repeat border reads them; the noise's octaves, gain and seed as a turbulence force takes them (an instance's seed is not mixed in), each octave's lattice wrapped every period times the lacunarity to the octave cells. A value is the turbulence's curl at that place in lattice units bit for bit, but where an octave puts it in its period's last cell, whose far corners wrap to the first. The field's box spans period / frequency along each axis; the noise's scroll is not baked (the host may move the box).  @param noise       A turbulence's noise, its lacunarity a whole number. @param period      The first octave's period in lattice cells, from 1 to MVFX_MAX_NOISE_PERIOD. @param size        The grid's points along X, Y and Z, each from 1 to MVFX_MAX_GRID_SIZE, at most MVFX_MAX_GRID_POINTS in all. @param values      Receives three values a point, X fastest, then Y, then Z. @param valueCount  The values the array holds, at least three a point. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a noise out of its ranges or with a lacunarity that is not whole, a period or size out of range, or too few values; nothing is written then. @par Thread safety Safe from any thread.

## `file.h`

Effect files: an effect def as bytes to keep and ship, written and read field by field, every integer and every float's bits little-endian whatever the host's order, checked by a hash of 64-bit words read little-endian, and refused with the offset of the first byte at fault. A file holds exactly a valid def: every field of every struct the def reaches, its arrays counted, and the names of the host's objects it refers to (its grids, its meshes, its host modules), which the host binds through a table of assets when it writes and when it reads. Equal defs with equal assets give equal bytes.

```c
MVFX_NODISCARD MVFX_API mvfxFileResult mvfxWriteEffect(const mvfxEffectDef* def, const mvfxEffectAsset* assets, int32_t assetCount, void* bytes, int64_t capacity, int64_t* sizeOut);
```
Writes an effect def as a file. Each grid, mesh and host module the def refers to is named by the first asset of the table that is it (a module by its function and user pointer both), in the order the def first refers to them.  @param def         The def, from mvfxDefaultEffectDef. @param assets      The table of assets; NULL when assetCount is 0. @param assetCount  Its length, at least 0. @param bytes       Receives the file; NULL when capacity is 0. @param capacity    Its size in bytes, at least 0. @param sizeOut     Receives the file's size in bytes, also when the capacity is too small; 0 on another failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def the def validator refuses (mvfxCreateEffect's checks that need no context) or an invalid asset table (a kind past the known ones, a name empty, too long, not UTF-8 or twice a kind); `mvfx_errorMissing` for an object the def refers to that the table lacks; `mvfx_errorCapacity` when the file does not fit in bytes, or is larger than INT32_MAX bytes. The offset is -1. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxFileResult mvfxReadEffect(const void* bytes, int64_t size, const mvfxEffectAsset* assets, int32_t assetCount, void* memory, int64_t capacity, int64_t* neededOut, mvfxEffectDef* defOut);
```
Reads a file into an effect def, its arrays in caller memory. Checks in order the header (its magic, its version, its size), the hash, the unit table, each unit alone, the bindings of the names it holds through the asset table, and the def validator. The bytes must not change during the call: bytes that do are refused or read as some valid def, but never read or laid past.  @param bytes       The file. @param size        Its size in bytes, at least 0. @param assets      The table of assets; NULL when assetCount is 0. @param assetCount  Its length, at least 0. @param memory      Receives the def's arrays, aligned to 16 bytes; NULL when capacity is 0. The def points into it. @param capacity    Its size in bytes, at least 0. @param neededOut   Receives the bytes of memory the def needs, also when the capacity is too small; 0 on another failure. The size is known before the def validator runs, which needs the def in memory, so a file measured may still be refused by the read that follows: check its result. @param defOut      Receives the def, with its cookies; zeroed on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, memory not aligned, an invalid asset table, or a file that is not one (a wrong magic, size or hash, a unit out of place, a count past its cap, a bool neither 0 nor 1, a name not UTF-8 or twice a kind, a reference past its names or out of their order) or holds a def the def validator refuses; `mvfx_errorVersion` for another format version; `mvfx_errorMissing` for a name the table lacks; `mvfx_errorCapacity` when the def does not fit in memory. The offset is the first byte at fault: the version's, a name's, a count's; the effect unit's for a def refused; -1 for a fault outside the file. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxFileResult mvfxCreateEffectFromBytes( mvfxContext* context, const void* bytes, int64_t size, const mvfxEffectAsset* assets, int32_t assetCount, mvfxEffectId* effectOut);
```
Creates an effect from a file: reads it into memory from the context's allocator, creates the effect from the def (mvfxCreateEffect) and frees the memory.  @param context     The context that owns the effect. @param bytes       The file. @param size        Its size in bytes, at least 0. @param assets      The table of assets; NULL when assetCount is 0. @param assetCount  Its length, at least 0. @param effectOut   Receives the effect's id; the null id on failure. @return As mvfxReadEffect, then as mvfxCreateEffect with the offset -1; `mvfx_errorCapacity` also when the allocator fails. A NULL argument counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `gas.h`

Gases: smoke and fire on a grid in a box, by Stam's stable fluids. An effect's gas is a box of cells holding a velocity (at the cells' faces) and density, temperature and fuel (at their centers), stepped with its instance: sources add to it, its velocity carries it and itself, and each field may decay. A host reads its fields to draw them (ray marching, a flipbook bake): the library draws no volume.

```c
mvfxGasDef mvfxDefaultGasDef(void);
```
Returns the default gas def: a 2D gas of 64 by 64 cells of 0.1, in local space, one substep, nothing decaying, no sources, its walls open, 4 V cycles of pressure, semi-Lagrangian; gravity (0, -9.81, 0) in the world, with no buoyancy, no confinement and inert fuel; its surface of emitter 0's particles of radius 0.1, reach 4, thresholds 0.4 and 2.0 and one smoothing pass; a liquid's fill time 1/60 s and courant number 1.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

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
MVFX_NODISCARD MVFX_API mvfxResult mvfxGrid_GetView(const mvfxContext* context, mvfxGridId grid, mvfxGridView* viewOut);
```
Returns a grid as stored. The values stay valid until the grid's next change or destruction; the serial tells a change.  @param context  The context that owns the grid. @param grid     The grid. @param viewOut  Receives the view; zeroed on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument; `mvfx_errorStale` for an id that names no live grid. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxGrid_SetValues(mvfxContext* context, mvfxGridId grid, const float* values, int64_t count);
```
Gives a grid new values for the same points, as an animated field; steps after read them.  @param context  The context that owns the grid. @param grid     The grid. @param values   The values, as in the grid def. @param count    How many: the grid's points times its components. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live grid; `mvfx_errorInvalid` for a NULL context or values, a count not the grid's or a value out of range, which counts as misuse when the context is given (the grid is then unchanged). @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxGrid_SetValuesIn(mvfxContext* context, mvfxGridId grid, const int32_t first[3], const int32_t size[3], const float* values, int64_t count);
```
Gives new values to a box of a grid's points, as a terrain dug or built up in one place; steps after read them. Sleeping rigid particles on a grid collider wake only near the points so changed since the step before, where new values for all of them wake every one within the grid's box.  @param context  The context that owns the grid. @param grid     The grid. @param first    The box's first point along X, Y and Z. @param size     The box's points along X, Y and Z, each at least 1, within the grid from first. @param values   The box's values, components a point, X fastest, then Y, then Z, each as in the grid def. @param count    How many: the box's points times the components. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live grid; `mvfx_errorInvalid` for a NULL context, first, size or values, a box not within the grid, a count not the box's or a value out of range, which counts as misuse when the context is given (the grid is then unchanged). @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `instance.h`

Instances: one playing of an effect, at a place, with a seed. A context's step advances every instance it owns.

```c
mvfxInstanceDef mvfxDefaultInstanceDef(void);
```
Returns the default instance def: no effect yet, at the world's origin, unturned and at scale 1, seeded by its order of creation.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateInstance(mvfxContext* context, const mvfxInstanceDef* def, mvfxInstanceId* instanceOut);
```
Creates an instance of an effect, allocating every particle it can hold in one block from the context's allocator. Its clock starts at 0 and its emitters start spawning with the next step.  @param context      The context that owns the effect and will own the instance. @param def          The def, from mvfxDefaultInstanceDef. @param instanceOut  Receives the instance's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, or an origin, rotation or scale out of range, which counts as misuse when the context is given; `mvfx_errorStale` for an effect id that names no live effect; `mvfx_errorLimit` when the context holds its limit of instances; `mvfx_errorCapacity` when the allocator fails or the block's size does not fit; `mvfx_errorState` for a planned instance of an effect that cannot be planned. @par Thread safety Safe from any thread; the context is used by one thread at a time.

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
Sets where an instance is at the end of the next step. Over that step it moves in a line from where it was (the rotation by normalized interpolation, the scale linearly), and births within it take its place at their ticks; its travel feeds spawning by distance and its velocity what emitters inherit. A teleport moves it at once instead, with no travel and no velocity, and ends every ribbon by the counter (mvfxInstance_BreakRibbons). How births along the way fall depends on how often the host moves it, which is the host's to keep the same.  @param context   The context that owns the instance. @param instance  The instance. @param origin    Its place in the world, each value finite and at most 2^52 in size. @param rotation  A unit quaternion (x, y, z, w), within 0.001 of unit length. @param scale     From MVFX_MIN_SCALE to MVFX_MAX_SCALE. @param teleport  Whether it jumps there. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or a value out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetVisible(mvfxContext* context, mvfxInstanceId instance, bool visible);
```
Tells an instance whether the host sees it, which its off-screen policy reads at each step. The library reports bounds (mvfxInstance_GetBounds) for the host to decide by.  @param context   The context that owns the instance. @param instance  The instance. @param visible   Whether it is seen. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetOffscreen(mvfxContext* context, mvfxInstanceId instance, mvfxOffscreen offscreen, int32_t interval);
```
Changes what an instance does while unseen. Steps it holds back stay held until it steps.  @param context    The context that owns the instance. @param instance   The instance. @param offscreen  The policy. @param interval   For mvfx_offscreenThrottle, every how many steps it steps: from 2 to MVFX_MAX_OFFSCREEN_INTERVAL; read only for that policy. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context or a policy or interval out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Spawn(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxSpawnRequest* request);
```
Queues particles for an emitter of an instance: they are born at the start of the next step, before its other births, each taking a serial and room like any birth (refused, and counted, when the emitter is full). The values are copied: the arrays may be released at once.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def. @param request   The particles. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, an emitter index out of range, a count below 1, a value out of range, or a color or size for an emitter that does not store it, which counts as misuse when the context is given; `mvfx_errorCapacity` when the emitter's host queue lacks the room (none of the particles is queued); `mvfx_errorState` for a planned instance. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetAttachFrames(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxAttachFrame* frames, int32_t count);
```
Sets the frames an emitter's units are held to (mvfx_attachHost) at the end of the next step: each unit's pinned points move from where its frame was at that step's start to where it is at its end over the substeps. The frames are copied; frames past count are no longer set, and units on them are held where they are.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def. @param frames    count frames; NULL only for a count of 0. @param count     From 0 to the emitter's attachFrameCapacity. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context, an emitter index out of range or not held to the host's frames, a count out of range, NULL frames with a count, a position not finite or over 2^52 in size, or a rotation not of length 1 within 1e-3, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_Prewarm(mvfxContext* context, mvfxInstanceId instance, double seconds, double stepSeconds, int32_t maxSteps, double* remainingOut);
```
Advances one instance alone toward a span, in steps of a given length (the last shorter), at most a number of steps a call, so a host can spread a long prewarm over frames: it calls again with the span left until none is. The same calls reach the same state. Spans and steps are rounded once to ticks.  @param context       The context that owns the instance. @param instance      The instance. @param seconds       The span left, from 0 to 1e9. @param stepSeconds   Each step's length, above 0 and at most MVFX_MAX_STEP_SECONDS, one tick at least. @param maxSteps      At least 1. @param remainingOut  Receives the span left, exactly as ticks; may be NULL. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorState` for a planned instance; `mvfx_errorInvalid` for a NULL context or a value out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetColliders(mvfxContext* context, mvfxInstanceId instance, const mvfxCollider* colliders, int32_t count);
```
Sets the colliders of an instance that its emitters collide with when their collision asks for the instance's (a character, a moving door), replacing the last set; the next step reads them. They are copied.  @param context    The context that owns the instance. @param instance   The instance. @param colliders  From 0 to MVFX_MAX_COLLIDERS colliders; NULL when count is 0. @param count      How many. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context, a count out of range or a collider out of range, which counts as misuse when the context is given (the colliders are then unchanged). @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetSceneQueries(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxSceneQuery** queriesOut, int32_t* countOut);
```
Reads the scene queries an emitter of an instance wrote at the end of the last step, which the host answers before the next step. They stay until answered, the next step or a restart.  @param context     The context that owns the instance. @param instance    The instance. @param emitter     The emitter's index in the effect's def. @param queriesOut  Receives the queries (NULL when there are none), the instance's own, read only. @param countOut    Receives how many. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or an emitter out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_AnswerSceneQueries(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxSceneAnswer* answers, int32_t count);
```
Answers an emitter's scene queries, one answer each in their order: each particle takes its answer's plane, or none, for the steps after. The queries are then gone.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def. @param answers   The answers; NULL when count is 0. @param count     How many: the queries' count. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context, an emitter out of range, a count not the queries' or an answer out of range, which counts as misuse when the context is given (no answer is then taken). @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SetDepthImage(mvfxContext* context, mvfxInstanceId instance, const mvfxDepthImage* image);
```
Sets the depth image an instance's emitters collide with when their collision asks for it, until it is set again or cleared (NULL). It is checked once, here; its pixels are borrowed, not copied, and must stay valid while it is set.  @param context   The context that owns the instance. @param instance  The instance. @param image     The image, or NULL for none. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context or an image out of range (a size, a stride, a byte count short of its rows, a format, a projection not finite or not invertible, a place or a turn), which counts as misuse when the context is given (the image set before is then kept). @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WakeBodies(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const float center[3], float radius);
```
Wakes an emitter's sleeping rigid particles (mvfxRigidDef.sleepSpeed) whose reach meets a sphere in its particles' space, now: for what the instance cannot see change (the host's scene answers, a depth image's pixels rewritten in place) or anything the host moved. The emitter's own colliders, the host's colliders and a depth image set again wake its bodies by themselves.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def; an emitter whose particles never sleep is left as it is. @param center    The sphere's center, finite. @param radius    Its radius, 0 or more; INFINITY wakes them all. @return `mvfx_success`; `mvfx_errorStale` for an instance id that names no live instance; `mvfx_errorInvalid` for a NULL argument, an emitter index out of range, a center not finite or a radius below 0 or NaN, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEvents(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxEvent** eventsOut, int32_t* countOut);
```
Reads the events an emitter of an instance wrote in the last step, in the order the step met them: deaths by age at its start, then as particles move (collisions) and are born (births, their collisions), then deaths by kills at its end. They stay until the next step or a restart; a finished instance's step clears them.  @param context    The context that owns the instance. @param instance   The instance. @param emitter    The emitter's index in the effect's def. @param eventsOut  Receives the events (NULL when there are none), the instance's own, read only. @param countOut   Receives how many. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or an emitter out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

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
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetQuality(mvfxContext* context, mvfxInstanceId instance, int32_t* levelOut);
```
Reads the quality level an instance plays at.  @param context   The context that owns the instance. @param instance  The instance. @param levelOut  Receives its level: 0 the full one. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_BreakRibbons(mvfxContext* context, mvfxInstanceId instance, int32_t emitter);
```
Ends an emitter's current ribbon, so its next births begin another: advances its ribbon counter (mvfx_ribbonOne), as a teleport does for every emitter drawn as ribbons. Emitters not drawn as ribbons are left alone.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def, or -1 for all. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL context or an emitter out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxContext_Step(mvfxContext* context, double seconds);
```
Advances every instance of the context by a span of time: each emitter spawns what it owes by the new time, each particle ages from its birth, and particles whose life has ended are removed. The span is rounded once to ticks (MVFX_TICKS_PER_SECOND), so spans whose ticks sum to the same clock reach the same state.  @param context  The context. @param seconds  The span, from 0 to MVFX_MAX_STEP_SECONDS. @return `mvfx_success`, or `mvfx_errorInvalid` for a NULL context or a span out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time. Its work runs on the host's task hooks when the context has them.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxContext_StepTicks(mvfxContext* context, int64_t ticks);
```
Advances every instance of the context by a number of ticks, as mvfxContext_Step does without the rounding.  @param context  The context. @param ticks    The span in ticks, from 0 to MVFX_MAX_STEP_SECONDS seconds' worth. @return `mvfx_success`, or `mvfx_errorInvalid` for a NULL context or a span out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time. Its work runs on the host's task hooks when the context has them.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetGasInfo(const mvfxContext* context, mvfxInstanceId instance, int32_t gas, mvfxGasInfo* infoOut);
```
Reads what a gas of an instance is now.  @param context  The context that owns the instance. @param instance The instance. @param gas      The gas's index in its effect's def. @param infoOut  Receives the gas's info; zeroed on failure. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or a gas index out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ReadGas(const mvfxContext* context, mvfxInstanceId instance, int32_t gas, mvfxGasField field, float* out, int64_t count);
```
Copies a field of a gas of an instance at its cells' centers, X fastest, then Y, then Z; a velocity's component the mean of the two faces' about the center.  @param context  The context that owns the instance. @param instance The instance. @param gas      The gas's index in its effect's def. @param field    The field. @param out      Receives count values. @param count    How many: the gas's cells. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, a gas index or field out of range (a surface's only field its distance, which no other gas has) or a count not the gas's cells, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_PushWater(mvfxContext* context, mvfxInstanceId instance, int32_t water, const mvfxWaterPush* pushes, int32_t count);
```
Pushes a water surface of an instance at the start of the instance's next step, after the pushes already given for it (at most MVFX_MAX_WATER_PUSHES in all); each push's cells sink by depth times its falloff at their centers, and what they sank raises every cell evenly (the water a body displaces lifts the level: the surface's volume stays).  @param context   The context that owns the instance. @param instance  The instance. @param water     The surface's index in its effect's def. @param pushes    count pushes; NULL for 0. @param count     How many, from 0. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorCapacity` for more pushes than the step holds, with none of these taken; `mvfx_errorInvalid` for a NULL context or pushes with a count, a surface index out of range, a negative count or a push out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetWaterInfo(const mvfxContext* context, mvfxInstanceId instance, int32_t water, mvfxWaterInfo* infoOut);
```
Reads what a water surface of an instance is now.  @param context  The context that owns the instance. @param instance The instance. @param water    The surface's index in its effect's def. @param infoOut  Receives the surface's info; zeroed on failure. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or a surface index out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time.

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

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_HullCenter(mvfxContext* context, mvfxMeshId mesh, float centerOut[3]);
```
Gives the centroid of the convex hull of a mesh's vertices, the place of a rigid hull particle (mvfx_rigidHull) in the mesh's frame: a host draws the mesh at the particle's pose, scaled by its size and moved by minus this centroid scaled likewise.  @param context    The context that owns the mesh. @param mesh       The mesh. @param centerOut  Receives x, y and z. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live mesh; `mvfx_errorInvalid` for a NULL argument or vertices that span no volume or whose hull has more than MVFX_MAX_HULL_VERTICES vertices, which counts as misuse when the context is given; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxMesh_HullVertexCount(mvfxContext* context, mvfxMeshId mesh, int32_t* countOut);
```
Counts the vertices of the convex hull of a mesh's vertices, past the MVFX_MAX_HULL_VERTICES a rigid hull particle takes: an editor tells an artist by how much to simplify a mesh refused for one.  @param context   The context that owns the mesh. @param mesh      The mesh. @param countOut  Receives the count; 0 on failure, so an editor given `mvfx_errorLimit` reports "more than MVFX_MAX_HULL_COUNT", never the 0. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live mesh; `mvfx_errorInvalid` for a NULL argument or vertices that span no volume, which counts as misuse when the context is given; `mvfx_errorLimit` for a hull past what a count holds, never fewer than MVFX_MAX_HULL_COUNT vertices (some hulls of a few more are still counted); `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `output.h`

Output: what the host draws, as data in its own layout. The library draws nothing; after a step the host asks, per view and per emitter, for records written into its buffers.

```c
mvfxLayoutDef mvfxDefaultLayoutDef(void);
```
Returns the default layout def: no fields yet.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateLayout(mvfxContext* context, const mvfxLayoutDef* def, mvfxLayoutId* layoutOut);
```
Creates a layout: checks the def and copies its fields.  @param context    The context that will own the layout. @param def        The def, from mvfxDefaultLayoutDef. @param layoutOut  Receives the layout's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, a count, stride, field, format, component count, offset or overlap out of range, which counts as misuse when the context is given; `mvfx_errorLimit` when the context holds its limit of layouts; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyLayout(mvfxContext* context, mvfxLayoutId layout);
```
Destroys a layout.  @param context  The context that owns the layout. @param layout   The layout. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live layout; `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxLayout_IsValid(const mvfxContext* context, mvfxLayoutId layout);
```
Returns whether an id names a live layout of the context. A stale id is not misuse.  @param context  The context. @param layout   Any id. @return Whether the layout is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetSortScratchBytes(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, int64_t* bytesOut);
```
Returns the bytes of scratch an emitter's output takes, enough for any step: 16 a particle of its capacity when it sorts by place or is drawn as ribbons, else 0.  @param context   The context that owns the instance. @param instance  The instance. @param emitter   The emitter's index in the effect's def. @param bytesOut  Receives the bytes. @return `mvfx_success`; `mvfx_errorStale` for an instance that names nothing live; `mvfx_errorInvalid` for a NULL context or out parameter or an emitter out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteParticles( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, void* scratch, int64_t scratchBytes, void* records, int64_t capacity, int64_t* countOut);
```
Writes a record in a layout for each live particle of an emitter for a view, in the order its def asks (mvfxSortMode): the pool's, its reverse, or by distance or depth from the view's eye through a stable radix sort, so equal keys keep the pool's order. A buffer too small takes the first capacity records of that order; the count is always the emitter's live particles.  @param context       The context that owns the instance and the layout. @param instance      The instance. @param emitter       The emitter's index in the effect's def. @param layout        The layout. @param view          The view. @param scratch       Memory for a sort by place, aligned to 4 bytes, of at least 16 bytes a live particle (mvfxInstance_GetSortScratchBytes gives enough for any step); NULL when scratchBytes is 0. Its contents are not kept. @param scratchBytes  Its bytes, at least 0. @param records       The buffer, capacity times the layout's stride bytes; NULL when capacity is 0. @param capacity      The records it holds, at least 0. @param countOut      Receives the records there are. @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout that names nothing live; `mvfx_errorCapacity` for scratch too small for a sort by place, with nothing written; `mvfx_errorInvalid` for a NULL context, view or count, a view out of range, an emitter out of range, a negative capacity or scratch size, a NULL or misaligned buffer with a size, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs (calls that only read a stepped context and write the host's buffers) may run on several threads at once, each with its own scratch.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteRibbons( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, const mvfxRibbonBuffers* buffers, int64_t* pointCountOut, int64_t* ribbonCountOut);
```
Writes an emitter's ribbons for a view: each run of its live particles with one ribbon id and at least two particles, oldest first, in the order of their ids. Each point is a record of the layout, its ribbon fields (tangent, width, length, coordinate along, ribbon) filled; its colour is times the ribbon's colour over length. Buffers too small take the first ribbons that fit whole, so the list never names a point not written; the counts are always all of them, 0 for an emitter not drawn as ribbons.  @param context         The context that owns the instance and the layout. @param instance        The instance. @param emitter         The emitter's index in the effect's def. @param layout          The points' layout. @param view            The view. @param buffers         The buffers. @param pointCountOut   Receives the points there are. @param ribbonCountOut  Receives the ribbons there are. @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout that names nothing live; `mvfx_errorCapacity` for scratch too small, with nothing written; `mvfx_errorInvalid` for a NULL context, view, buffers or count, a view out of range, an emitter out of range, a negative size or a NULL or misaligned buffer with a size, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once, each with its own scratch.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandSprites( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* countOut);
```
Expands an emitter's particles for a view into quads, for renderers that cannot draw instances: four vertices a particle (left bottom, right bottom, left top, right top; records of the layout with their corner fields, at the corners' places) and six indices (two triangles, counterclockwise seen from the side the quad faces), in the order its def sorts, each quad its size's X wide and Y high about the particle and turned as its sprite def's billboard says. Buffers too small take the first whole quads; the count is always the live particles.  @param context     The context that owns the instance and the layout. @param instance    The instance. @param emitter     The emitter's index in the effect's def. @param layout      The vertices' layout. @param view        The view. @param buffers     The buffers. @param countOut    Receives the quads there are. @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout that names nothing live; `mvfx_errorCapacity` for scratch too small for the emitter's sort, with nothing written; `mvfx_errorInvalid` for a NULL context, view, buffers or count, a view out of range, an emitter out of range, a negative size or a NULL or misaligned buffer with a size, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once, each with its own scratch.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandRibbons( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* vertexCountOut, int64_t* indexCountOut);
```
Expands an emitter's ribbons for a view into triangle strips, for renderers that cannot expand them in a shader: each point (as mvfxInstance_WriteRibbons makes them, tessellated) two vertices across its width along its side (corner 0 on the side's back, 1 on it, the coordinate along as the corner's first), and each segment six indices (two triangles, counterclockwise seen from its face normal's side). Buffers too small take the first whole ribbons that fit both; the counts are always all of them, 0 for an emitter not drawn as ribbons.  @param context         The context that owns the instance and the layout. @param instance        The instance. @param emitter         The emitter's index in the effect's def. @param layout          The vertices' layout. @param view            The view. @param buffers         The buffers; scratch as for mvfxInstance_WriteRibbons. @param vertexCountOut  Receives the vertices there are. @param indexCountOut   Receives the indices there are. @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout that names nothing live; `mvfx_errorCapacity` for scratch too small, with nothing written; `mvfx_errorInvalid` for a NULL context, view, buffers or count, a view out of range, an emitter out of range, a negative size or a NULL or misaligned buffer with a size, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once, each with its own scratch.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandTubes( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, const mvfxTubeDef* tube, const mvfxExpandBuffers* buffers, int64_t* vertexCountOut, int64_t* indexCountOut);
```
Expands an emitter's ribbons for a view into tubes, for chains and trails drawn round: each point (as mvfxInstance_WriteRibbons makes them, tessellated) a ring of sides + 1 vertices at half its width about it (the last at the first's place, so the texture's seam closes), turned along the ribbon by rotation-minimizing frames (Wang et al. 2008) from a normal across its first tangent, the face normal outward, the side around, the corner the coordinate along and the share around; each segment sides quads, six indices each, counterclockwise seen from outside. With caps, after a ribbon's rings, its start and end each a center and a ring (sides + 2 vertices facing back along and along the tangent, corners a disc's) and sides triangles. Buffers too small take the first whole ribbons that fit both; the counts are always all of them, 0 for an emitter not drawn as ribbons.  @param context         The context that owns the instance and the layout. @param instance        The instance. @param emitter         The emitter's index in the effect's def. @param layout          The vertices' layout. @param view            The view. @param tube            The tubes' sides and caps. @param buffers         The buffers; scratch as for mvfxInstance_WriteRibbons. @param vertexCountOut  Receives the vertices there are. @param indexCountOut   Receives the indices there are. @return As mvfxInstance_ExpandRibbons; `mvfx_errorInvalid` too for a NULL tube or its sides out of range. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once, each with its own scratch.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WritePlexus( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, const mvfxRibbonBuffers* buffers, int64_t* pointCountOut, int64_t* ribbonCountOut);
```
Writes an emitter's plexus links for a view, as two-point ribbons: each link the step found (the order it found them) is a ribbon of its two particles' records, owner first, its ribbon fields filled as a ribbon's (straight, the coordinate along 0 and 1, facing the eye, the width the plexus width; its id the link's index), its colour's alpha times the fade when the plexus fades. Buffers too small take the first links that fit whole; the counts are always all of them, 0 for an emitter without plexus links. Scratch is not used.  @param context         The context that owns the instance and the layout. @param instance        The instance. @param emitter         The emitter's index in the effect's def. @param layout          The points' layout. @param view            The view. @param buffers         The buffers. @param pointCountOut   Receives the points there are. @param ribbonCountOut  Receives the links there are. @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout that names nothing live; `mvfx_errorInvalid` for a NULL context, view, buffers or count, a view out of range, an emitter out of range, a negative size or a NULL or misaligned buffer with a size, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandPlexus( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* vertexCountOut, int64_t* indexCountOut);
```
Expands an emitter's plexus links for a view into triangle strips, as mvfxInstance_ExpandRibbons expands ribbons: each link four vertices and six indices. Buffers too small take the first whole links that fit both; the counts are always all of them. Scratch is not used.  @param context         The context that owns the instance and the layout. @param instance        The instance. @param emitter         The emitter's index in the effect's def. @param layout          The vertices' layout. @param view            The view. @param buffers         The buffers. @param vertexCountOut  Receives the vertices there are. @param indexCountOut   Receives the indices there are. @return As mvfxInstance_WritePlexus. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ExpandSheets( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxLayoutId layout, const mvfxView* view, const mvfxExpandBuffers* buffers, int64_t* vertexCountOut, int64_t* indexCountOut);
```
Expands an emitter's sheets (mvfx_unitSheet) into meshes, oldest first: each sheet a vertex a point, row after row (records of the layout with their tangent across, side down and face normal from the neighboring points, their corner the place across and row over the last of each, their ribbon the sheet's index), and each quad between rows not torn (none of its edges, mvfxUnitDef.tearStrain) six indices (two triangles, counterclockwise seen from the face normal's side). Buffers too small take the first whole sheets that fit both; the counts are always all of them, 0 for an emitter of other units. Scratch is not used.  @param context         The context that owns the instance and the layout. @param instance        The instance. @param emitter         The emitter's index in the effect's def. @param layout          The vertices' layout. @param view            The view. @param buffers         The buffers. @param vertexCountOut  Receives the vertices there are. @param indexCountOut   Receives the indices there are. @return `mvfx_success`; `mvfx_errorStale` for an instance or a layout that names nothing live; `mvfx_errorInvalid` for a NULL context, view, buffers or count, a view out of range, an emitter out of range, a negative size or a NULL or misaligned buffer with a size, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteLights( mvfxContext* context, mvfxInstanceId instance, int32_t emitter, const mvfxView* view, mvfxLight* lights, int64_t capacity, int64_t* countOut);
```
Writes the lights an emitter's particles carry (mvfxLightDef: every n-th particle by serial), in the pool's order (oldest first), for a view (positions less its origin). A buffer too small takes the first capacity, so a host that wants at most n lights passes n; the count is always all of them, 0 for an emitter without lights.  @param context     The context that owns the instance. @param instance    The instance. @param emitter     The emitter's index in the effect's def. @param view        The view. @param lights      The buffer; NULL when capacity is 0. @param capacity    The lights it holds, at least 0. @param countOut    Receives the lights there are. @return `mvfx_success`; `mvfx_errorStale` for an instance that names nothing live; `mvfx_errorInvalid` for a NULL context, view or count, a view out of range, an emitter out of range, a negative capacity or a NULL buffer with a capacity, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetBounds(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxBounds* boundsOut);
```
Reads an emitter's bounds in the world after the last step: its fixed box through the instance's transform, or the box of its live particles (measured at the step's end, grown on every side by the largest size component any of them can have over its life: its stored size times the most its size over life scales it); with emitter -1, the box around every emitter's and every gas's box.  @param context    The context that owns the instance. @param instance   The instance. @param emitter    The emitter's index in the effect's def, or -1. @param boundsOut  Receives the bounds. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument or an emitter out of range, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ReadGasAs(const mvfxContext* context, mvfxInstanceId instance, int32_t gas, const mvfxGasRead* read, void* out, int64_t bytes);
```
Writes a field of a gas of an instance over a box of its cells in a format, as the last step left it, X fastest, then Y, then Z: for a volume texture, or a region of one.  @param context   The context that owns the instance. @param instance  The instance. @param gas       The gas's index in its effect's def. @param read      What to read and how. @param out       Receives the values. @param bytes     out's size in bytes: at least the last value's end. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, a gas index, field, format, box, range or pitch out of range (a surface's only field its distance, which no other gas has), or bytes too few, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SampleGas(const mvfxContext* context, mvfxInstanceId instance, int32_t gas, mvfxGasField field, const double* points, int64_t count, float* out);
```
Samples a field of a gas of an instance at points in the world, as the last step left it: trilinear, as particles following the gas read it, 0 outside its box (a surface's distance its reach; a 2D gas's reaching along its Z without end); a velocity's component along the box's axis.  @param context   The context that owns the instance. @param instance  The instance. @param gas       The gas's index in its effect's def. @param field     The field. @param points    count points, x, y and z each, finite. @param count     How many, from 0 to INT64_MAX / 3; points and out may be NULL for 0. @param out       Receives count values. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, a gas index or field out of range, a count out of range or a point not finite, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_ReadWater(const mvfxContext* context, mvfxInstanceId instance, int32_t water, const mvfxWaterRead* read, void* out, int64_t bytes);
```
Writes a field of a water surface of an instance over a box of its cells in a format, as the last step left it.  @param context   The context that owns the instance. @param instance  The instance. @param water     The surface's index in its effect's def. @param read      The field, format, box, range and pitch. @param out       The buffer. @param bytes     Its bytes, at least the read's. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, a surface index, field, format, box, range or pitch out of range, or bytes too few, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_SampleWater(const mvfxContext* context, mvfxInstanceId instance, int32_t water, const double* points, int64_t count, mvfxWaterSample* out);
```
Samples a water surface of an instance at points in the world, as the last step left it: its heights, rates and normals bilinear between the cells' centers, held at the outer cells' within the grid's edge.  @param context   The context that owns the instance. @param instance  The instance. @param water     The surface's index in its effect's def. @param points    count points, x, y and z each, finite. @param count     How many, from 0 to INT64_MAX / 3; points and out may be NULL for 0. @param out       Receives count samples. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance; `mvfx_errorInvalid` for a NULL argument, a surface index out of range, a count out of range or a point not finite, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxContext_OrderEmitters(mvfxContext* context, const double eye[3], const mvfxFrustum* frustum, mvfxDrawItem* items, int64_t capacity, int64_t* countOut);
```
Lists a view's emitters to draw, in the order to draw them: every live instance's emitters with particles (and, with a frustum, bounds not wholly outside one of its planes), by their effect's draw priority, lower first, then farther first, then by instance slot and emitter index, so the order is fixed for given inputs. A buffer too small takes the first capacity of that order; the count is always all of them.  @param context   The context. @param eye       The view's eye in the world (each coordinate at most 2^52 in size). @param frustum   Its frustum, or NULL to keep every emitter; from 0 to MVFX_MAX_FRUSTUM_PLANES finite planes. @param items     The buffer; NULL when capacity is 0. @param capacity  The items it holds, at least 0. @param countOut  Receives how many emitters there are to draw. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context, eye or count, a frustum out of range, a negative capacity or a NULL buffer with a capacity, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetSurfaceMeshScratchBytes( mvfxContext* context, mvfxInstanceId instance, int32_t gas, int64_t* bytesOut);
```
Returns the bytes of scratch a surface gas's mesh takes: 32 a cell of a layer of its box (X by Y) in 3D, 24 a cell of a row in 2D.  @param context   The context that owns the instance. @param instance  The instance. @param gas       The surface gas's index in its effect's def. @param bytesOut  Receives the bytes. @return `mvfx_success`; `mvfx_errorStale` for an instance that names nothing live; `mvfx_errorInvalid` for a NULL context or out parameter, or a gas out of range or not a surface, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_WriteSurfaceMesh( mvfxContext* context, mvfxInstanceId instance, int32_t gas, const double origin[3], float level, const mvfxSurfaceMeshBuffers* buffers, int64_t* vertexCountOut, int64_t* indexCountOut);
```
Writes the mesh of a surface gas at a level of its distance (0 for its surface), as the last step left it, by marching cubes (marching squares for a 2D surface) between its cells' centers: a vertex on each edge where the distance crosses the level, by linear interpolation, shared by the cubes about it and numbered as the walk (Z, then Y, then X) first meets it, and its normal the distance's gradient there, out of the liquid; a face whose diagonal corners are alike is decided by the asymptotic decider (Nielson and Hamann 1991), the same from either cube, so the mesh has no cracks; a cube whose loop no triangles can close off its faces takes a vertex inside it, at the mean of the loop's points, its normal the mean of theirs. A sample exactly at the level is outside; the edges from it take its own vertex there, and a primitive whose vertices are not all distinct is left out, so no edge has zero length. A mesh has fewer than 2^24 vertices. Triangles turn counterclockwise seen from outside; a 2D surface's segments have the outside on their right. Positions are in the world less origin. The mesh's first triangles (or segments) that fit both buffers are written; the counts are always the whole mesh's. The same state gives the same bytes on every platform.  @param context         The context that owns the instance. @param instance        The instance. @param gas             The surface gas's index in its effect's def. @param origin          Subtracted from positions (each coordinate at most 2^52 in size). @param level           The distance meshed, finite. @param buffers         The buffers. @param vertexCountOut  Receives the mesh's vertices. @param indexCountOut   Receives the mesh's indices. @return `mvfx_success`; `mvfx_errorStale` for an instance that names nothing live; `mvfx_errorCapacity` for scratch too small, with nothing written; `mvfx_errorInvalid` for a NULL context, origin, buffers or count, a gas out of range or not a surface, an origin or level out of range, a negative size or a NULL or misaligned buffer with a size, which counts as misuse when the context is given. @par Thread safety Safe from any thread; the context is used by one thread at a time, except that outputs may run on several threads at once, each with its own scratch.

## `plan.h`

Plans: an instance whose particles are simulated elsewhere, on the GPU part or the host's own GPU path. A planned instance holds no particles; each step advances its clocks, counters, cursors and anchors and writes each emitter's plan: the births the step owes, with their serials, the ticks they are born at and the ticks they die at, all from the CPU path's own arithmetic, so a simulation elsewhere numbers, draws and ends its particles as the CPU path does. Which births have room is for the simulation to decide: only it knows the particles it holds.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxEffect_GetEmitterView(const mvfxContext* context, mvfxEffectId effect, int32_t emitter, mvfxEmitterView* viewOut);
```
Returns an emitter of an effect as compiled.  @param context  The context. @param effect   The effect. @param emitter  The emitter's index in the effect. @param viewOut  Receives the view; zeroed on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or an emitter out of range; `mvfx_errorStale` for an effect that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterPlan(const mvfxContext* context, mvfxInstanceId instance, int32_t emitter, mvfxEmitterPlan* planOut);
```
Returns an emitter's plan for the context's last step. The births stay valid until the context's next step or the instance's destruction.  @param context   The context. @param instance  A planned instance. @param emitter   The emitter's index in its effect. @param planOut   Receives the plan; zeroed on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or an emitter out of range; `mvfx_errorStale` for an instance that is gone; `mvfx_errorState` for an instance that is not planned. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `value.h`

Value sources: how a particle's initial values are drawn. Every source is a base times a uniform draw between min and max: the base is 1, a curve or a gradient over the emitter's age, a gradient at a random point, or a parameter of the instance.

```c
static inline mvfxScalarSource mvfxRangeSource(float min, float max);
```
Returns a scalar source uniform between min and max.  @param min  The least value. @param max  The greatest value. @return The source. @par Thread safety Safe from any thread.

## `water.h`

Water surfaces: heights over a grid of cells across X and Z of an instance's frame, along its Y, as heightfield colliders lie. A surface of waves (ripples, wakes, rain) carries each cell's displacement from its rest level and its rate: the wave equation, its waves at the def's speed. The host pushes it (instance.h), reads its fields into its own formats and samples it at points (output.h: floating things, cameras, sound); it draws the grid itself.

```c
mvfxWaterDef mvfxDefaultWaterDef(void);
```
Returns the default water def: 64 by 64 cells of 0.1 at the instance's origin, in local space, its walls closed, waves at 1 a second, damped at 0.5 a second; for shallow water, gravity 9.81, no friction, a flat floor, no sources, dry.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

## The `maul-vfx-rhi` part

### `gpu.h`

The GPU part, the optional target maul-vfx-rhi: effects simulated with Maul RHI on the host's device. A GPU object holds the part's kernels, whose pipelines the device builds asynchronously: the host passes the device's notifications to mvfxGpu_Notify until mvfxGpu_IsReady. The core never depends on it; it is a static library, whose functions MVFX_RHI_API marks.

```c
mvfxGpuDef mvfxDefaultGpuDef(void);
```
Returns the default GPU def: the C library's allocation, no device.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxCreateGpu(const mvfxGpuDef* def, mvfxGpu** gpuOut);
```
Creates a GPU object: the part's kernels as shaders, their pipelines requested from the device, built asynchronously.  @param def     The def, from mvfxDefaultGpuDef, naming a device. @param gpuOut  Receives the object; NULL on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie or a device, or an allocator with one of its two functions alone; `mvfx_errorCapacity` when the allocator fails; `mvfx_errorState` when the device refuses a kernel's shader or pipeline. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
void mvfxDestroyGpu(mvfxGpu* gpu);
```
Destroys a GPU object: its pipelines and shaders. No frame of the device may still use them.  @param gpu  The object; NULL does nothing. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
bool mvfxGpu_Notify(mvfxGpu* gpu, const mrhiDeviceNotification* notification);
```
Takes a device notification: a pipeline of the object built, or failed to build.  @param gpu           The object. @param notification  A notification from mrhiNextDeviceNotification. @return Whether it answered one of the object's requests (another owner's notifications are the host's to route). @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
bool mvfxGpu_IsReady(const mvfxGpu* gpu);
```
Returns whether every kernel's pipeline is built.  @param gpu  The object. @return Whether it is ready; false for NULL or a pipeline that failed. @par Thread safety Safe from any thread; the device is used by one thread at a time.

### `sim.h`

A planned instance simulated on the GPU: made once from a ready GPU object, a context and an instance planned in it (maul-vfx/plan.h), it holds each emitter's particles in device buffers. Each frame, after the context's step, mvfxGpuSim_AddPasses adds its passes to the frame the host builds, and mvfxGpuSim_Record records them once the frame is compiled. Nothing is read back in a step: each emitter's statistics record (its count and bounds) is the host's to read back, as late as its frames lag, and mvfxGpuSim_BoundsOf maps it to the world. What it does not run yet is refused when it is made.

```c
mvfxGpuSimDef mvfxDefaultGpuSimDef(void);
```
Returns the default simulation def: the C library's allocation, no GPU object, context or instance.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxCreateGpuSim(const mvfxGpuSimDef* def, mvfxGpuSim** simOut, mvfxGpuRefusal* refusalOut);
```
Makes a simulation of a planned instance: each emitter's buffers on the GPU object's device.  @param def         The def, from mvfxDefaultGpuSimDef. @param simOut      Receives the simulation; NULL on failure. @param refusalOut  Optional: receives why it was refused, an emitter of -1 otherwise. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, a GPU object that is not ready, an allocator with one of its two functions alone, or an instance that is not planned; `mvfx_errorStale` for an instance or effect that is gone; `mvfx_errorState` for an effect it does not run yet (refusalOut says why) or when the device refuses a buffer; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
void mvfxDestroyGpuSim(mvfxGpuSim* sim);
```
Destroys a simulation: its buffers. No frame of the device may still use them.  @param sim  The simulation; NULL does nothing. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_HostGridBytes(const mvfxContext* context, const mvfxCollider* colliders, int32_t count, int64_t* bytesOut);
```
Counts the bytes of the grids a set of the instance's colliders reads, the room mvfxGpuSimDef.hostGridBytes must keep for them: each heightfield's and distance grid's grid once, its bytes (mvfxGridView) rounded up to 4.  @param context    The context that owns the grids. @param colliders  The colliders, as for mvfxInstance_SetColliders; NULL when count is 0. @param count      How many, 0 to MVFX_MAX_COLLIDERS. @param bytesOut   Receives the bytes; 0 on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context or bytesOut, a count out of range or NULL colliders with a count; `mvfx_errorStale` for a grid that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_SetDepthTextures(mvfxGpuSim* sim, mrhiResourceId depth, mrhiResourceId normals);
```
Gives the textures of the frame open that the next mvfxGpuSim_AddPasses binds for the emitters whose collision meets the instance's depth image (mvfxInstance_SetDepthImage, which may come without pixels for a planned instance): the depth the host rendered, a `Depth32Float` or `DepthStencil` texture of the image's size whose depth aspect is read, nearest pixel; and optionally the surface's normals in the view's space, an `Rgba32Float` or `Rgba16Float` texture of the same size, read likewise (a null id to make them from the depth). Each is read where the step's passes run; mvfxGpuSim_Record lets go of them.  @param sim      The simulation. @param depth    The depth texture's id in the frame, or a null id. @param normals  The normal texture's id in the frame, or a null id (ignored without a depth texture). @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddPasses(mvfxGpuSim* sim);
```
Adds the passes of the context's last step to the frame the host builds (after mrhiBeginFrame, before mrhiCompileFrame): a transfer pass and seven compute passes an emitter. It reads the instance's plans, which must stay until mvfxGpuSim_Record. A step whose instance's grids pass the room the def keeps adds no pass: the particles stay as they were, the step's plans unsimulated.  @param sim  The simulation. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation; `mvfx_errorStale` for an instance that is gone; `mvfx_errorState` when the device refuses a pass or an import (no frame open), or when an emitter meets the instance's depth image and no depth texture was given (mvfxGpuSim_SetDepthTextures); `mvfx_errorCapacity` when the grids the instance's colliders read pass the def's hostGridBytes. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_Record(mvfxGpuSim* sim);
```
Records the passes mvfxGpuSim_AddPasses added, after the frame is compiled: the plans' bytes written, the kernels dispatched. Nothing when it added none. The bytes are the frame's uploads: 1,400 an emitter, 432 a force and 128 a collider it has (6,904 at most), 32 a planned birth (a plan holds at most twice the emitter's capacity), and the values of each grid its forces and colliders read (mvfxGridView's bytes) at the first step, after each change and, for the instance's, after each move in the room the def keeps, which the device's frameUploadBytes must hold.  @param sim  The simulation. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation; `mvfx_errorState` when the device refuses a command, such as an upload past the frame's. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_GetParticles(const mvfxGpuSim* sim, int32_t emitter, mvfxGpuParticles* particlesOut);
```
Returns an emitter's particles: the buffer the last recorded step left them in, which the host imports to draw or read them.  @param sim           The simulation. @param emitter       The emitter's index in its effect. @param particlesOut  Receives the buffers; zeroed on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or an emitter out of range. @par Thread safety Safe from any thread; the device is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_BoundsOf(const mvfxGpuSim* sim, int32_t emitter, const uint32_t* record, mvfxBounds* boundsOut);
```
Maps an emitter's statistics record, read back from its buffer any frames after its step, to its bounds in the world as mvfxInstance_GetBounds gives the CPU path's: its fixed box through the instance's transform at that step; else empty with no particles, or the box of its places at that step's end grown by the largest size any can have, through the map to the world at that step. The record holds that step's map, so a late read is exact for its step.  @param sim        The simulation. @param emitter    The emitter's index in its effect. @param record     The record, MVFX_GPU_STATISTICS_WORDS words. @param boundsOut  Receives the bounds; empty on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or an emitter out of range. @par Thread safety Safe from any thread; the device is used by one thread at a time.

---

104 functions across 17 headers.
