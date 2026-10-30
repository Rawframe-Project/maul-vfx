#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Builds the GPU part's kernel containers offline: naga compiles each
# rhi/kernels/NAME.wgsl (and each test-only one, test/kernels/NAME.wgsl) to
# SPIR-V, spirv-val checks it for Vulkan 1.3, and
# Maul RHI's tools cross it to DXIL (spirv-cross and dxc) and Metal
# (spirv-cross) and write the container with the WGSL beside it, which
# lands in NAME_container.h beside it as bytes, its bindings described by
# NAME.json. A line "// include: PATH.wgsl" in a kernel is replaced by
# rhi/kernels/PATH.wgsl before naga reads it, a file once, and the
# container carries the result: shared/ holds what several kernels splice
# (functions with no bindings), a kernel's own directory (sim/) the parts
# only it splices, cut at its top-level declarations.
# WGSL is the kernels' one source (the GPU path's decision on shaders).
# CI runs none of these tools; the headers are committed, and this script
# is run again whenever a kernel changes, with naga, spirv-val,
# spirv-cross and dxc on the path.
#
# Each directory of containers has a stamp, containers.stamp: the Maul RHI
# release whose tools cooked them, the flags its DXC compiles passed, and
# each container's SHA-256. The container format has no place for them,
# and DXIL compiled without -Gis cannot be told apart by its bytes: DXC
# without it marks every float operation fast and folds the math, so
# D3D12 would compute other than the kernels' source and the other APIs.
# This refuses to cook with a Maul RHI tree other than the release
# cmake/Rhi.cmake pins, or one whose mrhi_dxil.py passes no -Gis, and to
# cook some kernels only where the stamp names another release or other
# flags, so that a move of Maul RHI cooks every kernel again.
#
# With --check it needs none of the tools: each committed container must
# carry a fresh splice of its kernel's sources, byte for byte, so a part
# or shared file edited without a rebuild fails the gate and CI. It also
# refuses a stamp of another release than the pin, or without -Gis, a
# container that is missing, not in its stamp or not the bytes stamped,
# and a stamped name no kernel gives.
#
# usage: gen_kernels.py <a Maul RHI source tree, for its tools/> [NAME ...]
#        gen_kernels.py --check

import ast
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNELS = os.path.join(ROOT, "rhi", "kernels")
TEST_KERNELS = os.path.join(ROOT, "test", "kernels")
INCLUDE = "// include: "
STAMP = "containers.stamp"


def folder(name):
    """The directory a kernel's sources are in: the part's, or the tests'."""
    tested = os.path.join(TEST_KERNELS, f"{name}.wgsl")
    return TEST_KERNELS if os.path.exists(tested) else KERNELS


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{command[0]} failed:\n{result.stdout}{result.stderr}")


def spliced(path, seen):
    """A kernel's WGSL with each included file in place of its line, a file
    once."""
    lines = []
    with open(path, encoding="utf-8") as file:
        for line in file.read().split("\n"):
            if not line.startswith(INCLUDE):
                lines.append(line)
                continue
            name = line[len(INCLUDE):].strip()
            if name in seen:
                sys.exit(f"{path}: {name} included twice")
            seen.add(name)
            lines.append(spliced(os.path.join(KERNELS, name), seen).rstrip("\n"))
    return "\n".join(lines)


def build(work, tools, name):
    source = os.path.join(work, f"{name}.wgsl")
    with open(source, "w", encoding="utf-8", newline="\n") as file:
        file.write(spliced(os.path.join(folder(name), f"{name}.wgsl"), set()))
    module = os.path.join(work, f"{name}.spv")
    # Restrict holds every index into an array, a buffer or a texture in
    # range, so a kernel's result is defined for any input on every driver.
    run("naga", "--index-bounds-check-policy", "Restrict", source, module)
    run("spirv-val", "--target-env", "vulkan1.3", module)
    reflection = os.path.join(folder(name), f"{name}.json")
    dxil = os.path.join(work, "dxil")
    msl = os.path.join(work, "msl")
    run(sys.executable, os.path.join(tools, "mrhi_dxil.py"), module, reflection, dxil)
    run(sys.executable, os.path.join(tools, "mrhi_msl.py"), module, reflection, msl)
    container = os.path.join(work, f"{name}.mrsc")
    run(sys.executable, os.path.join(tools, "mrhi_container.py"), "--dxil", dxil, "--msl", msl,
        module, source, reflection, container)
    with open(container, "rb") as file:
        return file.read()


