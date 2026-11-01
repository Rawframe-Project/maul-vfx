#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Writes src/file_fields.c, the effect file's table of every def field,
src/file_tags.h, its tags as constants, test/file_pins.h, the defaults its
tag registry pins, and test/file_layout.h, the structs as the public
headers lay them out (offsetof and sizeof) for the suites that find fields
in a file's bytes.

The effect section of an effect file holds an mvfxEffectDef as tagged
fields: each struct a sequence of its fields that differ from their
defaults, each field a 32-bit tag, a 32-bit length and its payload. This
reads the structs reachable from mvfxEffectDef from the public headers,
gives each field the tag tools/file-tags.txt records for it, and writes a
table of each struct's fields in ascending tag order (tag, place, kind,
elements, nested type, name), which the codec (src/file_codec.c) walks to
read, write or measure, and mvfxGetEffectFieldPath names a refused field
by. The validators name the field they refuse by its tag, from
src/file_tags.h (MVFX_TAG_<STRUCT>_<FIELD>); a path from mvfxEffectDef
down to a field has at most MVFX_MAX_FIELD_DEPTH steps, which the
generator checks, since an effect refusal holds no more. It also writes the block of defaults an absent field
takes: the defs' own (mvfxDefault*Def) and zero bytes for the items of
other structs. The gate and CI run it with --check.

A field's kind is its type's: an integer, a bool, a float or a double (an
enum by the width it is declared as), a nested struct by its table, a
fixed array by its element count, a context id (a grid's, a mesh's) as a
name, a pointer as an array whose count field ARRAYS names (a pointer it
does not name is an error, so none is missed), a host module's function
and user pointer as a name. A def's cookie is not stored: its entry has
tag 0, and its default gives it.

The registry gives each field its tag, its wire kind (what its bytes are:
"f32", "f32[3]", "mvfxCurve", "array mvfxForce", "grid", "module") and,
for a field that is not a struct, its default: the hex of the bytes the
file would write for it, at every place a default holds it but the places
named apart ("= 00000000 mvfxB.inner.x=0000803f"). It refuses a field
without a tag, a tag used twice in a struct, a retired tag used again, an
entry that names no field (it must be retired), a field whose wire kind is
no longer its entry's, and a field without a default, or with one of the
wrong length or at a place no def holds: a field whose type or default
changes gets a new tag, and its old one is retired. The defaults' bytes
themselves C checks: this writes them into test/file_pins.h beside the
tables, and test_file_defaults writes each field at each place from the
default functions with the library's own field writer and compares. The
output goes through clang-format, as every source does.

Usage: gen_file.py [--check | --self-test]. With --check it writes nothing
and fails when the file differs from what it would write; --self-test
runs the registry's refusals on made-up structs.
"""

import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "file_fields.c"
TAG_HEADER = ROOT / "src" / "file_tags.h"
PINS = ROOT / "test" / "file_pins.h"
LAYOUT = ROOT / "test" / "file_layout.h"
TAGS = ROOT / "tools" / "file-tags.txt"
HEADERS = sorted((ROOT / "include" / "maul-vfx").glob("*.h"))

# A pointer field's count field, the cap on that count, and how many
# elements a counted item holds.
ARRAYS = {
    ("mvfxEffectDef", "emitters"): ("emitterCount", "MVFX_MAX_EMITTERS", 1),
    ("mvfxEffectDef", "parameters"): ("parameterCount", "MVFX_MAX_PARAMETERS", 4),
    ("mvfxEffectDef", "qualityLevels"): ("qualityLevelCount", "MVFX_MAX_QUALITY_LEVELS", 1),
    ("mvfxEffectDef", "gases"): ("gasCount", "MVFX_MAX_GASES", 1),
    ("mvfxEffectDef", "waters"): ("waterCount", "MVFX_MAX_WATERS", 1),
    ("mvfxEmitterDef", "bursts"): ("burstCount", "MVFX_MAX_BURSTS", 1),
    ("mvfxEmitterDef", "forces"): ("forceCount", "MVFX_MAX_FORCES", 1),
    ("mvfxEmitterDef", "killRules"): ("killRuleCount", "MVFX_MAX_KILL_RULES", 1),
    ("mvfxEmitterDef", "colliders"): ("colliderCount", "MVFX_MAX_COLLIDERS", 1),
    ("mvfxEmitterDef", "hostModules"): ("hostModuleCount", "MVFX_MAX_HOST_MODULES", 1),
    ("mvfxEmitterDef", "custom"): ("customCount", "MVFX_MAX_CUSTOM_ATTRIBUTES", 1),
    ("mvfxGasDef", "sources"): ("sourceCount", "MVFX_MAX_GAS_SOURCES", 1),
    ("mvfxGasDef", "obstacles"): ("obstacleCount", "MVFX_MAX_COLLIDERS", 1),
    ("mvfxGasDef", "forces"): ("forceCount", "MVFX_MAX_FORCES", 1),
    ("mvfxWaterDef", "sources"): ("sourceCount", "MVFX_MAX_WATER_SOURCES", 1),
    ("mvfxCurve", "keys"): ("keyCount", "MVFX_CURVE_MAX_KEYS", 1),
    ("mvfxGradient", "colorKeys"): ("colorKeyCount", "MVFX_CURVE_MAX_KEYS", 1),
    ("mvfxGradient", "alphaKeys"): ("alphaKeyCount", "MVFX_CURVE_MAX_KEYS", 1),
    ("mvfxSteerPath", "points"): ("pointCount", "MVFX_MAX_PATH_POINTS", 3),
}

# Context ids the file holds as names.
IDS = {"mvfxGridId", "mvfxMeshId"}

# The headers' numeric defines, read with their structs.
DEFINES = {}

COOKIES = {
    "mvfxEffectDef": "MVFX_EFFECT_DEF_COOKIE",
    "mvfxEmitterDef": "MVFX_EMITTER_DEF_COOKIE",
    "mvfxGasDef": "MVFX_GAS_DEF_COOKIE",
    "mvfxWaterDef": "MVFX_WATER_DEF_COOKIE",
}

# The structs whose defaults their own function gives, by the initializer
# src/defaults.h shares with it; an item of any other struct defaults to
# zero bytes. Each struct with a cookie has one, so a def read from a file
# gets its cookie from its default.
DEFAULTS = {
    "mvfxEffectDef": "MVFX_DEFAULT_EFFECT_DEF",
    "mvfxEmitterDef": "MVFX_DEFAULT_EMITTER_DEF",
    "mvfxGasDef": "MVFX_DEFAULT_GAS_DEF",
    "mvfxWaterDef": "MVFX_DEFAULT_WATER_DEF",
}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


class Structs(dict):
    """The headers' structs by name, each read into its fields,
    [(type, field, dims, pointer)], when first asked for."""

    def __init__(self, bodies):
        super().__init__()
        self.bodies = bodies

    def __contains__(self, name):
        return name in self.bodies

    def __getitem__(self, name):
        if not dict.__contains__(self, name):
            dict.__setitem__(self, name, fields(name, self.bodies[name]))
        return dict.__getitem__(self, name)


def fields(name, body):
    """A struct body's fields, one a declaration."""
    out = []
    for line in body.split(";"):
        line = " ".join(line.split())
        if not line:
            continue
        fm = re.fullmatch(
            r"(?:const\s+)?(?:struct\s+)?(\w+)\s*(\*?)\s*(\w+)((?:\[[^\]]+\])*)", line)
        if fm is None:
            raise SystemExit(f"{name}: cannot read the field '{line}'")
        dims = re.findall(r"\[([^\]]+)\]", fm.group(4))
        out.append((fm.group(1), fm.group(3), dims, fm.group(2) == "*"))
    return out


