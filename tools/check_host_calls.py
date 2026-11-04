#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Refuses a public call taking the context mutable that no test calls
back in from host code.

The contract: a call that takes the context mutable is refused with
mvfx_errorBusy, counted as misuse, while the context runs host code (the
allocator, the task hooks, a host module). test/test_host_calls.c calls
each such call from a host module through one table, s_changes, by its
public name. This lists every public function of include/maul-vfx/ whose
first parameter is `mvfxContext* context`, not const, and refuses one the
table lacks, and a name in the table that is no such function. The
context's own destruction is left out: the test's callbacks destroy it
and count its refusal apart.

Usage: check_host_calls.py [--self-test]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = "include/maul-vfx/*.h"
TEST = "test/test_host_calls.c"
DECLARATION = re.compile(r"MVFX_API\b[^;(]*?\b(mvfx\w+)\s*\(([^;{]*?)\)\s*;", re.S)
MUTABLE = re.compile(r"^\s*mvfxContext\s*\*\s*context\b")
TABLE = re.compile(r"\bs_changes\[\]\s*=\s*\{(.*?)\n\};", re.S)
NAME = re.compile(r'"(mvfx\w+)"')
LEFT_OUT = {"mvfxDestroyContext"}


def mutable_calls(text):
    """The functions declared in text whose first parameter is the
    context, not const."""
    return {m.group(1) for m in DECLARATION.finditer(text) if MUTABLE.match(m.group(2))}


def table_names(text):
    """The names the s_changes table holds, or None without a table."""
    m = TABLE.search(text)
    return None if m is None else set(NAME.findall(m.group(1)))


def problems(calls, names):
    """Each call the table lacks and each name it holds that is no call."""
    if names is None:
        return ["test/test_host_calls.c has no s_changes table"]
    wanted = calls - LEFT_OUT
    out = [f"{call}: takes the context mutable, missing from s_changes" for call in sorted(wanted - names)]
    out += [f"{name}: in s_changes, but no public call taking the context mutable"
            for name in sorted(names - wanted)]
    return out


def self_test():
    header = """
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateThing(mvfxContext* context,
                                                       mvfxThingId* thingOut);
    MVFX_NODISCARD MVFX_API mvfxResult mvfxThing_Get(const mvfxContext* context, int32_t* out);
    MVFX_API void mvfxDestroyContext(mvfxContext* context);
    MVFX_NODISCARD MVFX_API mvfxResult mvfxThing_Set(mvfxContext* context, int32_t value);
    MVFX_API int32_t mvfxGetVersion(void);
    """
    calls = mutable_calls(header)
    assert calls == {"mvfxCreateThing", "mvfxDestroyContext", "mvfxThing_Set"}, calls
    whole = 'static const struct { Change call; } s_changes[] = {\n    {A, "mvfxCreateThing"},\n    {B, "mvfxThing_Set"},\n};'
    assert problems(calls, table_names(whole)) == []
    short = 'static const struct { Change call; } s_changes[] = {\n    {A, "mvfxCreateThing"},\n};'
    assert problems(calls, table_names(short)) == [
        "mvfxThing_Set: takes the context mutable, missing from s_changes"]
    stale = whole.replace('"mvfxThing_Set"', '"mvfxThing_Set"},\n    {C, "mvfxThing_Get"')
    assert problems(calls, table_names(stale)) == [
        "mvfxThing_Get: in s_changes, but no public call taking the context mutable"]
    assert problems(calls, table_names("no table")) == [
        "test/test_host_calls.c has no s_changes table"]
    print("check_host_calls: self-test passed")


def main():
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return 0
    calls = set()
    for header in sorted(ROOT.glob(HEADERS)):
        calls |= mutable_calls(header.read_text(encoding="utf-8"))
    names = table_names((ROOT / TEST).read_text(encoding="utf-8"))
    found = problems(calls, names)
    for problem in found:
        print(problem)
    if found:
        return 1
    print(f"host calls: {len(calls - LEFT_OUT)} calls taking the context mutable, each in s_changes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
