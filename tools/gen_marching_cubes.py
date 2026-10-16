#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Writes src/marching_table.c: marching cubes' and marching squares' cases.

The table is derived, not copied. A cube's corner i sits at (i & 1,
i >> 1 & 1, i >> 2 & 1); a corner is inside when its value is below the
level. For each of the 256 sign cases, and each choice of its ambiguous
faces (two diagonal corners inside, two outside: joined through the
inside or the outside), each face's segments between its crossing edges
are chained into closed loops, each loop oriented so that its normal
points from its edges' inside ends to their outside ends, and fanned into
triangles. Two cubes sharing a face see its four values alike, so they
decide it alike, and the loops meet: the mesh has no cracks. Before
writing, the cases are checked: random fields over grids of cubes,
decided as the library decides (the asymptotic decider), mesh with every
edge in two triangles, opposite ways. Marching squares' 16 cases follow
the same face rule.

Usage: gen_marching_cubes.py [--check]. With --check it writes nothing and
fails when the file differs from what it would write.
"""

import random
import subprocess
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "src" / "marching_table.c"

CORNERS = [(i & 1, i >> 1 & 1, i >> 2 & 1) for i in range(8)]

# Edges: four along X (by Y and Z), four along Y (by X and Z), four along
# Z (by X and Y), each from its lower corner to its upper.
EDGES = []
for axis in range(3):
    for j in range(4):
        lo = [0, 0, 0]
        others = [k for k in range(3) if k != axis]
        lo[others[0]] = j & 1
        lo[others[1]] = j >> 1 & 1
        hi = list(lo)
        hi[axis] = 1
        EDGES.append((CORNERS.index(tuple(lo)), CORNERS.index(tuple(hi))))

# Faces: -X, +X, -Y, +Y, -Z, +Z, each a cycle of corners turning right
# handed about the face's outward normal.
def _face(axis, side):
    others = [k for k in range(3) if k != axis]
    ring = [(0, 0), (1, 0), (1, 1), (0, 1)]
    out = []
    for a, b in ring:
        p = [0, 0, 0]
        p[axis] = side
        p[others[0]] = a
        p[others[1]] = b
        out.append(CORNERS.index(tuple(p)))
    # Right handed about +axis for (others[0], others[1]) in cyclic
    # order X, Y, Z; reversed for the -axis side.
    return out if side == 1 else out[::-1]


FACES = [_face(axis, side) for axis in range(3) for side in (0, 1)]
NORMALS = [tuple((1 if side else -1) if k == axis else 0 for k in range(3))
           for axis in range(3) for side in (0, 1)]


def edge_of(a, b):
    key = (min(a, b), max(a, b))
    return EDGES.index(key)


def ambiguous(case, face):
    s = [(case >> c) & 1 for c in face]
    return s[0] == s[2] and s[1] == s[3] and s[0] != s[1]


def face_segments(case, f, join_inside):
    """Face f's segments, each a pair of edge indices directed so that the
    inside lies on its left seen from outside the cube."""
    face = FACES[f]
    s = [(case >> c) & 1 for c in face]
    e = [edge_of(face[i], face[(i + 1) % 4]) for i in range(4)]
    crossing = [i for i in range(4) if s[i] != s[(i + 1) % 4]]
    if not crossing:
        return []
    if len(crossing) == 2:
        inside = [CORNERS[face[i]] for i in range(4) if s[i]]
        towards = tuple(sum(c[k] for c in inside) / len(inside) for k in range(3))
        return [direct(f, e[crossing[0]], e[crossing[1]], towards, True)]
    # Four crossings: corners 0 and 2 alike. Isolate corners 1 and 3 (join
    # 0 and 2) or isolate 0 and 2.
    inside02 = s[0] == 1
    join02 = join_inside == inside02
    if join02:
        cut = [(e[0], e[1], 1), (e[2], e[3], 3)]
    else:
        cut = [(e[3], e[0], 0), (e[1], e[2], 2)]
    return [direct(f, a, b, CORNERS[face[i]], bool(s[i])) for a, b, i in cut]


def direct(f, a, b, point, inside):
    """Segment (a, b) of face f directed so that point lies on its left
    seen from outside when inside, on its right otherwise."""
    n = NORMALS[f]
    pa, pb = midpoint(a), midpoint(b)
    d = [pb[k] - pa[k] for k in range(3)]
    left = (n[1] * d[2] - n[2] * d[1], n[2] * d[0] - n[0] * d[2], n[0] * d[1] - n[1] * d[0])
    m = [(pa[k] + pb[k]) / 2 for k in range(3)]
    side = sum(left[k] * (point[k] - m[k]) for k in range(3))
    assert side != 0.0
    return (a, b) if (side > 0.0) == inside else (b, a)


def midpoint(edge):
    a, b = EDGES[edge]
    return tuple((CORNERS[a][k] + CORNERS[b][k]) / 2 for k in range(3))


def loops(case, decisions):
    """Closed directed loops of edges for a case and its faces' decisions."""
    amb = [f for f in range(6) if ambiguous(case, FACES[f])]
    joins = {f: bool(decisions >> j & 1) for j, f in enumerate(amb)}
    after = {}
    for f in range(6):
        for a, b in face_segments(case, f, joins.get(f, False)):
            assert a not in after, (case, decisions, a)
            after[a] = b
    assert sorted(after) == sorted(after.values()), (case, decisions)
    seen = set()
    out = []
    for start in sorted(after):
        if start in seen:
            continue
        loop = [start]
        seen.add(start)
        cur = after[start]
        while cur != start:
            loop.append(cur)
            seen.add(cur)
            cur = after[cur]
        out.append(loop[::-1] if FLIP else loop)
    return out