def parse():
    """The headers' scalar typedefs, {name: type}, structs and numeric
    defines."""
    typedefs = {}
    bodies = {}
    for header in HEADERS:
        text = strip_comments(header.read_text(encoding="utf-8"))
        for m in re.finditer(r"#define\s+(MVFX_\w+)\s+(\d+)\s*$", text, re.M):
            DEFINES[m.group(1)] = int(m.group(2))
        for m in re.finditer(r"typedef\s+(\w+)\s+(mvfx\w+)\s*;", text):
            typedefs[m.group(2)] = m.group(1)
        for m in re.finditer(r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", text, re.S):
            bodies[m.group(3)] = m.group(2)
    return typedefs, Structs(bodies)


def scalar(typedefs, name):
    """A type's scalar after typedefs, or None for a struct."""
    while name in typedefs:
        name = typedefs[name]
    return name if name in KINDS else None


def reachable(typedefs, structs, root):
    """The structs reachable from root, each after those it holds."""
    order = []
    seen = set()

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        for kind, _, _, pointer in structs[name]:
            if scalar(typedefs, kind) is None and kind not in IDS and kind in structs:
                visit(kind)
            elif scalar(typedefs, kind) is None and kind not in IDS and not pointer:
                raise SystemExit(f"{name}: no struct or scalar '{kind}'")
        order.append(name)

    visit(root)
    return order


KINDS = {
    "bool": "mvfx_fieldBool",
    "uint8_t": "mvfx_fieldU8",
    "int8_t": "mvfx_fieldI8",
    "uint16_t": "mvfx_fieldU16",
    "int16_t": "mvfx_fieldI16",
    "uint32_t": "mvfx_fieldU32",
    "int32_t": "mvfx_fieldI32",
    "uint64_t": "mvfx_fieldU64",
    "int64_t": "mvfx_fieldI64",
    "float": "mvfx_fieldF32",
    "double": "mvfx_fieldF64",
}


# Each scalar's name in a wire kind.
WIRES = {
    "bool": "bool",
    "uint8_t": "u8",
    "int8_t": "i8",
    "uint16_t": "u16",
    "int16_t": "i16",
    "uint32_t": "u32",
    "int32_t": "i32",
    "uint64_t": "u64",
    "int64_t": "i64",
    "float": "f32",
    "double": "f64",
}


def kind_of(typedefs, kind):
    """A field type's kind in the table, and its element type's table or
    nullptr."""
    s = scalar(typedefs, kind)
    if s is not None:
        return KINDS[s], "nullptr"
    if kind == "mvfxGridId":
        return "mvfx_fieldGrid", "nullptr"
    if kind == "mvfxMeshId":
        return "mvfx_fieldMesh", "nullptr"
    return "mvfx_fieldStruct", f"&s_{kind}"


def wire_of(typedefs, kind):
    """A type's name in a wire kind: a scalar's, grid, mesh or a struct's."""
    s = scalar(typedefs, kind)
    if s is not None:
        return WIRES[s]
    return {"mvfxGridId": "grid", "mvfxMeshId": "mesh"}.get(kind, kind)


def stored(typedefs, structs, name):
    """A struct's fields the file stores, in declaration order: (type,
    field, dims, pointer, wire). An array's count, a def's cookie and a
    host module's user pointer are not fields of their own."""
    counts = {count for (owner, _), (count, _, _) in ARRAYS.items() if owner == name}
    out = []
    for kind, field, dims, pointer in structs[name]:
        if field in counts:
            if kind != "int32_t":
                raise SystemExit(f"{name}.{field}: an array's count must be int32_t")
            continue
        if field == "cookie" and name in COOKIES:
            continue
        if name == "mvfxHostModule" and field == "user":
            continue
        if name == "mvfxHostModule" and field == "function":
            out.append((kind, field, dims, pointer, "module"))
            continue
        if pointer:
            if (name, field) not in ARRAYS:
                raise SystemExit(f"{name}.{field}: a pointer with no count in ARRAYS")
            per = ARRAYS[(name, field)][2]
            wire = f"array {wire_of(typedefs, kind)}" + (f"[{per}]" if per > 1 else "")
            out.append((kind, field, dims, pointer, wire))
            continue
        many = 1
        for d in dims:
            many *= number(d)
        wire = wire_of(typedefs, kind) + (f"[{many}]" if dims else "")
        out.append((kind, field, dims, pointer, wire))
    return out


class TagError(Exception):
    """A registry the generator refuses, and why."""


def read_pin(text, number_):
    """An entry's default: (common, {path: exception}), the hex of the
    field's bytes as written, at every place but those named; None for an
    entry without one."""
    if text is None:
        return None
    words = text.split()
    exceptions = {}
    for word in words[1:]:
        path, _, value = word.partition("=")
        if not path or not re.fullmatch(r"[0-9a-f]+", value):
            raise TagError(f"line {number_}: {word!r} is not a place and its default in hex")
        exceptions[path] = value
    if not re.fullmatch(r"[0-9a-f]+", words[0]):
        raise TagError(f"line {number_}: {words[0]!r} is not a default in hex")
    return words[0], exceptions


def read_registry(text):
    """The registry's structs: {struct: ({tag: (field, wire, line, pin)},
    {tag: line})}, its entries and its retired tags. A tag used twice in a
    struct, retired or not, is refused here."""
    structs = {}
    current = None
    for number_, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line:
            continue
        head = re.fullmatch(r"(mvfx\w+)", line)
        retired = re.fullmatch(r"\s+retired\s+(\d+)", line)
        entry = re.fullmatch(r"\s+(\d+)\s+(\w+)\s+(\S.*?)(?:\s+=\s+(\S.*))?", line)
        if head:
            if head.group(1) in structs:
                raise TagError(f"line {number_}: {head.group(1)} listed twice")
            current = structs[head.group(1)] = ({}, {})
            continue
        if current is None or not (retired or entry):
            raise TagError(f"line {number_}: not a struct, an entry or a retired tag: {raw!r}")
        tag = int((retired or entry).group(1))
        if not 1 <= tag <= 0xFFFFFFFF:
            raise TagError(f"line {number_}: tag {tag} is not from 1 to 2^32 - 1")
        if tag in current[0] or tag in current[1]:
            raise TagError(f"line {number_}: tag {tag} used twice in its struct")
        if retired:
            current[1][tag] = number_
        else:
            current[0][tag] = (entry.group(2), " ".join(entry.group(3).split()), number_,
                               read_pin(entry.group(4), number_))
    return structs


def check_pin(name, field, line, pin, places):
    """A field's recorded default against its places, {path: (steps,
    bytes)}: one for every place, each the hex of its payload's bytes. Its
    bytes themselves the file_defaults suite checks against the default
    functions."""
    if pin is None:
        raise TagError(f"line {line}: {name}.{field} records no default")
    common, exceptions = pin
    size = next(iter(places.values()))[1]
    for value in [common] + list(exceptions.values()):
        if len(value) != 2 * size:
            raise TagError(f"line {line}: {name}.{field}'s default {value} is "
                           f"{len(value) // 2} bytes, not {size}")
    for path in exceptions:
        if path not in places:
            raise TagError(f"line {line}: {name}.{field} records a default at {path}, which no "
                           f"def holds: remove it")


def assign_tags(declared, text, places=None):
    """Each declared struct's fields' tags, {struct: {field: tag}}, from
    the registry's text; declared is {struct: [(field, wire)]} in
    declaration order. With places, {(struct, field): {path: (steps,
    bytes)}}, each field that is not a struct must record a default for
    them. Raises TagError for what the registry may not say."""
    registry = read_registry(text)
    for name in registry:
        if name not in declared:
            raise TagError(f"{name} is in the registry but no def reaches it: remove it")
    tags = {}
    for name, fields in declared.items():
        entries, retired = registry.get(name, ({}, {}))
        by_field = {}
        for tag, (field, wire, line, pin) in entries.items():
            if field in by_field:
                raise TagError(f"line {line}: {name}.{field} has two tags")
            by_field[field] = (tag, wire, line, pin)
        names = {field for field, _ in fields}
        for field, (tag, _, line, _) in by_field.items():
            if field not in names:
                raise TagError(f"line {line}: {name} has no field {field}: retire tag {tag}")
        tags[name] = {}
        for field, wire in fields:
            if field not in by_field:
                raise TagError(f"{name}.{field} has no tag in {TAGS.name}")
            tag, recorded, line, pin = by_field[field]
            if recorded != wire:
                raise TagError(f"line {line}: {name}.{field} is {wire}, not {recorded}: retire "
                               f"tag {tag} and give the field a new one")
            if places is not None and (name, field) in places:
                check_pin(name, field, line, pin, places[(name, field)])
            elif places is not None and pin is not None:
                raise TagError(f"line {line}: {name}.{field} is a struct, whose fields record "
                               f"its defaults: remove its own")
            tags[name][field] = tag
    return tags


def table(typedefs, structs, name, tags, defaults):
    """One struct's fields, in ascending tag order, and type."""
    entries = []
    if name in COOKIES:
        # Not stored: tag 0, first, which no field in a file has.
        at = f"offsetof({name}, cookie)"
        entries.append((0, f"{{0, {at}, mvfx_fieldCookie, 0, 1, nullptr, {COOKIES[name]}, 0, "
                           f"\"cookie\"}}"))
    for kind, field, dims, pointer, wire in stored(typedefs, structs, name):
        tag = tags[name][field]
        at = f"offsetof({name}, {field})"
        if wire == "module":
            user = f"offsetof({name}, user)"
            entries.append((tag, f"{{{tag}, {at}, mvfx_fieldModule, 0, 1, nullptr, {user}, 0, "
                                 f"\"{field}\"}}"))
            continue
        if pointer:
            count, cap, per = ARRAYS[(name, field)]
            element, type_ = kind_of(typedefs, kind)
            if element in ("mvfx_fieldGrid", "mvfx_fieldMesh"):
                raise SystemExit(f"{name}.{field}: an array of ids is not supported")
            entries.append((tag, f"{{{tag}, {at}, mvfx_fieldArray, {element}, {per}, {type_}, "
                                 f"offsetof({name}, {count}), {cap}, \"{field}\"}}"))
            continue
        number_ = " * ".join(dims) if dims else "1"
        k, type_ = kind_of(typedefs, kind)
        entries.append((tag, f"{{{tag}, {at}, {k}, 0, {number_}, {type_}, 0, 0, \"{field}\"}}"))
    entries.sort()
    default = (f"offsetof(mvfxFileDefaultBlock, {name})" if name in defaults
               else "MVFX_FILE_NO_DEFAULT")
    out = [f"static const mvfxFileField s_fields_{name}[] = {{"]
    out += [f"    {e}," for _, e in entries]
    out += ["};", f"static const mvfxFileType s_{name} = MVFX_FILE_TYPE({name}, {len(entries)}, "
                  f"{default});", ""]
    return out


def deepest(declared, name):
    """The most steps of a field path from a struct down to a field, by the
    registry's wire kinds: a struct's field and every field below it."""
    best = 0
    for _, wire in declared[name]:
        held = re.sub(r"\[\d+\]$", "", wire.split()[-1])
        best = max(best, 1 + (deepest(declared, held) if held in declared else 0))
    return best


def check_depth(declared, limit):
    """Refuses a field deeper below mvfxEffectDef than an effect refusal's
    path holds."""
    depth = deepest(declared, "mvfxEffectDef")
    if depth > limit:
        raise TagError(f"a field is {depth} steps below mvfxEffectDef, past "
                       f"MVFX_MAX_FIELD_DEPTH ({limit}), which an effect refusal holds")


def tag_macro(name, field):
    """A field's tag's constant: MVFX_TAG_<STRUCT>_<FIELD>."""
    def upper(text):
        return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", text).upper()
    return f"MVFX_TAG_{upper(name[4:])}_{upper(field)}"


def tags_header(order, tags):
    """src/file_tags.h: every stored field's tag as a constant."""
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        "// Generated by tools/gen_file.py from tools/file-tags.txt: do not edit.",
        "// Each stored field's tag in the effect file, MVFX_TAG_<STRUCT>_<FIELD>,",
        "// by which the validators name a field they refuse (mvfxEffectRefusal).",
        "",
        "#ifndef MAUL_VFX_SRC_FILE_TAGS_H",
        "#define MAUL_VFX_SRC_FILE_TAGS_H",
        "",
    ]
    for name in order:
        lines += [f"// {name}"]
        lines += [f"#define {tag_macro(name, field)} {tag}u"
                  for field, tag in sorted(tags[name].items(), key=lambda item: item[1])]
        lines += [""]
    lines += ["#endif", ""]
    return "\n".join(lines)


