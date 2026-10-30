#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Writes src/file_fields.c: the effect file's table of every def field.

The effect section of an effect file holds an mvfxEffectDef as tagged
fields: each struct a sequence of its fields that differ from their
defaults, each field a 32-bit tag, a 32-bit length and its payload. This
reads the structs reachable from mvfxEffectDef from the public headers,
gives each field the tag tools/file-tags.txt records for it, and writes a
table of each struct's fields in ascending tag order (tag, place, kind,
elements, nested type), which the codec (src/file_codec.c) walks to read,
write or measure. It also writes the block of defaults an absent field
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
named apart ("= 00000000 mvfxB.inner.x=0000803f"). The defaults are read
from src/defaults.h, the initializers the default functions return, float
literals and operations rounded to binary32 as C rounds them; an item of
an array of another struct defaults to zero bytes. It refuses a field
without a tag, a tag used twice in a struct, a retired tag used again, an
entry that names no field (it must be retired), a field whose wire kind is
no longer its entry's and a field whose default is no longer its entry's:
a field whose type or default changes gets a new tag, and its old one is
retired. The output goes through clang-format, as every source does.

Usage: gen_file.py [--check | --self-test]. With --check it writes nothing
and fails when the file differs from what it would write; --self-test
runs the registry's refusals on made-up structs.
"""

import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "file_fields.c"
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


def check_pin(name, field, tag, line, pin, places):
    """A field's recorded default against its bytes at each place a def's
    default holds it, places {path: hex}."""
    if pin is None:
        raise TagError(f"line {line}: {name}.{field} records no default: add "
                       f"= {max(set(places.values()), key=list(places.values()).count)}")
    common, exceptions = pin
    for path, value in places.items():
        want = exceptions.get(path, common)
        if value != want:
            raise TagError(f"line {line}: {name}.{field}'s default at {path} is {value}, not "
                           f"{want}: a changed default gets a new tag, so retire tag {tag} and "
                           f"give the field a new one (a new place of {name} records its "
                           f"default as {path}={value})")
    for path in exceptions:
        if path not in places:
            raise TagError(f"line {line}: {name}.{field} records a default at {path}, which no "
                           f"def holds: remove it")


def assign_tags(declared, text, pins=None):
    """Each declared struct's fields' tags, {struct: {field: tag}}, from
    the registry's text; declared is {struct: [(field, wire)]} in
    declaration order. With pins, {(struct, field): {path: hex}}, each
    field's recorded default is checked against them too. Raises TagError
    for what the registry may not say."""
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
            if pins is not None and (name, field) in pins:
                check_pin(name, field, tag, line, pin, pins[(name, field)])
            elif pins is not None and pin is not None:
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
        entries.append((0, f"{{0, {at}, mvfx_fieldCookie, 0, 1, nullptr, {COOKIES[name]}, 0}}"))
    for kind, field, dims, pointer, wire in stored(typedefs, structs, name):
        tag = tags[name][field]
        at = f"offsetof({name}, {field})"
        if wire == "module":
            user = f"offsetof({name}, user)"
            entries.append((tag, f"{{{tag}, {at}, mvfx_fieldModule, 0, 1, nullptr, {user}, 0}}"))
            continue
        if pointer:
            count, cap, per = ARRAYS[(name, field)]
            element, type_ = kind_of(typedefs, kind)
            if element in ("mvfx_fieldGrid", "mvfx_fieldMesh"):
                raise SystemExit(f"{name}.{field}: an array of ids is not supported")
            entries.append((tag, f"{{{tag}, {at}, mvfx_fieldArray, {element}, {per}, {type_}, "
                                 f"offsetof({name}, {count}), {cap}}}"))
            continue
        number_ = " * ".join(dims) if dims else "1"
        k, type_ = kind_of(typedefs, kind)
        entries.append((tag, f"{{{tag}, {at}, {k}, 0, {number_}, {type_}, 0, 0}}"))
    entries.sort()
    default = (f"offsetof(mvfxFileDefaultBlock, {name})" if name in defaults
               else "MVFX_FILE_NO_DEFAULT")
    out = [f"static const mvfxFileField s_fields_{name}[] = {{"]
    out += [f"    {e}," for _, e in entries]
    out += ["};", f"static const mvfxFileType s_{name} = MVFX_FILE_TYPE({name}, {len(entries)}, "
                  f"{default});", ""]
    return out


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
}


def layout(tables):
    """A fingerprint of the bytes the tables lay down: each entry's kind,
    elements, nested type and cap, the defines a count or a cap names
    taken at their values, places and field names left out."""
    wire = []
    for line in tables:
        line = re.sub(r"offsetof\([^)]*\)", "", line)
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


DEFAULTS_H = ROOT / "src" / "defaults.h"

# Each scalar's struct format, little-endian.
FORMATS = {"bool": "B", "uint8_t": "B", "int8_t": "b", "uint16_t": "H", "int16_t": "h",
           "uint32_t": "I", "int32_t": "i", "uint64_t": "Q", "int64_t": "q"}


def binary32(value):
    """The binary32 nearest a value, ties to even, as an exact fraction, as
    C rounds a float literal and each float operation."""
    value = Fraction(value)
    if value == 0:
        return Fraction(0)
    sign = -1 if value < 0 else 1
    value = abs(value)
    exponent = value.numerator.bit_length() - value.denominator.bit_length()
    if Fraction(2) ** exponent > value:
        exponent -= 1
    if exponent < -126:
        raise SystemExit(f"{DEFAULTS_H.name}: {float(value)} is below binary32's least normal")
    unit = Fraction(2) ** (exponent - 23)
    whole, rest = divmod(value, unit)
    whole = int(whole)
    if rest > unit / 2 or (rest == unit / 2 and whole % 2):
        whole += 1
    return sign * whole * unit


def constants():
    """The headers' enum constants and defines, by name."""
    known = dict(DEFINES)
    for header in HEADERS:
        text = strip_comments(header.read_text(encoding="utf-8"))
        for m in re.finditer(r"#define\s+(MVFX_\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\s*$", text, re.M):
            known[m.group(1)] = int(m.group(2), 0)
        for m in re.finditer(r"\b(mvfx_\w+)\s*=\s*([0-9]+)u?\s*[,}]", text):
            known[m.group(1)] = int(m.group(2))
    return known


