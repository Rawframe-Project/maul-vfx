#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# pcg4d computed from Jarzynski and Olano's listing ("Hash Functions for
# GPU Rendering", JCGT 9(3), 2020), in Python's unbounded integers masked
# to 32 bits: an implementation independent of the library's C, which
# test/test_random.c checks against the vectors this prints.
#
# usage: pcg4d_vectors.py

MASK = 0xFFFFFFFF


def pcg4d(v):
    v = [(x * 1664525 + 1013904223) & MASK for x in v]
    x, y, z, w = v
    x = (x + y * w) & MASK
    y = (y + z * x) & MASK
    z = (z + x * y) & MASK
    w = (w + y * z) & MASK
    x ^= x >> 16
    y ^= y >> 16
    z ^= z >> 16
    w ^= w >> 16
    x = (x + y * w) & MASK
    y = (y + z * x) & MASK
    z = (z + x * y) & MASK
    w = (w + y * z) & MASK
    return [x, y, z, w]


INPUTS = [
    [0, 0, 0, 0],
    [1, 2, 3, 4],
    [MASK, MASK, MASK, MASK],
    [0x12345678, 0x9ABCDEF0, 0x0F1E2D3C, 0x4B5A6978],
    [42, 7, 1000000, 3],
    [0x80000000, 1, 0x7FFFFFFF, 0xDEADBEEF],
]

for inputs in INPUTS:
    out = pcg4d(inputs)
    print("    {{0x%08Xu, 0x%08Xu, 0x%08Xu, 0x%08Xu}, {0x%08Xu, 0x%08Xu, 0x%08Xu, 0x%08Xu}},"
          % tuple(inputs + out))