def number(text):
    """A dimension or a cap: a literal or a define."""
    return int(text) if text.isdigit() else DEFINES[text]


def times(factors, terms):
    """Each term of a sum times factors, as C products."""
    return [" * ".join(f for f in factors + [t] if f != "1") or "1" for t in terms]


def references(typedefs, structs, name, memo):
    """The most grids, meshes and host modules a struct refers to, each
    a sum of products of the headers' caps, as C."""
    if name in memo:
        return memo[name]
    total = [[], [], []]
    for kind, field, dims, pointer in structs[name]:
        factors = list(dims)
        if pointer and (name, field) in ARRAYS:
            factors = [ARRAYS[(name, field)][1]]
        if kind == "mvfxGridId":
            total[0] += times(factors, ["1"])
        elif kind == "mvfxMeshId":
            total[1] += times(factors, ["1"])
        elif name == "mvfxHostModule" and field == "function":
            total[2] += times(factors, ["1"])
        elif scalar(typedefs, kind) is None and kind in structs:
            inner = references(typedefs, structs, kind, memo)
            total = [t + times(factors, i) for t, i in zip(total, inner)]
    memo[name] = total
    return total


def value(term):
    """A product's value."""
    result = 1
    for factor in term.split(" * "):
        result *= number(factor)
    return result