def evaluate(words, known):
    """A constant expression of the defaults: float literals and float
    operations rounded as C rounds them, integers, constants."""
    floating = False
    terms = []
    for word in words:
        if re.fullmatch(r"\d+\.\d*(?:[eE][-+]?\d+)?f|\d+f", word):
            terms.append(f"binary32(Fraction('{word[:-1]}'))")
            floating = True
        elif re.fullmatch(r"0x[0-9A-Fa-f]+u?|\d+u?", word):
            terms.append(str(int(word.rstrip("u"), 0)))
        elif word in known:
            terms.append(str(known[word]))
        elif word in "+-*/()":
            terms.append(word)
        else:
            raise SystemExit(f"{DEFAULTS_H.name}: cannot read {word!r}")
    if floating:
        # Each operation of two floats rounds: here a literal, or a literal
        # over a literal, so rounding the result once is exact.
        if len([w for w in words if w in "+-*/"]) > 1:
            raise SystemExit(f"{DEFAULTS_H.name}: {' '.join(words)} has more than one operation")
        return binary32(eval(" ".join(terms), {"binary32": binary32, "Fraction": Fraction}))
    return eval(" ".join(terms))


def initializer(words, at, known):
    """The initializer at words[at]: [(designator or None, value)] for a
    braced one, else its value; and where it ends."""
    if words[at] != "{":
        end = at
        while words[end] not in (",", "}"):
            end += 1
        return evaluate(words[at:end], known), end
    items = []
    at += 1
    while words[at] != "}":
        name = None
        if words[at] == ".":
            name = words[at + 1]
            at += 3
        value, at = initializer(words, at, known)
        items.append((name, value))
        at += 1 if words[at] == "," else 0
    return items, at + 1


def read_defaults():
    """The initializers of src/defaults.h, by the struct they default."""
    text = DEFAULTS_H.read_text(encoding="utf-8").replace("\\\n", " ")
    text = strip_comments(text)
    known = constants()
    out = {}
    for name, macro in DEFAULTS.items():
        m = re.search(rf"#define\s+{macro}\s+(\{{.*)", text)
        if m is None:
            raise SystemExit(f"{DEFAULTS_H.name} has no {macro}")
        word = r"0x[0-9A-Fa-f]+u?|\d+\.\d*(?:[eE][-+]?\d+)?f|\d+f|\d+u?|\w+|[{}.,=/*+()-]"
        words = re.findall(word, m.group(1))
        out[name], _ = initializer(words, 0, known)
    return out


def scalar_bytes(base, value):
    if base == "float":
        return struct.pack("<f", float(value))
    if base == "double":
        return struct.pack("<d", float(value))
    return struct.pack("<" + FORMATS[base], int(value))


