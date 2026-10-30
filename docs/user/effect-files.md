# Effect files

An effect def can be written as bytes to keep and ship, and an effect made from them. The bytes are the same on every platform: every integer and every float's bits are little-endian whatever the machine's byte order. Equal defs give equal bytes. With `maul-vfx/file.h`:

```c
mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
emitter.capacity = 64;
emitter.rate = 20.0f;
mvfxEffectDef def = mvfxDefaultEffectDef();
def.emitters = &emitter;
def.emitterCount = 1;

// Its size first, then the bytes.
int64_t size = 0;
mvfxFileResult sized = mvfxWriteEffect(&def, NULL, 0, NULL, 0, &size, NULL);
static unsigned char file[4096];
if (sized.result != mvfx_errorCapacity || size > (int64_t)sizeof file ||
    mvfxWriteEffect(&def, NULL, 0, file, size, &size, NULL).result != mvfx_success)
{
    return false;
}

mvfxContextDef contextDef = mvfxDefaultContextDef();
mvfxContext* context = NULL;
if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
{
    return false;
}
mvfxEffectId effect = {0};
bool made = mvfxCreateEffectFromBytes(context, file, size, NULL, 0, &effect, NULL).result ==
            mvfx_success;

// A byte changed: refused, at an offset in the file.
file[size / 2] ^= 0x40u;
mvfxEffectId damaged = {0};
mvfxFileResult refused =
    mvfxCreateEffectFromBytes(context, file, size, NULL, 0, &damaged, NULL);
made = made && refused.result == mvfx_errorFormat && refused.offset >= 0;
mvfxDestroyContext(context);
return made;
```

## Writing

`mvfxWriteEffect` measures first when its buffer is too small: it answers `mvfx_errorCapacity` with the size the file needs. The def must be one `mvfxCreateEffect` would accept; the checks that need no context run while writing, so a file is never written from a def that could not be made into an effect.

## Reading

A file is read as hostile input. `mvfxReadEffect` checks the header (its magic, its version and its size), a hash over the whole file, each part of it on its own, the names it holds against the program's asset table, and finally the def itself, by the rules `mvfxCreateEffect` applies (a rigid sphere's size Y and Z equal to its X, for one). A refusal comes with `offset`, the first byte to blame, so a tool can point at the damage, and, given an `mvfxEffectRefusal`, the field at fault ([an effect that is refused](troubleshooting.md#an-effect-that-is-refused)). A file written by another format version is refused with `mvfx_errorVersion`.

A file holds only the fields that differ from their defaults, the values `mvfxDefaultEffectDef`, `mvfxDefaultEmitterDef`, `mvfxDefaultGasDef` and `mvfxDefaultWaterDef` give (zero for the items of other arrays): a field a file leaves out is read as its default, so an effect that sets few fields makes a small file. A field the reader does not know, which only a later version of the format writes, is refused with `mvfx_errorVersion` at its offset rather than skipped, so a def is never read as less than what was written.

`mvfxReadEffect` puts the def's arrays into memory the program gives and reports the bytes it needs. `mvfxCreateEffectFromBytes` reads, makes the effect and frees the memory in one call, with the context's allocator.

## Assets

Grids, meshes and host modules are the program's objects, not data a file can hold. A file names them, and the program binds each name to an object with a table of `mvfxEffectAsset` when it writes and when it reads. Writing names each object by the first entry of the table that is it; reading looks each name up and refuses a missing one with `mvfx_errorMissing`. A host module is named by its function and its user pointer together.

## The JSON form

`tools/effect_json.py` converts a file to JSON and back, byte for byte, for diffs, reviews and authoring by hand. The library itself reads only the binary form.

```sh
python3 tools/effect_json.py to-json effect.mvfx effect.json
python3 tools/effect_json.py to-binary effect.json effect.mvfx
```

The JSON holds the fields the file holds, in the file's order, with enums by their names and floats as numbers that read back to the same bits. A field it leaves out takes its default, as in the file, and a field the struct does not have is refused. Grids, meshes and host modules appear as their asset names.

## Versions

`MVFX_FILE_VERSION` is the format a library writes and the one it reads. Before 1.0.0 any release may change it, and a release that does says so in the changelog. Keep the defs, or their JSON form, as the source of an effect, and write the files again with the library a program ships.