# Each format version's layout of the effect section (layout()). A def whose
# layout is none of these changed the bytes: bump MVFX_FILE_VERSION in
# maul-vfx/file.h and record the new layout under it, so no file of one
# layout is read as another (an older version is then mvfx_errorVersion).
LAYOUTS = {
    1: "a53afec9fc6a364b",
    2: "0e339e17fd8d1a0b",
    3: "2b545a9627a6baaa",
    4: "e6b1062d9b3c5716",
}


def layout(tables):
    """A fingerprint of the bytes the tables lay down: each entry's kind,
    elements, nested type and cap, the defines a count or a cap names
    taken at their values, places and field names left out."""
    wire = []
    for line in tables:
        line = re.sub(r"offsetof\([^)]*\)", "", line)
        line = re.sub(r', "\w+"\}', "}", line)
        wire.append(re.sub(r"\bMVFX_\w+\b", lambda m: str(DEFINES.get(m.group(0), m.group(0))),
                           line))
    return hashlib.sha256("\n".join(wire).encode("utf-8")).hexdigest()[:16]


def defaulted(typedefs, structs, order):
    """The structs whose default the codec needs whole: the root, and each
    array's element struct, in order."""
    out = ["mvfxEffectDef"]
    for name in order:
        for kind, _, _, _, wire in stored(typedefs, structs, name):
            if wire.startswith("array ") and scalar(typedefs, kind) is None and kind not in out:
                out.append(kind)
    for name in COOKIES:
        if name not in DEFAULTS:
            raise SystemExit(f"{name} has a cookie but no default function in DEFAULTS")
    return out


