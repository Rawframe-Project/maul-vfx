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
Returns the default context def: the C library's allocator, serial work and 256 effects.  The default is built in the program from the headers it includes, so the version it stamps is the program's.  @return The def, with a valid cookie and version. @par Thread safety Safe from any thread.

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
Returns the default emitter def: 1,024 particles, 64 a second, each living one second, born at the emitter's origin.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
mvfxEffectDef mvfxDefaultEffectDef(void);
```
Returns the default effect def, with no emitters yet.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateEffect(mvfxContext* context, const mvfxEffectDef* def, mvfxEffectId* effectOut);
```
Creates an effect from a def: checks every field of it and of its emitters, then compiles it, allocating its compiled form in one block from the context's allocator.  @param context    The context that owns the effect. @param def        The def, from mvfxDefaultEffectDef. @param effectOut  Receives the effect's id; the null id on failure. @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def or an emitter def without its cookie, or a field out of its range, which counts as misuse when the context is given; `mvfx_errorLimit` when the context holds its limit of effects; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxDestroyEffect(mvfxContext* context, mvfxEffectId effect);
```
Destroys an effect and returns its memory to the context's allocator.  @param context  The context that owns the effect. @param effect   The effect. @return `mvfx_success`; `mvfx_errorStale` for an id that names no live effect (the null id included); `mvfx_errorInvalid` for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool mvfxEffect_IsValid(const mvfxContext* context, mvfxEffectId effect);
```
Returns whether an id names a live effect of the context. A stale id is not misuse.  @param context  The context. @param effect   Any id. @return Whether the effect is alive. @par Thread safety Safe from any thread; the context is used by one thread at a time.

---

12 functions across 4 headers.
