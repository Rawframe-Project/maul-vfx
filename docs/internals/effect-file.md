# The effect file

How an effect def becomes bytes and back ([src/file.c](../../src/file.c), [src/file_codec.c](../../src/file_codec.c)), and why the format is built so it cannot drift from the def. The user's view is [effect-files.md](../user/effect-files.md).

## Layout

A file is a header, a table of units, and the units:

- **The header** (24 bytes): the magic `MVFX`, the format version (`MVFX_FILE_VERSION`), the file's size, the unit count, and a hash of everything after the header (`mvfxHash64`, its 64-bit words read little-endian).
- **The unit table**: for each of four units, its kind, offset, size and item count.
- **Three units of names**, one for each kind of asset (grids, meshes, host modules), each name UTF-8 with its length.
- **The effect unit**: the def, every field of every struct it reaches, in declaration order. A pointer field is its count then its items; a grid, mesh or host module is an index into its kind's names. The def's cookies are not stored: the reader sets them.

Every integer and every float's bits are little-endian whatever the host's order, so the bytes are the same on every platform, and equal defs with equal assets give equal bytes, which makes files usable as content hashes and in diffs.

## One walk for both directions

The codec has no hand-written reader or writer for each struct. `tools/gen_file.py` reads the public headers and writes [src/file_fields.c](../../src/file_fields.c): a table of each struct's fields (place, kind, element count, nested type) in declaration order. One walk over those tables writes a def, measures one, reads bytes into a def, or checks bytes without reading them. Reading and writing therefore cannot disagree, and a field added to a def is in the file as soon as the generator runs; CI runs it with `--check`, so a header change without the regenerated table fails.

The generator refuses a pointer field it has no count for, so no array is silently left out, and passes its output through clang-format like any source.

## Reading hostile bytes

The reader checks in an order where each check can trust the ones before it: the header (magic, version, size), the hash, the unit table (each unit where it belongs, inside the file), each unit alone (counts within their caps, bools 0 or 1, names well-formed UTF-8 without overlong forms or surrogates, each name once a kind, every reference to a name within the names and in first-use order), then the names bound through the host's asset table, and last the def validator `mvfxCreateEffect` uses. A failure reports the offset of the first byte at fault: the version's, a count's, a name's, or the effect unit's for a def the validator refuses.

The reader lays the def's arrays into memory the host gives, measured first. The bytes must not change during a read, but if they do (a file mapped from a disk another process writes), the walk still never reads or writes past what it measured: it refuses, or reads some valid def.

The magic, the hash and the size reject a file truncated, extended or damaged anywhere. The hash is not cryptographic: it detects accidents, not an attacker, who can recompute it. Hostility is handled by the checks, which assume nothing of the bytes.

## Names, not handles

A grid, a mesh or a host module is the host's object; its address means nothing in another process. The file holds names instead, and the host binds them through an asset table on both sides. The writer names an object by the first asset of the table that is it, in the order the def first refers to them, so the same def and the same table give the same bytes. A host module is named by its function and its user pointer together, since one function may serve several modules.

## Versions

`MVFX_FILE_VERSION` is the only format a library reads; another is refused with `mvfx_errorVersion`, never guessed at. Every other check the reader makes on a file's bytes, a bound included, refuses it with `mvfx_errorFormat` and the offset of the first byte at fault. Any change to the bytes raises it, in the same change: a field added to a def, a field's type, an enum's width. Before 1.0.0 the library promises no reading of older files; the JSON form is the stable source for effects meant to outlive a format.

## The JSON form and the fixtures

`tools/effect_json.py` reads the same headers the generator reads, so its JSON holds exactly the fields the binary form holds; every field is required and none unknown. Its own `format` number moves when a field is renamed (the JSON changes, the bytes do not); a layout change moves both. The golden scene's effects are kept as JSON and binary pairs in `test/fixtures/`, and CI checks each pair converts both ways byte for byte, and that the library writes the same bytes for the same defs.

## Fuzzing

`test/fuzz_file.c` feeds arbitrary bytes to the reader, and `test/fuzz_step.c` steps whatever effects the fuzzed files make, so a def the validator accepts can never crash a step. Their seeds are the golden scene's files.