def header(name, data):
    symbol = "".join(part.capitalize() for part in name.split("_"))
    guard = f"MAUL_VFX_RHI_{name.upper()}_CONTAINER_H"
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        f"// The {name} kernel's container, generated by tools/gen_kernels.py from",
        f"// {os.path.relpath(folder(name), ROOT)}/{name}.wgsl and {name}.json: edit the sources and",
        "// run the script, not this file.",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdalign.h>",
        "#include <stdint.h>",
        "",
        f"alignas(8) static const uint8_t s_{symbol[0].lower() + symbol[1:]}Container[{len(data)}] = {{",
    ]
    for at in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in data[at:at + 16]))
    lines += ["};", "", f"#endif // {guard}", ""]
    return "\n".join(lines)


def kernel_names():
    return sorted(f[:-5] for d in (KERNELS, TEST_KERNELS) for f in os.listdir(d)
                  if f.endswith(".wgsl"))


def pinned_release():
    """The Maul RHI release the build takes (cmake/Rhi.cmake)."""
    with open(os.path.join(ROOT, "cmake", "Rhi.cmake"), encoding="utf-8") as file:
        found = re.search(r"^set\(MAUL_VFX_RHI_VERSION (\S+)\)$", file.read(), re.M)
    if found is None:
        sys.exit("cmake/Rhi.cmake sets no MAUL_VFX_RHI_VERSION")
    return found.group(1)


def tree_release(tree):
    """A Maul RHI tree's release, from its version macros."""
    with open(os.path.join(tree, "include", "maul-rhi", "base.h"), encoding="utf-8") as file:
        text = file.read()
    parts = [re.search(rf"^#define MRHI_VERSION_{part} (\d+)$", text, re.M)
             for part in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(part.group(1) for part in parts) if all(parts) else None


def dxc_flags(tree):
    """The DXC_FLAGS a Maul RHI tree's mrhi_dxil.py assigns, read from its
    source without running it; None when it assigns no tuple of strings."""
    with open(os.path.join(tree, "tools", "mrhi_dxil.py"), encoding="utf-8") as file:
        module = ast.parse(file.read())
    for node in module.body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1 and
                isinstance(node.targets[0], ast.Name) and node.targets[0].id == "DXC_FLAGS"):
            try:
                flags = ast.literal_eval(node.value)
            except ValueError:
                return None
            if isinstance(flags, tuple) and all(isinstance(flag, str) for flag in flags):
                return flags
            return None
    return None


def directories():
    """Each directory of containers and the kernels whose containers it
    holds."""
    held = {KERNELS: [], TEST_KERNELS: []}
    for name in kernel_names():
        held[folder(name)].append(name)
    return held


def read_stamp(directory):
    """A stamp's release, its DXC flags (a string) and each container's
    SHA-256 by kernel name; None when it is missing."""
    path = os.path.join(directory, STAMP)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as file:
        text = file.read()
    release = re.search(r"^maul-rhi (\S+)$", text, re.M)
    flags = re.search(r"^dxc-flags (.*)$", text, re.M)
    return (release.group(1) if release else None, flags.group(1) if flags else None,
            dict(re.findall(r"^container (\S+) ([0-9a-f]{64})$", text, re.M)))


