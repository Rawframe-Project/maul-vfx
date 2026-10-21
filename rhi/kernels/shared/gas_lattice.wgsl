// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's lattice as its kernels walk it (after shared/gas_numbers.wgsl):
// the points of its cells (axis 3) or of its faces across an axis, a
// point's index and place, its counts, a pass over every face, and the
// faces its closed walls hold.

// The points of an array along each axis: the cells' (axis 3), or the
// faces' across axis.
fn pointsOf(n: vec3<u32>, axis: u32) -> vec3<u32> {
    var m = n;
    if (axis < 3u) {
        m[axis] += 1u;
    }
    return m;
}

fn indexOf(m: vec3<u32>, at: vec3<u32>) -> u32 {
    return (at.z * m.y + at.y) * m.x + at.x;
}

fn pointOf(m: vec3<u32>, i: u32) -> vec3<u32> {
    return vec3<u32>(i % m.x, i / m.x % m.y, i / (m.x * m.y));
}

fn sizeOf() -> vec3<u32> {
    return gas.size.xyz;
}

fn cellCount(n: vec3<u32>) -> u32 {
    return n.x * n.y * n.z;
}

fn faceCount(n: vec3<u32>, k: u32) -> u32 {
    let m = pointsOf(n, k);
    return m.x * m.y * m.z;
}

// Which axis's faces point i of a pass over every face lies on (3 past
// them), and its index among them; an axis of one cell has none.
fn faceAxis(n: vec3<u32>, i: u32) -> vec2<u32> {
    var first = 0u;
    for (var k = 0u; k < 3u; k++) {
        let count = select(0u, faceCount(n, k), n[k] > 1u);
        if (i < first + count) {
            return vec2<u32>(k, i - first);
        }
        first += count;
    }
    return vec2<u32>(3u, 0u);
}

fn allFaces(n: vec3<u32>) -> u32 {
    var count = 0u;
    for (var k = 0u; k < 3u; k++) {
        count += select(0u, faceCount(n, k), n[k] > 1u);
    }
    return count;
}

// Whether face index f across k at at lies on a closed wall (an axis of
// one cell's all do).
fn isClosed(k: u32, at: vec3<u32>) -> bool {
    let n = sizeOf();
    let walls = gas.walls.x;
    return n[k] == 1u || (at[k] == 0u && (walls & (1u << (2u * k))) != 0u) ||
           (at[k] == n[k] && (walls & (2u << (2u * k))) != 0u);
}