def default_places(typedefs, structs, name, given, path, out):
    """Each stored field of a struct under its initializer (None for zero
    bytes): out[(struct, field)][path] is the hex of the bytes the file
    writes for it. A grid, a mesh or a host module is none (-1), an
    array no items (a count of 0)."""
    # A positional item is the struct's next field in declaration order.
    declared = [field for _, field, _, _ in structs[name]]
    values = {}
    for k, (field, value) in enumerate(given or []):
        if field is None and k > 0 and given[k - 1][0] is not None:
            raise SystemExit(f"{DEFAULTS_H.name}: {name}'s initializer mixes designators")
        values[field if field is not None else declared[k]] = value
    names = {field for _, field, _, _, _ in stored(typedefs, structs, name)}
    for field in values:
        if field not in names and not (field == "cookie" and name in COOKIES):
            raise SystemExit(f"{DEFAULTS_H.name}: {name} has no stored field {field}")
    for kind, field, dims, pointer, wire in stored(typedefs, structs, name):
        value = values.get(field)
        here = f"{path}.{field}"
        many = 1
        for d in dims:
            many *= number(d)
        base = scalar(typedefs, kind)
        if base is None and not pointer and wire != "module" and kind not in IDS:
            items = value if isinstance(value, list) and many > 1 else None
            for k in range(many):
                inner = value if many == 1 else (items[k][1] if items and k < len(items) else None)
                place = here + (f"[{k}]" if many > 1 else "")
                default_places(typedefs, structs, kind, inner, place, out)
            continue
        if wire == "module" or kind in IDS or pointer:
            if value is not None:
                raise SystemExit(f"{DEFAULTS_H.name}: {name}.{field} must default to none")
            hexed = "ffffffff" * many if not pointer else "00000000"
        else:
            listed = [v for _, v in value] if isinstance(value, list) else [value or 0]
            listed += [0] * (many - len(listed))
            hexed = b"".join(scalar_bytes(base, v) for v in listed).hex()
        out.setdefault((name, field), {})[here] = hexed


def default_pins(typedefs, structs, order):
    """Every stored field's default at each place a default holds it:
    {(struct, field): {path: hex}}, from the roots' and the array items'
    defaults."""
    given = read_defaults()
    out = {}
    for name in defaulted(typedefs, structs, order):
        default_places(typedefs, structs, name, given.get(name), name, out)
    return out


def generate(registry_text=None):
    typedefs, structs = parse()
    order = reachable(typedefs, structs, "mvfxEffectDef")
    declared = {name: [(f, w) for _, f, _, _, w in stored(typedefs, structs, name)]
                for name in order}
    if registry_text is None:
        registry_text = TAGS.read_text(encoding="utf-8")
    try:
        tags = assign_tags(declared, registry_text, default_pins(typedefs, structs, order))
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
    return format_c("\n".join(lines) + "\n")


def format_c(text):
    """The text as the family's clang-format writes it (CLANG_FORMAT, or
    clang-format-20, or clang-format)."""
    for tool in (os.environ.get("CLANG_FORMAT"), "clang-format-20", "clang-format"):
        if tool and shutil.which(tool):
            done = subprocess.run([tool, f"--assume-filename={OUT}"], input=text, text=True,
                                  capture_output=True, check=True, cwd=ROOT)
            return done.stdout
    raise SystemExit("gen_file.py needs clang-format")


def self_test():
    """Each refusal of the registry, on made-up structs."""
    declared = {"mvfxA": [("x", "f32"), ("y", "f32[3]")],
                "mvfxB": [("items", "array mvfxA"), ("inner", "mvfxA")]}
    pins = {("mvfxA", "x"): {"mvfxA.x": "00000000", "mvfxB.inner.x": "0000803f"},
            ("mvfxA", "y"): {"mvfxA.y": "00" * 12, "mvfxB.inner.y": "00" * 12},
            ("mvfxB", "items"): {"mvfxB.items": "00000000"}}
    good = ("mvfxA\n    1 x f32 = 00000000 mvfxB.inner.x=0000803f\n"
            "    2 y f32[3] = 000000000000000000000000\n    retired 3\n"
            "mvfxB\n    1 items array mvfxA = 00000000\n    2 inner mvfxA\n")
    wanted = {"mvfxA": {"x": 1, "y": 2}, "mvfxB": {"items": 1, "inner": 2}}
    if assign_tags(declared, good, pins) != wanted:
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
        ("a changed default", good.replace("mvfxB.inner.x=0000803f", "mvfxB.inner.x=00000040"),
         "mvfxA.x's default at mvfxB.inner.x is 0000803f, not 00000040: a changed default gets "
         "a new tag, so retire tag 1"),
        ("a changed common default", good.replace("1 x f32 = 00000000", "1 x f32 = 00008040"),
         "mvfxA.x's default at mvfxA.x is 00000000, not 00008040"),
        ("a default left out", good.replace(" = 000000000000000000000000", ""),
         "mvfxA.y records no default"),
        ("a default at no place", good.replace("=0000803f", "=0000803f mvfxC.x=00000000"),
         "records a default at mvfxC.x, which no def holds"),
        ("a struct's own default", good.replace("2 inner mvfxA", "2 inner mvfxA = 00"),
         "mvfxB.inner is a struct"),
        ("a default not in hex", good.replace("1 items array mvfxA = 00000000",
                                              "1 items array mvfxA = none"),
         "'none' is not a default in hex"),
    ]
    failed = 0
    for what, text, expected in cases:
        try:
            assign_tags(declared, text, pins)
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
    text = generate()
    if "--check" in sys.argv[1:]:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            print(f"{OUT.relative_to(ROOT)} is stale: run tools/gen_file.py")
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
