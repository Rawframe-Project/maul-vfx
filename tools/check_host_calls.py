#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Refuses a public call taking the context mutable that no test calls
back in from host code.

The contract: a call that takes the context mutable is refused with
mvfx_errorBusy, counted as misuse, while the context runs host code (the
allocator, the task hooks, a host module), wherever the call lives. Each
suite calls each such call of its headers from a host module through one
table, s_changes, by its public name: test/test_host_calls.c those of
include/maul-vfx/, test/test_rhi_host_calls.c those of the GPU part's
rhi/include/maul-vfx-rhi/. This lists every public function of those
headers with a parameter `mvfxContext* name`, not const, in any place, and
refuses one its table lacks, and a name in a table that is no such
function of its headers. The context's own destruction is left out: the
test's callbacks destroy it and count its refusal apart.

Usage: check_host_calls.py [--self-test]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Each part's headers and the suite whose table calls their calls.
PARTS = (("include/maul-vfx/*.h", "test/test_host_calls.c"),
         ("rhi/include/maul-vfx-rhi/*.h", "test/test_rhi_host_calls.c"))
DECLARATION = re.compile(r"MVFX(?:_RHI)?_API\b[^;(]*?\b(mvfx\w+)\s*\(([^;{]*?)\)\s*;", re.S)
MUTABLE = re.compile(r"^\s*mvfxContext\s*\*\s*\w+\s*$")
TABLE = re.compile(r"\bs_changes\[\]\s*=\s*\{(.*?)\n\};", re.S)
NAME = re.compile(r'"(mvfx\w+)"')
LEFT_OUT = {"mvfxDestroyContext"}


def mutable_calls(text):
    """The functions declared in text with a parameter that is the
    context, not const, in any place."""
    return {m.group(1) for m in DECLARATION.finditer(text)
            if any(MUTABLE.match(p) for p in " ".join(m.group(2).split()).split(","))}


def table_names(text):
    """The names the s_changes table holds, or None without a table."""
    m = TABLE.search(text)
    return None if m is None else set(NAME.findall(m.group(1)))


def problems(suite, calls, names):
    """Each call the suite's table lacks and each name it holds that is no
    call."""
    if names is None:
        return [f"{suite} has no s_changes table"]
    wanted = calls - LEFT_OUT
    out = [f"{call}: takes the context mutable, missing from {suite}'s s_changes"
           for call in sorted(wanted - names)]
    out += [f"{name}: in {suite}'s s_changes, but no public call of its headers taking the "
            "context mutable" for name in sorted(names - wanted)]
    return out


def self_test():
    header = """
    MVFX_NODISCARD MVFX_API mvfxResult mvfxCreateThing(mvfxContext* context,
                                                       mvfxThingId* thingOut);
    MVFX_NODISCARD MVFX_API mvfxResult mvfxThing_Get(const mvfxContext* context, int32_t* out);
    MVFX_API void mvfxDestroyContext(mvfxContext* context);
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuThing_Report(const mvfxGpuThing* thing,
                                                               mvfxContext* context, int32_t at);
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuThing_Read(mvfxGpuThing* thing,
                                                             const mvfxContext* context);
    MVFX_API int32_t mvfxGetVersion(void);
    """
    calls = mutable_calls(header)
    assert calls == {"mvfxCreateThing", "mvfxDestroyContext", "mvfxGpuThing_Report"}, calls
    whole = ('static const struct { Change call; } s_changes[] = {\n    {A, "mvfxCreateThing"},\n'
             '    {B, "mvfxGpuThing_Report"},\n};')
    assert problems("t.c", calls, table_names(whole)) == []
    short = 'static const struct { Change call; } s_changes[] = {\n    {A, "mvfxCreateThing"},\n};'
    assert problems("t.c", calls, table_names(short)) == [
        "mvfxGpuThing_Report: takes the context mutable, missing from t.c's s_changes"]
    stale = whole.replace('"mvfxGpuThing_Report"', '"mvfxGpuThing_Report"},\n    {C, "mvfxThing_Get"')
    assert problems("t.c", calls, table_names(stale)) == [
        "mvfxThing_Get: in t.c's s_changes, but no public call of its headers taking the context "
        "mutable"]
    assert problems("t.c", calls, table_names("no table")) == ["t.c has no s_changes table"]
    print("check_host_calls: self-test passed")


def main():
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return 0
    found = []
    counts = []
    for headers, suite in PARTS:
        calls = set()
        for header in sorted(ROOT.glob(headers)):
            calls |= mutable_calls(header.read_text(encoding="utf-8"))
        names = table_names((ROOT / suite).read_text(encoding="utf-8"))
        found += problems(suite, calls, names)
        counts.append(f"{len(calls - LEFT_OUT)} in {suite}")
    for problem in found:
        print(problem)
    if found:
        return 1
    print("host calls: every call taking the context mutable in its table, " + ", ".join(counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