# The bytes of each scalar.
SIZES = {"bool": 1, "uint8_t": 1, "int8_t": 1, "uint16_t": 2, "int16_t": 2, "uint32_t": 4,
         "int32_t": 4, "uint64_t": 8, "int64_t": 8, "float": 4, "double": 8}


def payload_bytes(typedefs, kind, dims, pointer, wire):
    """The bytes a field's payload takes when it holds a default: its
    scalars', 4 for each grid, mesh or host module (its name, none), and
    for an array, which holds no items then, its count and zero word."""
    many = 1
    for d in dims:
        many *= number(d)
    if wire == "module" or kind in IDS:
        return 4 * many
    if pointer:
        return 8
    return many * SIZES[scalar(typedefs, kind)]


def default_places(typedefs, structs, name, path, steps, out):
    """Each stored field of a struct that is not a struct, at each place a
    default holds it: out[(struct, field)][path] = (steps, bytes), steps
    the fields and fixed items from the default's root, bytes its
    payload's."""
    for kind, field, dims, pointer, wire in stored(typedefs, structs, name):
        here = f"{path}.{field}"
        if scalar(typedefs, kind) is None and not pointer and wire != "module" and kind not in IDS:
            many = 1
            for d in dims:
                many *= number(d)
            for k in range(many):
                place = here + (f"[{k}]" if many > 1 else "")
                item = k if many > 1 else -1
                default_places(typedefs, structs, kind, place, steps + [(name, field, item)], out)
            continue
        size = payload_bytes(typedefs, kind, dims, pointer, wire)
        out.setdefault((name, field), {})[here] = (steps + [(name, field, -1)], size)


def all_places(typedefs, structs, order):
    """Every stored field's places, {(struct, field): {path: (steps,
    bytes)}}, under the roots' and the array items' defaults."""
    out = {}
    for name in defaulted(typedefs, structs, order):
        default_places(typedefs, structs, name, name, [], out)
    return out


def generate(registry_text=None):
    typedefs, structs = parse()
    order = reachable(typedefs, structs, "mvfxEffectDef")
    declared = {name: [(f, w) for _, f, _, _, w in stored(typedefs, structs, name)]
                for name in order}
    if registry_text is None:
        registry_text = TAGS.read_text(encoding="utf-8")
    places = all_places(typedefs, structs, order)
    try:
        tags = assign_tags(declared, registry_text, places)
        check_depth(declared, DEFINES["MVFX_MAX_FIELD_DEPTH"])
    except TagError as error:
        raise SystemExit(f"{TAGS.name}: {error}") from error
    defaults = defaulted(typedefs, structs, order)
    tables = []
    for name in order:
        tables += table(typedefs, structs, name, tags, defaults)
    version = DEFINES["MVFX_FILE_VERSION"]
    if LAYOUTS.get(version) != layout(tables):
        raise SystemExit(f"the effect section's layout is {layout(tables)}, not format "
                         f"{version}'s: bump MVFX_FILE_VERSION and record it in LAYOUTS")
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        "// Generated by tools/gen_file.py from the public headers and",
        "// tools/file-tags.txt: do not edit. Every struct an effect def holds,",
        "// each field in ascending tag order, as the effect file's codec reads",
        "// and writes it, and the defaults an absent field takes.",
        "",
        '#include "file_fields.h"',
        "",
        '#include "defaults.h"',
        "",
        '#include "maul-vfx/effect.h"',
        "",
        "#include <stdalign.h>",
        "#include <stddef.h>",
        "",
        "// The defaults the codec takes whole: the root's and each array's",
        "// items'.",
        "typedef struct mvfxFileDefaultBlock",
        "{",
    ]
    lines += [f"    {name} {name};" for name in defaults]
    lines += [
        "} mvfxFileDefaultBlock;",
        "",
        "static const mvfxFileDefaultBlock s_defaults = {",
    ]
    lines += [f"    .{name} = {DEFAULTS[name]}," for name in defaults if name in DEFAULTS]
    lines += [
        "};",
        "",
        "const uint8_t* const mvfxFileDefaults = (const uint8_t*)&s_defaults;",
        "",
    ]
    lines += tables
    sums = references(typedefs, structs, "mvfxEffectDef", {})
    lines += [
        "const mvfxFileType* const mvfxFileEffectDef = &s_mvfxEffectDef;",
        "",
        "// The most references of each kind a def holds, which bound its names:",
        "// set in file_fields.h to these sums of the caps.",
    ]
    for macro, terms in zip(("GRIDS", "MESHES", "MODULES"), sums):
        total = sum(value(t) for t in terms)
        lines += [f"static_assert(MVFX_FILE_MAX_{macro} == {' + '.join(terms)}, \"{total}\");"]
    pins = pins_header(typedefs, structs, order, tags, read_registry(registry_text), places)
    layout_text = layout_header(typedefs, structs, order, tags)
    return {OUT: format_c("\n".join(lines) + "\n", OUT), PINS: format_c(pins, PINS),
            LAYOUT: format_c(layout_text, LAYOUT),
            TAG_HEADER: format_c(tags_header(order, tags), TAG_HEADER)}


