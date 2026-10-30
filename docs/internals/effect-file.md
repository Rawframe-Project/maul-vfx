# The effect file

How an effect def becomes bytes and back ([src/file.c](../../src/file.c), [src/file_codec.c](../../src/file_codec.c)), why the format cannot drift from the def, and how it stays readable as the def grows. The user's view is [effect-files.md](../user/effect-files.md).

The effect file is an asset format in the family's sense ([maul-conventions.md](maul-conventions.md), Compatibility): after its header it is made of tagged fields, a reader gives a field that is absent its default and refuses a tag it does not know, and a writer leaves out a field that holds its default.

## Layout

Every integer and every float's bits are little-endian whatever the host's order. The alignment is 8 bytes: every section, field and item starts at a multiple of 8 in the file, after zero bytes, and a reader refuses padding that is not zero. Nothing in the file is read in place (the reader lays the def into memory the host gives) and no scalar is wider than 8 bytes, so 16 is not needed.

- **The header** (24 bytes): the magic `MVFX`, the format version (`MVFX_FILE_VERSION`), the file's size (a multiple of 8), the number of sections, and a hash of everything after the header (`mvfxHash64`, its 64-bit words read little-endian).
- **The section table**: for each section, its tag, offset and length (12 bytes), in ascending tag order, each tag once.
- **The sections**, each where the last ends, the last ending with the file. Tags 1, 2 and 3 hold the names of grids, meshes and host modules: a count, then each name's length and its UTF-8 bytes. A kind of which the def names nothing has no section. Tag 4 is the effect, which every file has.
- **The effect** is the def as a sequence of fields. A field is its 32-bit tag, its payload's 32-bit length and its payload:
  - a scalar or a fixed array of scalars: its bytes, exactly its type's size times its count;
  - a grid, a mesh or a host module (each element of a fixed array of them): a 32-bit index into its kind's names, -1 for none;
  - a nested struct: its own sequence of fields;
  - an array (a pointer and its count) or a fixed array of structs: a 32-bit count and a zero word, then its items: scalars packed, a struct each as its length, a zero word and its sequence of fields. A fixed array's count is its length.

A struct's fields are in ascending tag order, each once, so equal defs with equal assets give equal bytes, which makes files usable as content hashes and in diffs. The def's cookies are not stored: a def read takes them from its defaults.

## Tags

