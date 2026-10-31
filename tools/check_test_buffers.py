#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Refuses a byte array in the tests without an alignment.

A test hands the library byte buffers whose alignment it checks: a read
def's memory (16), a sort scratch (4), records and vertices of a layout.
An array of unsigned char or uint8_t has an alignment of 1, and where it
lands decides the rest: AddressSanitizer aligns globals on Linux, so a
missing alignment passes there and fails on another platform. Every
array of unsigned char or uint8_t declared in test/ (static or local)
therefore states one, with _Alignas or alignas. A struct's member is left
out: it lays out bytes as a record holds them, which an alignment would
move. The guide's tests
(test_guide*.c) are left out: their code is the user documentation's, as
a host writes it, and a buffer there that needs an alignment states it in
the documentation too.

Usage: check_test_buffers.py [--self-test]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ARRAY = re.compile(
    r"^\s*(?:static\s+)?(?:const\s+)?(?P<align>(?:_Alignas|alignas)\s*\([^)]*\)\s+)?"
    r"(?:static\s+)?(?:const\s+)?(?:unsigned\s+char|uint8_t)\s+\w+\s*\[")


STRUCT_START = re.compile(r"^\s*(?:typedef\s+)?(?:struct|union)\b[^;]*$")
STRUCT_END = re.compile(r"^\s*}[^;]*;")


def findings(name, text):
    """Each line of text that declares a byte array without an alignment,
    outside a struct's members."""
    out = []
    member = False
    for number, line in enumerate(text.splitlines(), 1):
        if STRUCT_START.match(line):
            member = True
        elif member and STRUCT_END.match(line):
            member = False
            continue
        if member:
            continue
        m = ARRAY.match(line)
        # A parameter on a line of its own is not a declaration.
        parameter = line.rstrip().endswith((")", ",", ") {"))
        if m and not m.group("align") and not parameter:
            out.append(f"{name}:{number}: a byte array without _Alignas: {line.strip()}")
    return out


def self_test():
    good = ("static _Alignas(16) unsigned char a[4];\n"
            "    _Alignas(16) uint8_t b[8] = {0};\n"
            "static const _Alignas(16) uint8_t c[2] = {1, 2};\n"
            "    uint8_t* pointer;\n"
            "    char text[256];\n"
            "                 unsigned char out[16])\n"
            "typedef struct Record\n"
            "{\n"
            "    uint8_t color[4];\n"
            "} Record;\n")
    bad = ("static unsigned char a[4];\n"
           "    uint8_t b[8] = {0};\n"
           "static const uint8_t c[2] = {1, 2};\n"
           "    unsigned char d[1 << 16];\n")
    found = findings("good", good) + findings("bad", bad)
    expected = [f"bad:{n}" for n in range(1, 5)]
    if [f.split(": ", 1)[0] for f in found] != expected:
        print("check_test_buffers self-test: found", found)
        return 1
    print("test buffers self-test: the 4 expected findings, nothing else")
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    found = []
    for path in sorted((ROOT / "test").glob("*.[ch]")):
        if path.name.startswith("test_guide"):
            continue
        found += findings(str(path.relative_to(ROOT)), path.read_text(encoding="utf-8"))
    for f in found:
        print(f)
    if found:
        return 1
    print("test buffers: every byte array aligned")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