FLIP = False
CENTERS = ([], [])


def _outward(case):
    """Whether case's first triangle faces from its inside corners out."""
    a, b, c = (midpoint(e) for e in triangles(case, 0)[0])
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    return sum(n) > 0.0


def faces_of(edge):
    a, b = EDGES[edge]
    return {f for f in range(6) if a in FACES[f] and b in FACES[f]}


def chord_ok(a, b):
    """Whether a chord between two vertices of a loop stays off the cube's
    faces: a chord on a face would lie flat on it, where the neighbor's
    triangles lie too."""
    return not (faces_of(a) & faces_of(b))


def triangulate(loop, center):
    """The loop's triangles, in its turn, no chord on a cube face: a fan
    from the first start that allows one, else the first such
    triangulation by ears, else a fan about a vertex inside the cube (id
    center: the mean of the loop's vertices), as Chernyaev's tunnels need;
    and whether the center was used."""
    n = len(loop)
    for k in range(n):
        v = loop[k:] + loop[:k]
        if all(chord_ok(v[0], v[i]) for i in range(2, n - 1)):
            return [(v[0], v[i], v[i + 1]) for i in range(1, n - 1)], False
    found = by_ears(loop)
    if found is not None:
        return found, False
    return [(center, loop[i], loop[(i + 1) % n]) for i in range(n)], True


def by_ears(loop):
    if len(loop) == 3:
        return [tuple(loop)]
    n = len(loop)
    for i in range(n):
        a, b, c = loop[i - 1], loop[i], loop[(i + 1) % n]
        if not chord_ok(a, c):
            continue
        rest = by_ears([v for j, v in enumerate(loop) if j != i])
        if rest is not None:
            return [(a, b, c)] + rest
    return None


def triangles(case, decisions):
    return mesh(case, decisions)[0]


def mesh(case, decisions):
    """A case's triangles (edges 0 to 11, centers 12 on) and its centers,
    each the list of edges whose vertices' mean it is."""
    out = []
    centers = []
    for loop in loops(case, decisions):
        tris, used = triangulate(loop, 12 + len(centers))
        out.extend(tris)
        if used:
            centers.append(loop)
    return out, centers


# Squares: corner i at (i & 1, i >> 1 & 1); edges bottom, top, left,
# right; the face's cycle right handed about +Z.
SQUARE = [0, 1, 3, 2]
SQUARE_EDGES = [(0, 1), (2, 3), (0, 2), (1, 3)]


def square_segments(case, join_inside):
    s = [(case >> c) & 1 for c in SQUARE]
    e = [SQUARE_EDGES.index((min(SQUARE[i], SQUARE[(i + 1) % 4]),
                             max(SQUARE[i], SQUARE[(i + 1) % 4]))) for i in range(4)]
    crossing = [i for i in range(4) if s[i] != s[(i + 1) % 4]]
    if not crossing:
        return []
    if len(crossing) == 2:
        pairs = [(e[crossing[0]], e[crossing[1]])]
    else:
        inside02 = s[0] == 1
        join02 = join_inside == inside02
        pairs = [(e[0], e[1]), (e[2], e[3])] if join02 else [(e[3], e[0]), (e[1], e[2])]
    # Each segment turned so the outside lies on its right: its direction
    # turned clockwise points outward.
    out = []
    for a, b in pairs:
        pa = square_mid(a)
        pb = square_mid(b)
        d = (pb[0] - pa[0], pb[1] - pa[1])
        normal = (d[1], -d[0])
        g = [0.0, 0.0]
        for edge in (a, b):
            c0, c1 = SQUARE_EDGES[edge]
            inside, outside = (c0, c1) if (case >> c0) & 1 else (c1, c0)
            for k in range(2):
                g[k] += SQUARE_CORNER[outside][k] - SQUARE_CORNER[inside][k]
        dot = normal[0] * g[0] + normal[1] * g[1]
        assert dot != 0.0
        out.append((a, b) if dot > 0.0 else (b, a))
    return out