[tools/file-tags.txt](../../tools/file-tags.txt) gives each stored field of each struct the def reaches its tag, numbered within its struct, its wire kind: what its bytes are, such as `f32`, `f32[3]`, `mvfxCurve`, `array mvfxForce` or `grid`, and, for a field that is not a struct, its default: the hex of the bytes a file would write for it, with each place whose default differs named apart (`mvfxCollider.kind` is `00` in an array's items and `03` at `mvfxWaterDef.terrain.kind`). A grid, a mesh or a host module defaults to none (`ffffffff`), an array to no items (a count and a zero word, `0000000000000000`). A tag names a field's bytes and their meaning for good:

- A renamed field keeps its tag.
- **A field whose wire kind or default changes gets a new tag**, and its old tag is retired (`retired N`). A file that leaves a field out means "its default", so a new default read under an old tag would change what every file that left it out means.
- A removed field's tag is retired. A retired tag is never used again.

The validators name a field they refuse by the same tags (`mvfxEffectRefusal`), from constants `gen_file.py` writes into `src/file_tags.h` (`MVFX_TAG_<STRUCT>_<FIELD>`); since a tag keeps its meaning, a refusal kept by a tool names the same field in a later release. A path from `mvfxEffectDef` down to a field has at most `MVFX_MAX_FIELD_DEPTH` steps, which the generator checks.

`tools/gen_file.py` refuses a field without a tag, a tag used twice in a struct, a retired tag used again, an entry that names no field (it must be retired), a field whose wire kind is no longer its entry's, a field without a default, or with one of the wrong length or at a place no def holds, and a field deeper below `mvfxEffectDef` than `MVFX_MAX_FIELD_DEPTH`; `gen_file.py --self-test` checks each refusal.

The suite `test_file_defaults` is what checks the defaults themselves, in C and in every CI cell. `gen_file.py` writes the pins into [test/file_pins.h](../../test/file_pins.h), each field at each place with the tags that reach it. The suite builds each default through its default function (zero bytes for an item of an array of a struct without one), writes each field there with the codec's own field writer (`mvfxPutPayload`), and compares the bytes with the pin; it does the same from the block of defaults the reader lays absent fields from. A mismatch fails and names the tag to retire. The fixtures cannot catch a changed default: a field that holds its default is left out of every file, so its bytes do not change when the default does.

## Defaults

A field the file leaves out takes the value its struct's default holds there: the defs' own defaults (`mvfxDefaultEffectDef`, `mvfxDefaultEmitterDef`, `mvfxDefaultGasDef`, `mvfxDefaultWaterDef`), and for an item of an array of another struct, zero bytes. A nested struct's default is the one its holder's default holds. The writer compares a field's bits with its default's and leaves out a field whose bits are equal, a nested struct all of whose fields are, and an array of no items; no default has items. An emitter that holds its defaults is therefore an empty item, and an effect that sets few fields is small. A reader takes a field that holds its default as any other, so a file written by hand or by the JSON form need not leave defaults out; the library's own writer always does.

## One table for both directions

The codec has no hand-written reader or writer for each struct. `tools/gen_file.py` reads the public headers and the tags and writes [src/file_fields.c](../../src/file_fields.c): a table of each struct's fields (tag, place, kind, element count, nested type, name) in ascending tag order, and the block of defaults, a constant built from the initializers [src/defaults.h](../../src/defaults.h) shares with the default functions. The writer walks a struct's table and puts each field that differs from its default; the reader walks the file's tags against the same table. A field added to a def is refused by the generator until it has a tag, and is in the file once it has one; CI runs the generator with `--check`, so a header change without the regenerated table fails.

The generator refuses a pointer field it has no count for, so no array is silently left out, and passes its output through clang-format like any source.

## Reading hostile bytes

The reader checks in an order where each check can trust the ones before it: the header (magic, version, size, the section count against the file), the hash, the section table (tags ascending and known, each section where the last ends and inside the file, the bytes between them zero, the effect present), each names section alone (its count within its cap, names well-formed UTF-8 without overlong forms or surrogates, each name once a kind), the effect's fields (tags ascending and known, each field's length within its struct and right for its kind, padding and zero words zero, counts within their caps, bools 0 or 1, every reference to a name within the names and in first-use order), then the names bound through the host's asset table, and last the def validator `mvfxCreateEffect` uses. A tag this version does not know, of a section or of a field, is refused with `mvfx_errorVersion`, since only a later version writes one; every other check refuses the file with `mvfx_errorFormat`. A failure reports the offset of the first byte at fault: the version's, a tag's, a length's, a count's, a name's. A fault in the effect's fields also names the field the walk is in (`mvfxEffectRefusal`): the codec keeps the path of tags and items it has entered, and records it with its first fault. A def the validator refuses is reported at the field the validator names: the reader walks the section along the refusal's path to that field's tag, or, for a field the file leaves out, to the head of the nearest struct or item holding it that the file has (the section's own for a refusal of the def as a whole); the walk keeps within the section and each length it reads.

The reader lays the def's arrays into memory the host gives, measured first. The bytes must not change during a read, but if they do (a file mapped from a disk another process writes), the walk still never reads or writes past what it measured: it refuses, or reads some valid def.

The magic, the hash and the size reject a file truncated, extended or damaged anywhere. The hash is not cryptographic: it detects accidents, not an attacker, who can recompute it. Hostility is handled by the checks, which assume nothing of the bytes.

## Names, not handles

A grid, a mesh or a host module is the host's object; its address means nothing in another process. The file holds names instead, and the host binds them through an asset table on both sides. The writer names an object by the first asset of the table that is it, in the order the def first refers to them in the writer's walk, so the same def and the same table give the same bytes. A host module is named by its function and its user pointer together, since one function may serve several modules.

## Versions

`MVFX_FILE_VERSION` is the only format a library reads; another is refused with `mvfx_errorVersion`, never guessed at. Before 1.0.0 it rises with any change to the bytes, in the same change: a field added, a tag retired, a wire kind changed; `gen_file.py` records each version's layout and refuses a layout no version records. From 1.0.0 it no longer changes: fields are added with new tags, and a 1.z reader reads every tag a 1.y wrote. Before 1.0.0 the library promises no reading of older files; the JSON form is the stable source for effects meant to outlive a format.

## The JSON form and the fixtures

`tools/effect_json.py` reads the same headers and tags the generator reads, so its JSON holds exactly the fields the binary form holds, in the same order; a field the JSON leaves out takes its default, and one the struct does not have is refused. Its own `format` number moves when a field is renamed (the JSON changes, the bytes do not); a layout change moves both. The golden scene's effects are kept as JSON and binary pairs in `test/fixtures/`, and CI checks each pair converts both ways byte for byte, and that the library writes the same bytes for the same defs.

## Fuzzing

`test/fuzz_file.c` feeds arbitrary bytes to the reader, and `test/fuzz_step.c` steps whatever effects the fuzzed files make, so a def the validator accepts can never crash a step. A file that reads writes back to bytes that read and write again to themselves. Their seeds are the golden scene's files, and `test/corpus/` holds corpora grown from them.
