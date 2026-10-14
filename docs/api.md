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
Returns the default context def: the C library's allocator, serial work, 256 effects and 1,024 instances.  The default is built in the program from the headers it includes, so the version it stamps is the program's.  @return The def, with a valid cookie and version. @par Thread safety Safe from any thread.

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
Returns the default emitter def: 1,024 particles, 64 a second without end from the instance's start, no bursts, each living one second, born at the emitter's origin at rest (a point shape, unturned, whole arcs, along the shape's normal at a speed of 0).  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
mvfxEffectDef mvfxDefaultEffectDef(void);
```
Returns the default effect def, with no emitters yet.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateEffect(mvfxContext* context, const mvfxEffectDef* def, mvfxEffectId* effectOut);
```
Creates an effect from a def: checks every field of it and of its emitters, then compiles it, allocating its compiled form in one block from the context's allocator.  @param context    The context that owns the effect. @param def        The def, from mvfxDefaultEffectDef. @param effectOut  Receives the effect's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def or an emitter def without its cookie, a field out of its range or an invalid curve, which counts as misuse when the context is given; `mvfx_errorLimit` when the context holds its limit of effects; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyEffect(mvfxContext* context, mvfxEffectId effect);
```
Destroys an effect and returns its memory to the context's allocator.  @param context  The context that owns the effect. @param effect   The effect. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live effect (the null id included); `mvfx_errorBusy` while an instance plays it; `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxEffect_IsValid(const mvfxContext* context, mvfxEffectId effect);
```
Returns whether an id names a live effect of the context. A stale id is not misuse.  @param context  The context. @param effect   Any id. @return Whether the effect is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `instance.h`

Instances: one playing of an effect, at a place, with a seed. A context's step advances every instance it owns.

```c
mvfxInstanceDef mvfxDefaultInstanceDef(void);
```
Returns the default instance def: no effect yet, at the world's origin, seeded by its order of creation.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateInstance(mvfxContext* context, const mvfxInstanceDef* def, mvfxInstanceId* instanceOut);
```
Creates an instance of an effect, allocating every particle it can hold in one block from the context's allocator. Its clock starts at 0 and its emitters start spawning with the next step.  @param context      The context that owns the effect and will own the instance. @param def          The def, from mvfxDefaultInstanceDef. @param instanceOut  Receives the instance's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def without its cookie or an origin out of range, which counts as misuse when the context is given; `mvfx_errorStale` for an effect id that names no live effect; `mvfx_errorLimit` when the context holds its limit of instances; `mvfx_errorCapacity` when the allocator fails or the block's size does not fit. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyInstance(mvfxContext* context, mvfxInstanceId instance);
```
Destroys an instance and its particles.  @param context   The context that owns the instance. @param instance  The instance. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live instance (the null id included); `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxInstance_IsValid(const mvfxContext* context, mvfxInstanceId instance);
```
Returns whether an id names a live instance of the context. A stale id is not misuse.  @param context   The context. @param instance  Any id. @return Whether the instance is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

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

---

22 functions across 6 headers.