def write_stamp(directory, names, cooked, release, flags):
    """Writes a directory's stamp: the digests of the containers cooked
    now, and for the others the ones the old stamp holds, so a cook by name
    vouches for no container it did not make; one the old stamp lacks is
    left out, and --check refuses it."""
    old = read_stamp(directory)
    kept = old[2] if old is not None else {}
    lines = [
        "# Written by tools/gen_kernels.py and read by its --check: the Maul RHI",
        "# release whose tools cooked these containers, the flags its DXC",
        "# compiles passed, and each container's SHA-256.",
        f"maul-rhi {release}",
        f"dxc-flags {' '.join(flags)}",
    ]
    for name in sorted(names):
        data = committed(name) if name in cooked else None
        digest = hashlib.sha256(data).hexdigest() if data is not None else kept.get(name)
        if digest is not None:
            lines.append(f"container {name} {digest}")
    with open(os.path.join(directory, STAMP), "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines) + "\n")


def stamp_problems():
    """What in the containers no longer matches their stamps."""
    found = []
    release = pinned_release()
    for directory, names in directories().items():
        shown = os.path.relpath(os.path.join(directory, STAMP), ROOT)
        stamp = read_stamp(directory)
        if stamp is None:
            found.append(f"{shown}: missing")
            continue
        stamped, flags, hashes = stamp
        if stamped != release:
            found.append(f"{shown}: cooked with Maul RHI {stamped or 'of no release'}, "
                         f"cmake/Rhi.cmake pins {release}")
        if flags is None or "-Gis" not in flags.split():
            found.append(f"{shown}: its DXIL was compiled without -Gis")
        for name in names:
            data = committed(name)
            if data is None:
                found.append(f"{name}_container.h: missing")
            elif name not in hashes:
                found.append(f"{name}_container.h: not in {shown}")
            elif hashes[name] != hashlib.sha256(data).hexdigest():
                found.append(f"{name}_container.h: not the bytes {shown} names")
        for name in sorted(set(hashes) - set(names)):
            found.append(f"{shown}: names {name}, which no kernel gives")
    return found


def committed(name):
    """The bytes of a kernel's committed container, read from its header."""
    path = os.path.join(folder(name), f"{name}_container.h")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as file:
        text = file.read()
    body = text[text.index("= {") + 3:text.index("};")]
    return bytes(int(word, 16) for word in body.replace(",", " ").split())


# WGSL the crossing tools break on though naga takes it: spirv-cross
# 2021.01.15 (Vulkan SDK 1.3.239, as Debian and Ubuntu ship it) writes an
# OpAtomicStore in a branch or a called function as an InterlockedExchange
# into a result it never declares, which dxc refuses, so kernels store an
# atomic by `_ = atomicExchange(&a, v);`. The check refuses the word, as
# CI runs no dxc; it goes when the pinned spirv-cross writes it right.
REFUSED = {"atomicStore": "write `_ = atomicExchange(&a, v);` (spirv-cross 2021.01.15's HLSL)"}


def check():
    stale = []
    for name in kernel_names():
        data = committed(name)
        text = spliced(os.path.join(folder(name), f"{name}.wgsl"), set())
        for word, instead in REFUSED.items():
            if word in text:
                sys.exit(f"{name}.wgsl uses {word}: {instead}")
        if data is None or text.encode("utf-8") not in data:
            stale.append(name)
    if stale:
        sys.exit(f"stale kernel containers (run gen_kernels.py): {' '.join(stale)}")
    found = stamp_problems()
    if found:
        sys.exit("kernel containers their stamps do not vouch for (run gen_kernels.py "
                 "with the pinned Maul RHI):\n  " + "\n  ".join(found))


def main():
    if sys.argv[1:] == ["--check"]:
        check()
        return
    if len(sys.argv) < 2:
        sys.exit("usage: gen_kernels.py <a Maul RHI source tree> [NAME ...]")
    tree = os.path.abspath(sys.argv[1])
    tools = os.path.join(tree, "tools")
    release = tree_release(tree)
    if release != pinned_release():
        sys.exit(f"{tree} is Maul RHI {release or 'of no release'}; cmake/Rhi.cmake pins "
                 f"{pinned_release()}")
    flags = dxc_flags(tree)
    if flags is None or "-Gis" not in flags:
        sys.exit(f"{tools}/mrhi_dxil.py compiles DXIL without -Gis: D3D12 would fold the float "
                 "math the other APIs keep")
    names = sys.argv[2:] or kernel_names()
    for directory in directories():
        stamp = read_stamp(directory)
        if sys.argv[2:] and (stamp is None or stamp[:2] != (release, " ".join(flags))):
            sys.exit(f"{os.path.relpath(os.path.join(directory, STAMP), ROOT)} names another "
                     "Maul RHI release or other DXC flags: cook every kernel")
    for name in names:
        with tempfile.TemporaryDirectory() as work:
            data = build(work, tools, name)
        path = os.path.join(folder(name), f"{name}_container.h")
        with open(path, "w", encoding="utf-8", newline="\n") as file:
            file.write(header(name, data))
        print(f"{os.path.relpath(path, ROOT)}: {len(data)} bytes")
    for directory, held in directories().items():
        write_stamp(directory, held, set(names), release, flags)


if __name__ == "__main__":
    main()