def root_tags(typedefs, structs, tags, root):
    """The tags from mvfxEffectDef to the first array of a root's items,
    breadth first; none for mvfxEffectDef."""
    queue = [("mvfxEffectDef", [])]
    seen = set()
    while queue:
        name, path = queue.pop(0)
        if name == root:
            return path
        if name in seen:
            continue
        seen.add(name)
        for kind, field, _, _, _ in stored(typedefs, structs, name):
            if scalar(typedefs, kind) is None and kind in structs:
                queue.append((kind, path + [tags[name][field]]))
    raise SystemExit(f"no def reaches {root}")


def pins_header(typedefs, structs, order, tags, registry, places):
    """test/file_pins.h: each default the registry pins, at each place,
    with the tags that reach it and the bytes pinned, for test_file_defaults
    to check against the default functions."""
    roots = defaulted(typedefs, structs, order)
    pins = {}
    for name, (entries, _) in registry.items():
        for tag, (field, _, _, pin) in entries.items():
            if pin is not None:
                pins[(name, field)] = pin
    rows = []
    for (name, field), at in sorted(places.items(), key=lambda item: (order.index(item[0][0]),
                                                                       tags[item[0][0]][item[0][1]])):
        common, exceptions = pins[(name, field)]
        for path, (steps, _) in sorted(at.items()):
            value = exceptions.get(path, common)
            walk = ", ".join(f"{{{tags[s][f]}, {k}}}" for s, f, k in steps)
            escaped = "".join(f"\\x{value[i:i + 2]}" for i in range(0, len(value), 2))
            rows.append(f'    {{{roots.index(path.split(".")[0])}, "{path}", "{name}.{field}", '
                        f'{tags[name][field]}, {len(steps)}, {{{walk}}}, "{escaped}", '
                        f'{len(value) // 2}}},')
    depth = max(len(steps) for at in places.values() for steps, _ in at.values())
    reach = [root_tags(typedefs, structs, tags, root) for root in roots]
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        "// Generated by tools/gen_file.py from tools/file-tags.txt: do not edit.",
        "// Each default the registry pins: every field that is not a struct, at",
        "// each place a def's default or an array item's holds it, and the bytes",
        "// a file writes for it there. test_file_defaults checks them against",
        "// the default functions.",
        "",
        "#ifndef MAUL_VFX_TEST_FILE_PINS_H",
        "#define MAUL_VFX_TEST_FILE_PINS_H",
        "",
        '#include "maul-vfx/effect.h"',
        "",
        "#include <stdint.h>",
        "#include <string.h>",
        "",
        f"#define FILE_PIN_MAX_STEPS {depth}",
        f"#define FILE_PIN_MAX_ROOT_TAGS {max(1, max(len(r) for r in reach))}",
        "",
        "// A step from a struct to its field of a tag, and to an item of it when",
        "// it is a fixed list (-1 when it is not).",
        "typedef struct FilePinStep",
        "{",
        "    uint32_t tag;",
        "    int32_t item;",
        "} FilePinStep;",
        "",
        "// A pinned default: its root, its place, its field and tag, the steps from",
        "// the root to it, and the bytes the registry pins.",
        "typedef struct FilePin",
        "{",
        "    int32_t root;",
        "    const char* place;",
        "    const char* field;",
        "    uint32_t tag;",
        "    int32_t stepCount;",
        "    FilePinStep steps[FILE_PIN_MAX_STEPS];",
        "    const char* bytes;",
        "    int32_t byteCount;",
        "} FilePin;",
        "",
        "// A default's root: its struct's name, the tags from mvfxEffectDef to an",
        "// array of its items (none for mvfxEffectDef), its size, and how its",
        "// default is made: by its default function, or zero bytes for an item",
        "// of a struct that has none.",
        "typedef struct FilePinRoot",
        "{",
        "    const char* name;",
        "    int32_t tagCount;",
        "    uint32_t tags[FILE_PIN_MAX_ROOT_TAGS];",
        "    size_t size;",
        "    void (*fill)(void* out);",
        "} FilePinRoot;",
        "",
    ]
    for k, root in enumerate(roots):
        lines += [f"static void FillPinRoot{k}(void* out)", "{"]
        if root in DEFAULTS:
            function = "mvfxDefault" + root[4:]
            lines += [f"    {root} made = {function}();", "    memcpy(out, &made, sizeof made);"]
        else:
            lines += [f"    memset(out, 0, sizeof({root}));"]
        lines += ["}", ""]
    lines += ["static const FilePinRoot s_filePinRoots[] = {"]
    for k, (root, path) in enumerate(zip(roots, reach)):
        listed = ", ".join(str(t) for t in path) or "0"
        lines += [f'    {{"{root}", {len(path)}, {{{listed}}}, sizeof({root}), FillPinRoot{k}}},']
    lines += ["};", "", "static const FilePin s_filePins[] = {"] + rows + ["};", "", "#endif", ""]
    return "\n".join(lines)