SQUARE_CORNER = [(i & 1, i >> 1 & 1) for i in range(4)]


def square_mid(edge):
    a, b = SQUARE_EDGES[edge]
    return tuple((SQUARE_CORNER[a][k] + SQUARE_CORNER[b][k]) / 2 for k in range(2))


def decide(values, face, level=0.0):
    """Whether the face's inside corners join: its bilinear saddle inside."""
    a, b, c, d = (values[i] - level for i in face)
    num = a * c - b * d
    den = a + c - b - d
    return num != 0.0 and (num < 0.0) != (den < 0.0)


def check_cubes(trials=200, size=6, seed=7):
    """Random fields mesh with every edge in two triangles, opposite ways."""
    rng = random.Random(seed)
    for _ in range(trials):
        field = {}
        for x in range(size):
            for y in range(size):
                for z in range(size):
                    field[(x, y, z)] = rng.choice([-1, 1]) * rng.uniform(0.01, 1.0)
        directed = {}
        for x in range(size - 1):
            for y in range(size - 1):
                for z in range(size - 1):
                    values = [field[(x + c[0], y + c[1], z + c[2])] for c in CORNERS]
                    case = sum(1 << i for i in range(8) if values[i] < 0.0)
                    amb = [f for f in range(6) if ambiguous(case, FACES[f])]
                    decisions = sum(1 << j for j, f in enumerate(amb) if decide(values, FACES[f]))
                    for tri in triangles(case, decisions):
                        ids = []
                        for e in tri:
                            if e >= 12:
                                ids.append(("center", x, y, z, e))
                                continue
                            a, b = EDGES[e]
                            pa = (x + CORNERS[a][0], y + CORNERS[a][1], z + CORNERS[a][2])
                            pb = (x + CORNERS[b][0], y + CORNERS[b][1], z + CORNERS[b][2])
                            ids.append((min(pa, pb), max(pa, pb)))
                        for i in range(3):
                            key = (ids[i], ids[(i + 1) % 3])
                            directed[key] = directed.get(key, 0) + 1
        for (p, q), count in directed.items():
            assert count == 1, ("an edge twice the same way", p, q)
            if p[0] == "center" or q[0] == "center":
                assert directed.get((q, p), 0) == 1, ("a crack at a center", p, q)
                continue
            points = [p[0], p[1], q[0], q[1]]
            onface = any(all(v[k] == side for v in points)
                         for k in range(3) for side in (0, size - 1))
            assert directed.get((q, p), 0) == 1 or onface, ("a crack", p, q)


def check_squares(trials=200, size=8, seed=11):
    """Random 2D fields give closed curves: each vertex off the box's edge
    has one segment in and one out."""
    rng = random.Random(seed)
    for _ in range(trials):
        field = {(x, y): rng.choice([-1, 1]) * rng.uniform(0.01, 1.0)
                 for x in range(size) for y in range(size)}
        ins, outs = {}, {}
        for x in range(size - 1):
            for y in range(size - 1):
                values = [field[(x + c[0], y + c[1])] for c in SQUARE_CORNER]
                case = sum(1 << i for i in range(4) if values[i] < 0.0)
                s = [(case >> c) & 1 for c in SQUARE]
                amb = s[0] == s[2] and s[1] == s[3] and s[0] != s[1]
                join = amb and decide(values, SQUARE)
                for a, b in square_segments(case, join):
                    ends = []
                    for edge in (a, b):
                        c0, c1 = SQUARE_EDGES[edge]
                        p0 = (x + SQUARE_CORNER[c0][0], y + SQUARE_CORNER[c0][1])
                        p1 = (x + SQUARE_CORNER[c1][0], y + SQUARE_CORNER[c1][1])
                        ends.append((min(p0, p1), max(p0, p1)))
                    outs[ends[0]] = outs.get(ends[0], 0) + 1
                    ins[ends[1]] = ins.get(ends[1], 0) + 1
        for v in set(ins) | set(outs):
            # A vertex on an edge of the box's border has one segment.
            boundary = any(v[0][k] == v[1][k] and v[0][k] in (0, size - 1) for k in range(2))
            assert boundary or (ins.get(v, 0) == 1 and outs.get(v, 0) == 1), ("a gap", v)
            assert ins.get(v, 0) <= 1 and outs.get(v, 0) <= 1, ("a vertex met twice", v)


