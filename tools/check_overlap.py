#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Refuses a public function whose output may overlap an input unstated.

The family rule: an output never overlaps an input unless the call's
header says it may, and a call whose output has the type of one of its
inputs, which invites an update in place, either builds its result apart
and says so or checks for the overlap and refuses it. This lists every
public function (MVFX_API and MVFX_RHI_API in include/ and rhi/include/)
with an output parameter, a pointer or array not const, whose element type
is an input parameter's, a const pointer or array, and refuses each one
whose documentation does not say "overlap". Every byte type (void, char,
signed and unsigned char, int8_t, uint8_t) counts as one element type, as
any buffer of bytes may lie over another. A pointer to a pointer is an
output of a pointer, which no input is. Buffers a struct parameter points
to are not compared.

Usage: check_overlap.py [--self-test]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = ("include/maul-vfx/*.h", "rhi/include/maul-vfx-rhi/*.h")
DECLARATION = re.compile(r"MVFX(?:_RHI)?_API\b[^;(]*?\b(mvfx\w+)\s*\(([^;{]*?)\)\s*;", re.S)
PARAMETER = re.compile(r"^(?P<type>.*?)\b(?P<name>\w+)\s*(?P<array>\[[^\]]*\])?$")
BYTES = {"void", "char", "signedchar", "unsignedchar", "int8_t", "uint8_t"}


def parameters(text):
    """Each parameter's type, as written with a pointer for an array, and
    name."""
    out = []
    for part in " ".join(text.split()).split(","):
        part = part.strip()
        if part in ("", "void"):
            continue
        m = PARAMETER.match(part)
        if m:
            out.append((m.group("type").strip() + ("*" if m.group("array") else ""),
                        m.group("name")))
    return out


def element(kind):
    """A pointer's or array's element type, const and stars left out, every
    byte type one."""
    bare = re.sub(r"\bconst\b|\*|\s", "", kind)
    return "bytes" if bare in BYTES else bare


def documentation(text, start):
    """The /// lines right above the declaration at start."""
    lines = text[:start].splitlines()
    block = []
    while lines and (lines[-1].strip().startswith("///") or
                     lines[-1].strip() in ("MVFX_NODISCARD", "")):
        line = lines.pop().strip()
        if line.startswith("///"):
            block.append(line)
        elif not line and block:
            break
    return " ".join(reversed(block))


def findings(name, text):
    """Each function of a header with an output of an input's type, and
    whether its documentation states the overlap rule."""
    out = []
    for m in DECLARATION.finditer(text):
        # The declaration's line, from its attributes on.
        start = text.rfind("\n", 0, m.start()) + 1
        function, given = m.group(1), parameters(m.group(2))
        outputs = [(t, n) for t, n in given
                   if "*" in t and not t.startswith("const") and not t.endswith("**")]
        inputs = [(t, n) for t, n in given if "*" in t and t.startswith("const")]
        pairs = [(o, i) for o, i in ((o, i) for o in outputs for i in inputs)
                 if element(o[0]) == element(i[0])]
        if pairs:
            stated = "overlap" in documentation(text, start)
            out.append((name, function, [(o[1], i[1]) for o, i in pairs], stated))
    return out


def self_test():
    text = (
        "    /// Reads into memoryOut; it must not overlap the bytes.\n"
        "    MVFX_NODISCARD MVFX_API int mvfxStated(const void* bytes, void* memoryOut);\n"
        "\n"
        "    /// Copies values.\n"
        "    MVFX_NODISCARD MVFX_API int mvfxUnstated(const float* from, int32_t count,\n"
        "                                         float* valuesOut);\n"
        "\n"
        "    /// Fills an array.\n"
        "    MVFX_API int mvfxArray(const float in[4], float out[4]);\n"
        "\n"
        "    /// Other types, a pointer's pointer and a by-value input.\n"
        "    MVFX_API int mvfxApart(const double* points, float* valuesOut,\n"
        "                           const mvfxEvent** eventsOut, float scale);\n"
        "\n"
        "    /// Bytes of two spellings.\n"
        "    MVFX_API int mvfxBytes(const uint8_t* bytes, unsigned char* out);\n"
        "\n"
        "    /// A pointer to a pointer of an input's type is no output of it.\n"
        "    MVFX_API int mvfxHandle(const mvfxEvent* events, mvfxEvent** handleOut);\n"
        "\n"
        "    /// A value of an input's type is no output.\n"
        "    MVFX_API int mvfxValue(const float* values, float scale);\n"
        "\n"
        "    /// The GPU part's declarations count too.\n"
        "    MVFX_NODISCARD MVFX_RHI_API int mvfxGpuCopy(const uint32_t* from, uint32_t* to);\n")
    found = [(f, pairs, stated) for _, f, pairs, stated in findings("self", text)]
    expected = [("mvfxStated", [("memoryOut", "bytes")], True),
                ("mvfxUnstated", [("valuesOut", "from")], False),
                ("mvfxArray", [("out", "in")], False),
                ("mvfxBytes", [("out", "bytes")], False),
                ("mvfxGpuCopy", [("to", "from")], False)]
    if found != expected:
        print("check_overlap self-test: found", found)
        return 1
    print("overlap self-test: the 5 expected functions, one stated")
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    found = []
    for pattern in HEADERS:
        for path in sorted(ROOT.glob(pattern)):
            found += findings(str(path.relative_to(ROOT)), path.read_text(encoding="utf-8"))
    failed = 0
    for name, function, pairs, stated in found:
        listed = ", ".join(f"{o} over {i}" for o, i in pairs)
        if not stated:
            failed += 1
            print(f"{name}: {function}: an output of an input's type ({listed}), "
                  "its documentation silent on overlap")
    if failed:
        return 1
    noun = "function" if len(found) == 1 else "functions"
    print(f"overlap: {len(found)} {noun} with an output of an input's type, each stated")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