def layout_name(name):
    """A struct's layout's name in test/file_layout.h."""
    return f"s_layout{name[4:]}"


def layout_header(typedefs, structs, order, tags):
    """test/file_layout.h: each struct a def holds as the public headers
    lay it out, for the suites that find fields in a file and fill a def's
    padding: every offset and size is the compiler's (offsetof, sizeof),
    and the table only names each field's tag, kind and nested layout."""
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        "// Generated by tools/gen_file.py from the public headers and",
        "// tools/file-tags.txt: do not edit. Every struct an effect def holds,",
        "// as the public headers lay it out: each stored field in ascending tag",
        "// order with its tag and name, its place and bytes (offsetof and",
        "// sizeof, so the compiler gives every one), what it holds, the layout",
        "// of a struct it holds or points to, an array's element size, the",
        "// elements a counted item holds and its count's place, a module's user",
        "// pointer's place; a def's cookie first, tag 0, as no file stores it.",
        "// The suites find fields in a file's bytes and fill a def's padding by",
        "// it, through nothing but the public headers.",
        "",
        "#ifndef MAUL_VFX_TEST_FILE_LAYOUT_H",
        "#define MAUL_VFX_TEST_FILE_LAYOUT_H",
        "",
        '#include "maul-vfx/effect.h"',
        "",
        "#include <stdalign.h>",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "// What a field holds.",
        "typedef enum FileFieldKind",
        "{",
        "    // Scalars, one or a fixed array of them.",
        "    fileFieldScalar,",
        "    // A struct, one or a fixed array of them.",
        "    fileFieldStruct,",
        "    // A pointer to counted items, scalars or structs.",
        "    fileFieldArray,",
        "    // A grid's or a mesh's id, one or a fixed array of them.",
        "    fileFieldId,",
        "    // A host module's function; its user pointer is at at.",
        "    fileFieldModule,",
        "    // A def's cookie, which no file stores.",
        "    fileFieldCookie,",
        "} FileFieldKind;",
        "",
        "typedef struct FileLayout FileLayout;",
        "",
        "typedef struct FileLayoutField",
        "{",
        "    uint32_t tag;",
        "    const char* name;",
        "    size_t offset;",
        "    size_t size;",
        "    FileFieldKind kind;",
        "    const FileLayout* type;",
        "    size_t element;",
        "    size_t per;",
        "    size_t at;",
        "} FileLayoutField;",
        "",
        "struct FileLayout",
        "{",
        "    const char* name;",
        "    size_t size;",
        "    size_t alignment;",
        "    int32_t fieldCount;",
        "    const FileLayoutField* fields;",
        "};",
        "",
    ]
    for name in order:
        rows = []
        if name in COOKIES:
            rows.append((0, f'{{0, "cookie", offsetof({name}, cookie), sizeof(uint32_t), '
                            f'fileFieldCookie, NULL, 0, 1, 0}}'))
        for kind, field, dims, pointer, wire in stored(typedefs, structs, name):
            tag = tags[name][field]
            at = f"offsetof({name}, {field})"
            size = f"sizeof((({name}*)0)->{field})"
            if wire == "module":
                rows.append((tag, f'{{{tag}, "{field}", {at}, {size}, fileFieldModule, NULL, 0, 1, '
                                  f'offsetof({name}, user)}}'))
                continue
            if pointer:
                count, _, per = ARRAYS[(name, field)]
                nested = (f"&{layout_name(kind)}" if scalar(typedefs, kind) is None else "NULL")
                rows.append((tag, f'{{{tag}, "{field}", {at}, {size}, fileFieldArray, {nested}, '
                                  f'sizeof({kind}), {per}, offsetof({name}, {count})}}'))
                continue
            if scalar(typedefs, kind) is not None:
                what, nested = "fileFieldScalar", "NULL"
            elif kind in IDS:
                what, nested = "fileFieldId", "NULL"
            else:
                what, nested = "fileFieldStruct", f"&{layout_name(kind)}"
            rows.append((tag, f'{{{tag}, "{field}", {at}, {size}, {what}, {nested}, 0, 1, 0}}'))
        rows.sort()
        lines += [f"static const FileLayoutField {layout_name(name)}Fields[] = {{"]
        lines += [f"    {row}," for _, row in rows]
        lines += ["};", f"static const FileLayout {layout_name(name)} = {{\"{name}\", "
                        f"sizeof({name}), alignof({name}), {len(rows)}, "
                        f"{layout_name(name)}Fields}};", ""]
    sums = references(typedefs, structs, "mvfxEffectDef", {})
    lines += [
        "// The most names of each kind a def refers to: the sums of the caps on",
        "// what holds a grid, a mesh or a host module.",
    ]
    for macro, terms in zip(("GRIDS", "MESHES", "MODULES"), sums):
        lines += [f"#define FILE_LAYOUT_MAX_{macro} ({' + '.join(terms)})"]
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def format_c(text, path):
    """The text as the family's clang-format writes it (CLANG_FORMAT, or
    clang-format-20, or clang-format)."""
    for tool in (os.environ.get("CLANG_FORMAT"), "clang-format-20", "clang-format"):
        if tool and shutil.which(tool):
            done = subprocess.run([tool, f"--assume-filename={path}"], input=text, text=True,
                                  capture_output=True, check=True, cwd=ROOT)
            return done.stdout
    raise SystemExit("gen_file.py needs clang-format")