def table():
    variant_first = []
    triangle_first = []
    edges = []
    center_first = []
    center_edges = []
    for case in range(256):
        amb = [f for f in range(6) if ambiguous(case, FACES[f])]
        variant_first.append(len(triangle_first))
        for decisions in range(1 << len(amb)):
            triangle_first.append(len(edges) // 3)
            center_first.append(len(center_edges))
            tris, centers = mesh(case, decisions)
            # surface_mesh.c keeps MAX_CENTERS of a cube's centers.
            assert len(centers) <= 4, (case, decisions)
            for tri in tris:
                edges.extend(tri)
            for loop in centers:
                center_edges.append(len(loop))
                center_edges.extend(loop)
    triangle_first.append(len(edges) // 3)
    center_first.append(len(center_edges))
    global CENTERS
    CENTERS = (center_first, center_edges)
    square_variant_first = []
    square_first = []
    square_edges = []
    for case in range(16):
        s = [(case >> c) & 1 for c in SQUARE]
        amb = s[0] == s[2] and s[1] == s[3] and s[0] != s[1]
        square_variant_first.append(len(square_first))
        for decisions in range(2 if amb else 1):
            square_first.append(len(square_edges) // 2)
            for seg in square_segments(case, bool(decisions)):
                square_edges.extend(seg)
    square_first.append(len(square_edges) // 2)
    return (variant_first, triangle_first, edges, square_variant_first, square_first,
            square_edges)


def ints(values, per_line=16):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def render():
    (variant_first, triangle_first, edges, square_variant_first, square_first,
     square_edges) = table()
    faces = ", ".join("{" + ", ".join(str(c) for c in f) + "}" for f in FACES)
    cube_edges = ", ".join("{%d, %d}" % e for e in EDGES)
    sq_edges = ", ".join("{%d, %d}" % e for e in SQUARE_EDGES)
    return f"""// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Generated by tools/gen_marching_cubes.py; do not edit. Marching cubes'
// and marching squares' cases, each choice of its ambiguous faces apart.

#include "marching_table.h"

const uint8_t mvfxCubeEdges[12][2] = {{{cube_edges}}};

const uint8_t mvfxCubeFaces[6][4] = {{{faces}}};

const uint16_t mvfxCubeVariantFirst[256] = {{
{ints(variant_first)}
}};

const uint16_t mvfxCubeTriangleFirst[{len(triangle_first)}] = {{
{ints(triangle_first)}
}};

const uint8_t mvfxCubeTriangleEdges[{len(edges)}] = {{
{ints(edges, 24)}
}};

const uint16_t mvfxCubeCenterFirst[{len(CENTERS[0])}] = {{
{ints(CENTERS[0])}
}};

const uint8_t mvfxCubeCenterEdges[{max(len(CENTERS[1]), 1)}] = {{
{ints(CENTERS[1] or [0], 24)}
}};

const uint8_t mvfxSquareEdges[4][2] = {{{sq_edges}}};

const uint8_t mvfxSquareVariantFirst[16] = {{
{ints(square_variant_first)}
}};

const uint8_t mvfxSquareSegmentFirst[{len(square_first)}] = {{
{ints(square_first)}
}};

const uint8_t mvfxSquareSegmentEdges[{len(square_edges)}] = {{
{ints(square_edges, 24)}
}};
"""


def formatted(text):
    """The text as clang-format lays it out with the repository's style."""
    done = subprocess.run(["clang-format", f"--assume-filename={OUT}"], input=text,
                          capture_output=True, text=True, check=True, cwd=OUT.parent)
    return done.stdout


def main():
    global FLIP
    # Corner 0 alone inside: its triangle faces (1, 1, 1), away from it.
    FLIP = not _outward(1)
    check_cubes()
    check_squares()
    text = formatted(render())
    if "--check" in sys.argv[1:]:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"{OUT} is not what tools/gen_marching_cubes.py writes")
            return 1
        return 0
    OUT.write_text(text)
    triangle_first, edges = table()[1:3]
    print(f"wrote {OUT} ({len(triangle_first) - 1} variants, {len(edges) // 3} triangles)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
