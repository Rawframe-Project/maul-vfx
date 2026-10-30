#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Converts effect files between their binary form and a JSON form.

The library reads and writes the binary form only (maul-vfx/file.h); the
JSON form is for diffs, reviews and test fixtures. It holds the effect def
as the binary form does: the fields of each struct the binary form holds,
in ascending tag order, read from the public headers and
tools/file-tags.txt as tools/gen_file.py reads them, so the two forms
cannot drift. A field the form leaves out takes its default, as in the
binary form; the library's writer leaves out every field that holds its
default, so a fixture says only what differs. A struct is an object of
its fields; a fixed array and an array a list (an item of several scalars,
such as a parameter's four values, a list of its own); a grid, a mesh or a
host module the name the asset table gives it, null for none; an enum the
name of its constant when the value has one; a float or a double a number
that reads back to its bits (non-finite ones as their bits in hex,
"0x7FC00000"). A field the struct does not have is refused. The form's
"format" is its own, apart from the binary form's version: a renamed
field bumps it alone, a changed layout bumps both.

Usage:
  effect_json.py to-json <file.mvfx> <file.json>
  effect_json.py to-binary <file.json> <file.mvfx>
  effect_json.py --check <dir>   each <name>.json makes <name>.mvfx and
                                 back, byte for byte
  effect_json.py --write <dir>   writes each <name>.mvfx from its JSON
"""

import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_file  # noqa: E402

VERSION = 2

# The JSON form's own format, which names fields where the binary form
# places them: each format's names of every field (names()). Renaming a
# field keeps the binary layout but not the JSON form: bump JSON_FORMAT
# and record the new names under it. A change of the binary layout
# changes the names' fingerprint too, so both move together then.
JSON_FORMAT = 4
NAMES = {
    1: "74fbb09047c22002",
    2: "bf3b95e3c138a4d7",
    3: "57c1059eb670f758",
}
MAGIC = b"MVFX"
HEADER = 24
# A section table entry: tag, offset, length.
ENTRY = 12
# Every section, field and item starts at a multiple of it.
ALIGNMENT = 8
# The sections' tags: the three kinds of names, then the effect.
EFFECT = 4
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
    """The def's structs as the headers declare them, field by field, with
    the tags the registry gives them."""

    def __init__(self):
        self.typedefs, self.structs = gen_file.parse()
        self.enums = parse_enums()
        order = gen_file.reachable(self.typedefs, self.structs, "mvfxEffectDef")
        declared = {name: [(f, w) for _, f, _, _, w in self.stored(name)] for name in order}
        try:
            self.tags = gen_file.assign_tags(
                declared, gen_file.TAGS.read_text(encoding="utf-8"))
        except gen_file.TagError as error:
            raise FileError(f"{gen_file.TAGS.name}: {error}") from error

    def scalar(self, kind):
        return gen_file.scalar(self.typedefs, kind)

    def stored(self, name):
        return gen_file.stored(self.typedefs, self.structs, name)

    def fields(self, name):
        """A struct's fields as the file holds them, in ascending tag
        order: (tag, field, what, type, count, extra). what is scalar,
        struct, grid, mesh, module or array; count a fixed array's
        elements; extra an array's cap and the items an element holds."""
        out = []
        for kind, field, dims, pointer, wire in self.stored(name):
            tag = self.tags[name][field]
            if wire == "module":
                out.append((tag, field, "module", kind, 1, None))
                continue
            what = ("grid" if kind == "mvfxGridId" else "mesh" if kind == "mvfxMeshId" else
                    "scalar" if self.scalar(kind) else "struct")
            if pointer:
                _, cap, per = gen_file.ARRAYS[(name, field)]
                out.append((tag, field, "array", kind, 1, (gen_file.number(cap), per, what)))
                continue
            many = 1
            for d in dims:
                many *= gen_file.number(d)
            out.append((tag, field, what, kind, many, None))
        return sorted(out)


def padded(at):
    return (at + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT


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
        self.names = [[], [], []]

    def u32s(self, at, end, count=1):
        if at + 4 * count > end:
            raise FileError(f"a field at {at} runs past {end}")
        return struct.unpack_from(f"<{count}I", self.data, at)

    def scalars(self, kind, at, end, count):
        base = self.layout.scalar(kind)
        fmt = FORMATS[base]
        size = struct.calcsize("<" + fmt)
        if end - at != size * count:
            raise FileError(f"{count} of {kind} at {at} are not {end - at} bytes")
        out = []
        for i in range(count):
            here = at + i * size
            if fmt in ("f", "d"):
                value = struct.unpack_from("<" + fmt, self.data, here)[0]
                out.append(float_text(value, fmt))
                continue
            value = struct.unpack_from("<" + fmt, self.data, here)[0]
            if base == "bool":
                if value > 1:
                    raise FileError(f"a bool of {value} at {here}")
                out.append(value == 1)
                continue
            out.append(self.layout.enums.get(kind, {}).get(value, value))
        return out

    def names_of(self, k, at, end, count):
        if end - at != 4 * count:
            raise FileError(f"{count} names at {at} are not {end - at} bytes")
        out = []
        for i in range(count):
            index = struct.unpack_from("<i", self.data, at + 4 * i)[0]
            if index != -1 and not 0 <= index < len(self.names[k]):
                raise FileError(f"a name {index} past the names at {at + 4 * i}")
            out.append(None if index == -1 else self.names[k][index])
        return out

    def items(self, kind, at, end, count):
        out = []
        for _ in range(count):
            length, zero = self.u32s(at, end, 2)
            if zero != 0 or length > end - at - 8:
                raise FileError(f"an item of {kind} at {at} out of place")
            out.append(self.struct(kind, at + 8, at + 8 + length))
            at += 8 + length
        if at != end:
            raise FileError(f"bytes after the items at {at}")
        return out

    def field(self, what, kind, count, extra, at, end):
        if what == "array":
            cap, per, inner = extra
            n, zero = self.u32s(at, end, 2)
            if zero != 0 or n > cap:
                raise FileError(f"a count of {n} at {at}, past its cap {cap}")
            if inner == "struct":
                return self.items(kind, at + 8, end, n)
            values = self.scalars(kind, at + 8, end, n * per)
            return values if per == 1 else [values[i:i + per] for i in range(0, n * per, per)]
        if what == "struct":
            if count == 1:
                return self.struct(kind, at, end)
            n, zero = self.u32s(at, end, 2)
            if zero != 0 or n != count:
                raise FileError(f"a list of {n} at {at}, not {count}")
            return self.items(kind, at + 8, end, count)
        if what in ("grid", "mesh", "module"):
            values = self.names_of(("grid", "mesh", "module").index(what), at, end, count)
        else:
            values = self.scalars(kind, at, end, count)
        return values[0] if count == 1 else values

    def struct(self, name, at, end):
        fields = {f[0]: f for f in self.layout.fields(name)}
        out = {}
        last = 0
        while at < end:
            tag, length = self.u32s(at, end, 2)
            if tag <= last or tag not in fields:
                raise FileError(f"{name} has no tag {tag} after {last} at {at}")
            if length > end - at - 8:
                raise FileError(f"a field at {at} runs past {end}")
            _, field, what, kind, count, extra = fields[tag]
            out[field] = self.field(what, kind, count, extra, at + 8, at + 8 + length)
            after = padded(at + 8 + length)
            if after > end or any(self.data[at + 8 + length:after]):
                raise FileError(f"the padding after {at + 8 + length}")
            at = after
            last = tag
        return out


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def to_json(layout, data):
    """A file's JSON form, as an object."""
    if len(data) < HEADER or data[:4] != MAGIC:
        raise FileError("not an effect file")
    if u32(data, 4) != VERSION:
        raise FileError(f"version {u32(data, 4)}, not {VERSION}")
    count = u32(data, 12)
    if u32(data, 8) != len(data) or HEADER + count * ENTRY > len(data):
        raise FileError("the header's size or section count is wrong")
    if struct.unpack_from("<Q", data, 16)[0] != hash64(data[HEADER:]):
        raise FileError("the hash does not match")
    reader = Reader(layout, data)
    effect = None
    last = 0
    at = padded(HEADER + count * ENTRY)
    for i in range(count):
        tag, offset, size = struct.unpack_from("<3I", data, HEADER + i * ENTRY)
        if tag <= last or tag > EFFECT or offset != at or offset + size > len(data):
            raise FileError(f"section {i} out of place")
        if tag < EFFECT:
            names = u32(data, offset)
            here = offset + 4
            for _ in range(names):
                length = u32(data, here)
                reader.names[tag - 1].append(data[here + 4:here + 4 + length].decode("utf-8"))
                here += 4 + length
            if here != offset + size:
                raise FileError(f"bytes after the names at {here}")
        else:
            effect = (offset, offset + size)
        at = padded(offset + size)
        last = tag
    if effect is None or at != len(data):
        raise FileError("no effect section, or bytes after the last")
    return {"format": JSON_FORMAT, "effect": reader.struct("mvfxEffectDef", *effect)}


class Writer:
    """Writes the JSON form into the binary form."""

    def __init__(self, layout):
        self.layout = layout
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
                return struct.pack("<" + bits, int(value, 16))
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
            return struct.pack("<" + fmt, value)
        except (struct.error, OverflowError) as error:
            raise FileError(f"{path}: {value!r} does not fit {kind}") from error

    def name(self, k, value, path):
        if value is None:
            return struct.pack("<i", -1)
        if not isinstance(value, str) or not value:
            raise FileError(f"{path}: a name or null, not {value!r}")
        if value not in self.names[k]:
            self.names[k].append(value)
        return struct.pack("<i", self.names[k].index(value))

    def item(self, what, kind, value, path):
        if what == "struct":
            body = self.struct(kind, value, path)
            return struct.pack("<II", len(body), 0) + body
        if what in ("grid", "mesh", "module"):
            return self.name(("grid", "mesh", "module").index(what), value, path)
        return self.scalar(kind, value, path)

    def items(self, what, kind, value, count, path):
        if not isinstance(value, list) or len(value) != count:
            raise FileError(f"{path}: a list of {count}")
        return b"".join(self.item(what, kind, v, f"{path}[{i}]") for i, v in enumerate(value))

    def field(self, what, kind, count, extra, value, path):
        if what == "array":
            cap, per, inner = extra
            if not isinstance(value, list) or len(value) > cap:
                raise FileError(f"{path}: a list of at most {cap}")
            out = struct.pack("<II", len(value), 0)
            for i, element in enumerate(value):
                if per == 1:
                    out += self.item(inner, kind, element, f"{path}[{i}]")
                else:
                    out += self.items(inner, kind, element, per, f"{path}[{i}]")
            return out
        if what == "struct" and count == 1:
            return self.struct(kind, value, path)
        if what == "struct":
            return struct.pack("<II", count, 0) + self.items(what, kind, value, count, path)
        if count == 1:
            return self.item(what, kind, value, path)
        return self.items(what, kind, value, count, path)

    def struct(self, name, value, path):
        """A struct's fields the object holds, in ascending tag order."""
        if not isinstance(value, dict):
            raise FileError(f"{path}: an object of {name}")
        fields = self.layout.fields(name)
        unknown = [f for f in value if f not in {field[1] for field in fields}]
        if unknown:
            raise FileError(f"{path}: {name} has no fields {unknown}")
        out = bytearray()
        for tag, field, what, kind, count, extra in fields:
            if field not in value:
                continue
            payload = self.field(what, kind, count, extra, value[field], f"{path}.{field}")
            out += struct.pack("<II", tag, len(payload)) + payload
            out += bytes(padded(len(out)) - len(out))
        return bytes(out)


def to_binary(layout, form):
    """A JSON form's file bytes."""
    if not isinstance(form, dict) or list(form) != ["format", "effect"]:
        raise FileError(f'the form is {{"format": {JSON_FORMAT}, "effect": {{...}}}}')
    if form["format"] != JSON_FORMAT:
        raise FileError(f"format {form['format']!r}, not {JSON_FORMAT}")
    writer = Writer(layout)
    effect = writer.struct("mvfxEffectDef", form["effect"], "effect")
    sections = []
    for k, names in enumerate(writer.names):
        if names:
            body = bytearray(struct.pack("<I", len(names)))
            for name in names:
                encoded = name.encode("utf-8")
                body += struct.pack("<I", len(encoded)) + encoded
            sections.append((k + 1, bytes(body)))
    sections.append((EFFECT, effect))
    table = bytearray()
    bodies = bytearray()
    at = padded(HEADER + len(sections) * ENTRY)
    for tag, body in sections:
        table += struct.pack("<3I", tag, at, len(body))
        bodies += body + bytes(padded(len(body)) - len(body))
        at = padded(at + len(body))
    start = padded(HEADER + len(table))
    payload = bytes(table) + bytes(start - HEADER - len(table)) + bytes(bodies)
    size = HEADER + len(payload)
    return MAGIC + struct.pack("<3IQ", VERSION, size, len(sections), hash64(payload)) + payload


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


def names(layout):
    """A fingerprint of the JSON form's shape: every struct the def
    reaches, each field's tag, name, what it is and its elements, in
    order."""
    seen = []
    order = ["mvfxEffectDef"]
    while order:
        name = order.pop(0)
        if name in seen:
            continue
        seen.append(name)
        for tag, field, what, kind, count, extra in layout.fields(name):
            seen.append(f"{tag} {field} {what} {kind} {count} {extra}")
            inner = extra[2] if extra else what
            if inner == "struct":
                order.append(kind)
    return hashlib.sha256("\n".join(seen).encode("utf-8")).hexdigest()[:16]


def main(argv):
    try:
        layout = Layout()
    except FileError as error:
        print(f"effect_json.py: {error}")
        return 1
    if NAMES.get(JSON_FORMAT) != names(layout):
        print(f"effect_json.py: the JSON form's names are {names(layout)}, not format "
              f"{JSON_FORMAT}'s: bump JSON_FORMAT and record them in NAMES")
        return 1
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
