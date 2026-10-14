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
Returns the default context def: the C library's allocator and serial work.  The default is built in the program from the headers it includes, so the version it stamps is the program's.  @return The def, with a valid cookie and version. @par Thread safety Safe from any thread.

```c
MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateContext(const mvfxContextDef* def, mvfxContext** contextOut);
```
Creates a context.  @param def         The def, from mvfxDefaultContextDef. @param contextOut  Receives the context; set to NULL on failure. @return `mvfx_success`; `mvfx_errorVersion` for a def stamped with another major or minor version than the library's, checked before anything else of it; `mvfx_errorInvalid` for a NULL argument, a def without its cookie, an allocator with one function or a task system with one hook; `mvfx_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void mvfxDestroyContext(mvfxContext* context);
```
Destroys a context and everything it owns.  @param context  The context. NULL does nothing. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
uint64_t mvfxContext_GetMisuse(const mvfxContext* context);
```
Returns how many calls the context refused as misuse: invalid arguments against it. Stale ids are not misuse.  @param context  The context. @return The count since the context was created. @par Thread safety Safe from any thread; the context is used by one thread at a time.

---

7 functions across 2 headers.