def self_test():
    """Each refusal of the registry, on made-up structs. Whether a pinned
    default's bytes are the default's is the file_defaults suite's check."""
    declared = {"mvfxA": [("x", "f32"), ("y", "f32[3]")],
                "mvfxB": [("items", "array mvfxA"), ("inner", "mvfxA")]}
    places = {("mvfxA", "x"): {"mvfxA.x": ([], 4), "mvfxB.inner.x": ([], 4)},
              ("mvfxA", "y"): {"mvfxA.y": ([], 12), "mvfxB.inner.y": ([], 12)},
              ("mvfxB", "items"): {"mvfxB.items": ([], 8)}}
    good = ("mvfxA\n    1 x f32 = 00000000 mvfxB.inner.x=0000803f\n"
            "    2 y f32[3] = 000000000000000000000000\n    retired 3\n"
            "mvfxB\n    1 items array mvfxA = 0000000000000000\n    2 inner mvfxA\n")
    wanted = {"mvfxA": {"x": 1, "y": 2}, "mvfxB": {"items": 1, "inner": 2}}
    if assign_tags(declared, good, places) != wanted:
        print("gen_file.py --self-test: a good registry's tags")
        return 1
    y = "    2 y f32[3] = 000000000000000000000000\n"
    cases = [
        ("a field without a tag", good.replace(y, ""), "mvfxA.y has no tag"),
        ("a struct without its tags", good[:good.index("\nmvfxB\n") + 1],
         "mvfxB.items has no tag"),
        ("a tag used twice", good.replace("2 y", "1 y"), "tag 1 used twice"),
        ("a retired tag used again",
         good.replace("    retired 3\n", "").replace("    1 x", "    retired 2\n    1 x", 1),
         "tag 2 used twice"),
        ("a field given two tags", good.replace("retired 3", "4 x f32 = 00000000"),
         "mvfxA.x has two tags"),
        ("an entry naming no field", good.replace("retired 3", "3 z f32 = 00000000"),
         "mvfxA has no field z: retire tag 3"),
        ("a changed wire kind", good.replace("2 y f32[3]", "2 y f32[4]"),
         "mvfxA.y is f32[3], not f32[4]"),
        ("a struct no def reaches", good + "mvfxC\n    1 z f32 = 00000000\n",
         "mvfxC is in the registry"),
        ("tag 0", good.replace("retired 3", "0 z f32"), "tag 0 is not from 1"),
        ("a line of neither kind", good + "    x f32\n", "not a struct, an entry"),
        ("a default left out", good.replace(" = 000000000000000000000000", ""),
         "mvfxA.y records no default"),
        ("a default of the wrong length", good.replace("= 00000000 ", "= 000000 "),
         "mvfxA.x's default 000000 is 3 bytes, not 4"),
        ("an exception of the wrong length", good.replace("=0000803f", "=0000803f00"),
         "mvfxA.x's default 0000803f00 is 5 bytes, not 4"),
        ("a default at no place", good.replace("=0000803f", "=0000803f mvfxC.x=00000000"),
         "records a default at mvfxC.x, which no def holds"),
        ("a struct's own default", good.replace("2 inner mvfxA", "2 inner mvfxA = 00"),
         "mvfxB.inner is a struct"),
        ("a default not in hex", good.replace("1 items array mvfxA = 0000000000000000",
                                              "1 items array mvfxA = none"),
         "'none' is not a default in hex"),
    ]
    failed = 0
    try:
        check_depth({"mvfxEffectDef": [("b", "array mvfxB")], **declared}, 2)
        message = "accepted"
    except TagError as error:
        message = str(error)
    if "3 steps below mvfxEffectDef, past MVFX_MAX_FIELD_DEPTH (2)" not in message:
        print(f"gen_file.py --self-test: a field too deep: {message!r}")
        failed += 1
    for what, text, expected in cases:
        try:
            assign_tags(declared, text, places)
            message = "accepted"
        except TagError as error:
            message = str(error)
        if expected not in message:
            print(f"gen_file.py --self-test: {what}: {message!r} lacks {expected!r}")
            failed += 1
    return 1 if failed else 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
    outputs = generate()
    if "--check" in sys.argv[1:]:
        stale = [path for path, text in outputs.items()
                 if not path.exists() or path.read_text(encoding="utf-8") != text]
        for path in stale:
            print(f"{path.relative_to(ROOT)} is stale: run tools/gen_file.py")
        return 1 if stale else 0
    for path, text in outputs.items():
        path.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
