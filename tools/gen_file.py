#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Writes src/file_fields.c: the effect file's table of every def field.

The effect unit of an effect file holds an mvfxEffectDef: every field of
every struct reachable from it, in declaration order. This reads those
structs from the public headers and writes a table of each one's fields
(place, kind, elements, nested type), which the codec (src/file_codec.c)
walks to read, write or measure, so the file and the def cannot drift: a
field added to a def is in the file once this runs, and the gate and CI
run it with --check.

A field's kind is its type's: an integer, a bool, a float or a double (an
enum by the width it is declared as), a nested struct by its table, a
fixed array by its element count, a context id (a grid's, a mesh's) as a
name, a pointer as an array whose count field ARRAYS names (a pointer it
does not name is an error, so none is missed), a host module's function
and user pointer as a name. A def's cookie is not stored: the reader sets
it. The output goes through clang-format, as every source does.

Usage: gen_file.py [--check]. With --check it writes nothing and fails
when the file differs from what it would write.
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


def table(typedefs, structs, name):
    """One struct's fields and type."""
    fields = structs[name]
    counts = {}
    for (owner, field), (count, cap, per) in ARRAYS.items():
        if owner == name:
            counts[count] = field
    entries = []
    for kind, field, dims, pointer in fields:
        at = f"offsetof({name}, {field})"
        if field in counts:
            if kind != "int32_t":
                raise SystemExit(f"{name}.{field}: an array's count must be int32_t")
            continue
        if field == "cookie" and name in COOKIES:
            entries.append(f"{{{at}, mvfx_fieldCookie, 0, 1, nullptr, {COOKIES[name]}, 0}}")
            continue
        if name == "mvfxHostModule" and field == "user":
            continue
        if name == "mvfxHostModule" and field == "function":
            user = f"offsetof({name}, user)"
            entries.append(f"{{{at}, mvfx_fieldModule, 0, 1, nullptr, {user}, 0}}")
            continue
        if pointer:
            if (name, field) not in ARRAYS:
                raise SystemExit(f"{name}.{field}: a pointer with no count in ARRAYS")
            count, cap, per = ARRAYS[(name, field)]
            element, type_ = kind_of(typedefs, kind)
            entries.append(f"{{{at}, mvfx_fieldArray, {element}, {per}, {type_}, "
                           f"offsetof({name}, {count}), {cap}}}")
            continue
        number = " * ".join(dims) if dims else "1"
        k, type_ = kind_of(typedefs, kind)
        entries.append(f"{{{at}, {k}, 0, {number}, {type_}, 0, 0}}")
    out = [f"static const mvfxFileField s_fields_{name}[] = {{"]
    out += [f"    {e}," for e in entries]
    out += ["};", f"static const mvfxFileType s_{name} = MVFX_FILE_TYPE({name}, {len(entries)});",
            ""]
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


# Each format version's layout of the effect unit (layout()). A def whose
# layout is none of these changed the bytes: bump MVFX_FILE_VERSION in
# maul-vfx/file.h and record the new layout under it, so no file of one
# layout is read as another (an older version is then mvfx_errorVersion).
LAYOUTS = {
    1: "a53afec9fc6a364b",
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


def generate():
    typedefs, structs = parse()
    order = reachable(typedefs, structs, "mvfxEffectDef")
    tables = []
    for name in order:
        tables += table(typedefs, structs, name)
    version = DEFINES["MVFX_FILE_VERSION"]
    if LAYOUTS.get(version) != layout(tables):
        raise SystemExit(f"the effect unit's layout is {layout(tables)}, not format "
                         f"{version}'s: bump MVFX_FILE_VERSION and record it in LAYOUTS")
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        "// Generated by tools/gen_file.py from the public headers: do not edit.",
        "// Every struct an effect def holds, each field in declaration order,",
        "// as the effect file's codec reads and writes it.",
        "",
        '#include "file_fields.h"',
        "",
        '#include "maul-vfx/effect.h"',
        "",
        "#include <stdalign.h>",
        "#include <stddef.h>",
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


def main():
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
