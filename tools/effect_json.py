#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Converts effect files between their binary form and a JSON form.

The library reads and writes the binary form only (maul-vfx/file.h); the
JSON form is for diffs, reviews and test fixtures. It holds the effect def
as the binary form does: every field of every struct the def reaches, in
declaration order, read from the public headers as tools/gen_file.py reads
them, so the two forms cannot drift. A struct is an object of its fields;
a fixed array and an array of scalars a list (an item of several, such as
a parameter's four values, a list of its own); a grid, a mesh or a host
module the name the asset table gives it, null for none; an enum the name
of its constant when the value has one; a float or a double a number that
reads back to its bits (non-finite ones as their bits in hex, "0x7FC00000").
Every field is required and none unknown, so a fixture says all it means.

Usage:
  effect_json.py to-json <file.mvfx> <file.json>
  effect_json.py to-binary <file.json> <file.mvfx>
  effect_json.py --check <dir>   each <name>.json makes <name>.mvfx and
                                 back, byte for byte
  effect_json.py --write <dir>   writes each <name>.mvfx from its JSON
"""

import json
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_file  # noqa: E402

VERSION = 1
MAGIC = b"MVFX"
HEADER = 24
ENTRY = 16
UNITS = 4
KINDS = ("grids", "meshes", "modules")

# Each scalar's struct format, little-endian.
FORMATS = {
    "bool": "B",
    "uint8_t": "B",
    "int8_t": "b",
    "uint16_t": "H",
    "int16_t": "h",
    "uint32_t": "I",
    "int32_t": "i",
    "uint64_t": "Q",
    "int64_t": "q",
    "float": "f",
    "double": "d",
}


class FileError(Exception):
    """A file or a JSON form that is not one, and where."""


def hash64(data):
    """mvfxHash64 from MVFX_HASH_INIT, its words read little-endian."""
    mask = (1 << 64) - 1
    h = 0xCBF29CE484222325
    whole = len(data) - len(data) % 8
    for i in range(0, whole, 8):
        h = ((h ^ int.from_bytes(data[i:i + 8], "little")) * 0x9E3779B97F4A7C15) & mask
        h ^= h >> 32
    for b in data[whole:]:
        h = ((h ^ b) * 0x100000001B3) & mask
    return h


def constant(expr):
    """An enum constant's value: integers joined by << and |, or None."""
    total = 0
    for term in expr.replace("(", " ").replace(")", " ").split("|"):
        parts = [p.strip() for p in term.split("<<")]
        if not all(re.fullmatch(r"-?\d+", p) for p in parts) or len(parts) > 2:
            return None
        value = int(parts[0]) << (int(parts[1]) if len(parts) == 2 else 0)
        total |= value
    return total


def parse_enums():
    """{typedef name: {value: constant}} for each scalar typedef that an
    enum of constants follows."""
    enums = {}
    known = {}
    for header in gen_file.HEADERS:
        text = gen_file.strip_comments(header.read_text(encoding="utf-8"))
        pattern = r"typedef\s+\w+\s+(mvfx\w+)\s*;\s*enum\s*\{(.*?)\}\s*;"
        for m in re.finditer(pattern, text, re.S):
            names = {}
            for item in m.group(2).split(","):
                cm = re.fullmatch(r"\s*(mvfx_\w+)\s*=\s*(.+?)\s*", item, re.S)
                if cm is None:
                    continue
                expr = re.sub(r"\b(\d+)u\b", r"\1", cm.group(2))
                expr = re.sub(r"\bmvfx_\w+\b", lambda n: str(known.get(n.group(0), "x")), expr)
                value = constant(expr)
                if value is None:
                    continue
                known[cm.group(1)] = value
                names.setdefault(value, cm.group(1))
            enums[m.group(1)] = names
    return enums


class Layout:
    """The def's structs as the headers declare them, field by field."""

    def __init__(self):
        self.typedefs, self.structs = gen_file.parse()
        self.enums = parse_enums()

    def scalar(self, kind):
        return gen_file.scalar(self.typedefs, kind)

    def fields(self, name):
        """A struct's fields as the file holds them: (field, what, type,
        count, extra). what is scalar, struct, grid, mesh, module or array;
        count a fixed array's elements; extra an array's count field, cap
        and items an element holds."""
        counts = {c for (owner, _), (c, _, _) in gen_file.ARRAYS.items() if owner == name}
        out = []
        for kind, field, dims, pointer in self.structs[name]:
            if field in counts or (field == "cookie" and name in gen_file.COOKIES):
                continue
            if name == "mvfxHostModule" and field == "user":
                continue
            if name == "mvfxHostModule" and field == "function":
                out.append((field, "module", kind, 1, None))
                continue
            many = 1
            for d in dims:
                many *= gen_file.number(d)
            what = ("grid" if kind == "mvfxGridId" else "mesh" if kind == "mvfxMeshId" else
                    "scalar" if self.scalar(kind) else "struct")
            if pointer:
                count, cap, per = gen_file.ARRAYS[(name, field)]
                out.append((field, "array", kind, 1, (count, gen_file.number(cap), per, what)))
                continue
            out.append((field, what, kind, many, None))
        return out


def float_text(value, fmt):
    """A float's JSON value: a number that reads back to its bits, or its
    bits in hex."""
    if math.isfinite(value) and fmt == "f":
        # The shortest decimal that rounds to the same binary32.
        bits = struct.pack("<f", value)
        for digits in range(1, 10):
            short = float(f"{value:.{digits}g}")
            if struct.pack("<f", short) == bits:
                return short
    if math.isfinite(value):
        return value
    width = 8 if fmt == "f" else 16
    bits = struct.unpack("<I" if fmt == "f" else "<Q", struct.pack("<" + fmt, value))[0]
    return f"0x{bits:0{width}X}"


class Reader:
    """Reads the binary form into the JSON form."""

    def __init__(self, layout, data):
        self.layout = layout
        self.data = data
        self.at = 0
        self.end = 0
        self.names = [[], [], []]

    def take(self, fmt):
        size = struct.calcsize("<" + fmt)
        if self.at + size > self.end:
            raise FileError(f"the effect unit ends at {self.end} inside a field at {self.at}")
        value = struct.unpack_from("<" + fmt, self.data, self.at)[0]
        self.at += size
        return value

    def scalar(self, kind):
        base = self.layout.scalar(kind)
        fmt = FORMATS[base]
        if fmt in ("f", "d"):
            raw = self.take("I" if fmt == "f" else "Q")
            value = struct.unpack("<" + fmt, struct.pack("<I" if fmt == "f" else "<Q", raw))[0]
            return float_text(value, fmt)
        value = self.take(fmt)
        if base == "bool":
            if value > 1:
                raise FileError(f"a bool of {value} at {self.at - 1}")
            return value == 1
        return self.layout.enums.get(kind, {}).get(value, value)

    def name(self, k):
        at = self.at
        index = self.take("i")
        if index == -1:
            return None
        if not 0 <= index < len(self.names[k]):
            raise FileError(f"a name {index} past the names at {at}")
        return self.names[k][index]

    def item(self, what, kind):
        if what == "struct":
            return self.struct(kind)
        if what == "grid":
            return self.name(0)
        if what == "mesh":
            return self.name(1)
        return self.scalar(kind)

    def struct(self, name):
        out = {}
        for field, what, kind, count, extra in self.layout.fields(name):
            if what == "module":
                out[field] = self.name(2)
            elif what == "array":
                _, cap, per, inner = extra
                at = self.at
                n = self.take("i")
                if not 0 <= n <= cap:
                    raise FileError(f"a count of {n} at {at}, past its cap {cap}")
                items = [[self.item(inner, kind) for _ in range(per)] for _ in range(n)]
                out[field] = [i[0] for i in items] if per == 1 else items
            elif count == 1:
                out[field] = self.item(what, kind)
            else:
                out[field] = [self.item(what, kind) for _ in range(count)]
        return out


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def to_json(layout, data):
    """A file's JSON form, as an object."""
    if len(data) < HEADER + UNITS * ENTRY or data[:4] != MAGIC:
        raise FileError("not an effect file")
    if u32(data, 4) != VERSION:
        raise FileError(f"version {u32(data, 4)}, not {VERSION}")
    if u32(data, 8) != len(data) or u32(data, 12) != UNITS:
        raise FileError("the header's size or unit count is wrong")
    if struct.unpack_from("<Q", data, 16)[0] != hash64(data[HEADER:]):
        raise FileError("the hash does not match")
    reader = Reader(layout, data)
    for k in range(UNITS):
        kind, offset, size, count = struct.unpack_from("<4I", data, HEADER + k * ENTRY)
        if kind != k or offset + size > len(data):
            raise FileError(f"unit {k} out of place")
        if k < 3:
            at = offset
            for _ in range(count):
                length = u32(data, at)
                reader.names[k].append(data[at + 4:at + 4 + length].decode("utf-8"))
                at += 4 + length
        else:
            reader.at = offset
            reader.end = offset + size
    effect = reader.struct("mvfxEffectDef")
    if reader.at != reader.end:
        raise FileError(f"bytes after the def at {reader.at}")
    return {"format": VERSION, "effect": effect}


class Writer:
    """Writes the JSON form into the binary form."""

    def __init__(self, layout):
        self.layout = layout
        self.out = bytearray()
        self.names = [[], [], []]

    def scalar(self, kind, value, path):
        base = self.layout.scalar(kind)
        fmt = FORMATS[base]
        if base == "bool":
            if not isinstance(value, bool):
                raise FileError(f"{path}: a bool, not {value!r}")
            value = int(value)
        elif fmt in ("f", "d"):
            if isinstance(value, str) and re.fullmatch(r"0x[0-9A-Fa-f]+", value):
                bits = "I" if fmt == "f" else "Q"
                self.out += struct.pack("<" + bits, int(value, 16))
                return
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise FileError(f"{path}: a number, not {value!r}")
            value = float(value)
        else:
            if isinstance(value, str):
                names = {v: k for k, v in self.layout.enums.get(kind, {}).items()}
                if value not in names:
                    raise FileError(f"{path}: {value!r} is no constant of {kind}")
                value = names[value]
            if isinstance(value, bool) or not isinstance(value, int):
                raise FileError(f"{path}: an integer, not {value!r}")
        try:
            self.out += struct.pack("<" + fmt, value)
        except (struct.error, OverflowError) as error:
            raise FileError(f"{path}: {value!r} does not fit {kind}") from error

    def name(self, k, value, path):
        if value is None:
            index = -1
        elif not isinstance(value, str) or not value:
            raise FileError(f"{path}: a name or null, not {value!r}")
        else:
            if value not in self.names[k]:
                self.names[k].append(value)
            index = self.names[k].index(value)
        self.out += struct.pack("<i", index)

    def item(self, what, kind, value, path):
        if what == "struct":
            self.struct(kind, value, path)
        elif what in ("grid", "mesh"):
            self.name(0 if what == "grid" else 1, value, path)
        else:
            self.scalar(kind, value, path)

    def items(self, what, kind, value, count, path):
        if not isinstance(value, list) or len(value) != count:
            raise FileError(f"{path}: a list of {count}")
        for i, v in enumerate(value):
            self.item(what, kind, v, f"{path}[{i}]")

    def struct(self, name, value, path):
        fields = self.layout.fields(name)
        if not isinstance(value, dict):
            raise FileError(f"{path}: an object of {name}")
        wanted = [f[0] for f in fields]
        if list(value) != wanted:
            missing = [f for f in wanted if f not in value]
            unknown = [f for f in value if f not in wanted]
            raise FileError(f"{path}: {name}'s fields in order; missing {missing}, "
                            f"unknown {unknown}")
        for field, what, kind, count, extra in fields:
            here = f"{path}.{field}"
            v = value[field]
            if what == "module":
                self.name(2, v, here)
            elif what == "array":
                _, cap, per, inner = extra
                if not isinstance(v, list) or len(v) > cap:
                    raise FileError(f"{here}: a list of at most {cap}")
                self.out += struct.pack("<i", len(v))
                for i, element in enumerate(v):
                    if per == 1:
                        self.item(inner, kind, element, f"{here}[{i}]")
                    else:
                        self.items(inner, kind, element, per, f"{here}[{i}]")
            elif count == 1:
                self.item(what, kind, v, here)
            else:
                self.items(what, kind, v, count, here)


def to_binary(layout, form):
    """A JSON form's file bytes."""
    if not isinstance(form, dict) or list(form) != ["format", "effect"]:
        raise FileError('the form is {"format": 1, "effect": {...}}')
    if form["format"] != VERSION:
        raise FileError(f"format {form['format']!r}, not {VERSION}")
    writer = Writer(layout)
    writer.struct("mvfxEffectDef", form["effect"], "effect")
    units = []
    for names in writer.names:
        unit = bytearray()
        for name in names:
            encoded = name.encode("utf-8")
            unit += struct.pack("<I", len(encoded)) + encoded
        units.append((bytes(unit), len(names)))
    units.append((bytes(writer.out), 1))
    table = bytearray()
    at = HEADER + UNITS * ENTRY
    for k, (unit, count) in enumerate(units):
        table += struct.pack("<4I", k, at, len(unit), count)
        at += len(unit)
    payload = bytes(table) + b"".join(u for u, _ in units)
    size = HEADER + len(payload)
    return MAGIC + struct.pack("<3IQ", VERSION, size, UNITS, hash64(payload)) + payload


def dumps(value, indent=0):
    """JSON with lists of scalars on one line, objects a field a line."""
    pad = "  " * indent
    if isinstance(value, dict):
        if not value:
            return "{}"
        inner = [f'{pad}  {json.dumps(k)}: {dumps(v, indent + 1)}' for k, v in value.items()]
        return "{\n" + ",\n".join(inner) + "\n" + pad + "}"
    if isinstance(value, list):
        if all(not isinstance(v, (dict, list)) for v in value):
            return "[" + ", ".join(dumps(v) for v in value) + "]"
        inner = [f"{pad}  {dumps(v, indent + 1)}" for v in value]
        return "[\n" + ",\n".join(inner) + "\n" + pad + "]"
    return json.dumps(value, allow_nan=False)


def json_text(layout, data):
    return dumps(to_json(layout, data)) + "\n"


def check(layout, folder, write):
    """Each JSON fixture against its binary form; True when all match."""
    good = True
    jsons = sorted(Path(folder).glob("*.json"))
    if not jsons:
        print(f"no fixtures in {folder}")
        return False
    for path in jsons:
        binary = path.with_suffix(".mvfx")
        text = path.read_text(encoding="utf-8")
        data = to_binary(layout, json.loads(text))
        if write:
            binary.write_bytes(data)
            continue
        if not binary.exists() or binary.read_bytes() != data:
            print(f"{binary} is not what {path.name} makes: run tools/effect_json.py --write")
            good = False
        elif json_text(layout, data) != text:
            print(f"{path} is not the canonical form of {binary.name}")
            good = False
    return good


def main(argv):
    layout = Layout()
    try:
        if len(argv) == 3 and argv[0] == "to-json":
            text = json_text(layout, Path(argv[1]).read_bytes())
            Path(argv[2]).write_text(text, encoding="utf-8")
            return 0
        if len(argv) == 3 and argv[0] == "to-binary":
            form = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
            Path(argv[2]).write_bytes(to_binary(layout, form))
            return 0
        if len(argv) == 2 and argv[0] in ("--check", "--write"):
            return 0 if check(layout, argv[1], argv[0] == "--write") else 1
    except FileError as error:
        print(f"effect_json.py: {error}")
        return 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
