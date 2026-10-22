// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface's field (waves' or shallow water's) written over a box
// of its cells in a host's format, as the core's mvfxInstance_ReadWater
// (src/water_api.c): an invocation a word of the host's buffer, which
// writes the bytes of the values within it and keeps the rest, so values
// sharing a word never race.

// include: shared/collider_numbers.wgsl

// include: shared/water_numbers.wgsl

// include: shared/formats.wgsl

// include: shared/format_bits.wgsl

const ROW: u32 = 2097152u;
const FIELD_HEIGHT: u32 = 0u;
const FIELD_VELOCITY_X: u32 = 1u;
const FIELD_NORMAL_Z: u32 = 6u;

// A read: the field, its format, the buffer current, the words written;
// the box's first cell along X and Z; its size along X and Z, and a
// value's bytes; the row pitch, the read's first byte in the buffer, the
// bytes it reaches; the range a normalized format maps from.
struct Read {
    head: vec4<u32>,
    first: vec4<u32>,
    size: vec4<u32>,
    pitch: vec4<u32>,
    range: vec4<f32>,
}

@group(0) @binding(0) var<uniform> water: Water;
@group(0) @binding(1) var<storage, read> fields: array<f32>;
@group(0) @binding(2) var<uniform> query: Read;
@group(0) @binding(3) var<storage, read_write> out: array<u32>;

// Shallow water's fields: depths, X faces and Z faces, two buffers each,
// the givers' scales, the ground.
fn faceAt(axis: u32) -> u32 {
    let b = query.head.z;
    let c = water.size.z;
    return select(2u * c + 2u * water.faces.x + b * water.faces.y, 2u * c + b * water.faces.x,
                  axis == 0u);
}

fn groundAt(i: u32) -> f32 {
    return fields[3u * water.size.z + 2u * (water.faces.x + water.faces.y) + i];
}

fn shallow() -> bool {
    return water.head.y == MODEL_SHALLOW;
}

// The surface's height at cell (x, z) over the grid's center: a wave's
// displacement, or shallow water's ground and depth.
fn surfaceAt(x: u32, z: u32) -> f32 {
    let i = z * water.size.x + x;
    let u = fields[query.head.z * water.size.z + i];
    return select(u, groundAt(i) + u, shallow());
}

// The surface's unit normal at cell (x, z): its heights' central
// differences, one-sided at the grid's edges.
fn normalAt(x: u32, z: u32) -> vec3<f32> {
    let h = water.sink.x;
    let x0 = select(x, x - 1u, x > 0u);
    let x1 = select(x, x + 1u, x + 1u < water.size.x);
    let z0 = select(z, z - 1u, z > 0u);
    let z1 = select(z, z + 1u, z + 1u < water.size.y);
    let dx = (surfaceAt(x1, z) - surfaceAt(x0, z)) / (f32(x1 - x0) * h);
    let dz = (surfaceAt(x, z1) - surfaceAt(x, z0)) / (f32(z1 - z0) * h);
    let length = sqrt(dx * dx + 1.0 + dz * dz);
    return vec3<f32>(-dx / length, 1.0 / length, -dz / length);
}

// The surface's velocity at cell (x, z): a wave's rate along Y, or shallow
// water's flow along X and Z, the mean of the cell's two faces' along each.
fn velocityAt(x: u32, z: u32) -> vec3<f32> {
    let n0 = water.size.x;
    if (!shallow()) {
        return vec3<f32>(0.0, fields[(2u + query.head.z) * water.size.z + z * n0 + x], 0.0);
    }
    let across = faceAt(0u) + z * (n0 + 1u) + x;
    let along = faceAt(1u) + z * n0 + x;
    return vec3<f32>(0.5 * (fields[across] + fields[across + 1u]), 0.0,
                     0.5 * (fields[along] + fields[along + n0]));
}

// A field's value at cell (x, z): the height, the velocity, the normal,
// the depth (a wave's 0).
fn cellValue(x: u32, z: u32) -> f32 {
    let field = query.head.x;
    if (field == FIELD_HEIGHT) {
        return water.sink.z + surfaceAt(x, z);
    }
    if (field < FIELD_VELOCITY_X + 3u) {
        return velocityAt(x, z)[field - FIELD_VELOCITY_X];
    }
    if (field <= FIELD_NORMAL_Z) {
        return normalAt(x, z)[field - FIELD_VELOCITY_X - 3u];
    }
    return select(0.0, fields[query.head.z * water.size.z + z * water.size.x + x], shallow());
}

@compute @workgroup_size(64, 1, 1)
fn waterRead(@builtin(global_invocation_id) id: vec3<u32>) {
    let w = id.x + id.y * ROW;
    if (w >= query.head.w) {
        return;
    }
    let format = query.head.y;
    let bytes = query.size.w;
    let row = query.pitch.x;
    let first = query.pitch.y;
    let word = first / 4u + w;
    var bits = out[word];
    for (var byte = 0u; byte < 4u; byte++) {
        let at = word * 4u + byte;
        if (at < first || at - first >= query.pitch.z) {
            continue;
        }
        let rel = at - first;
        let z = rel / row;
        let x = rel % row / bytes;
        if (x >= query.size.x || z >= query.size.y) {
            continue;
        }
        let value = formatBits(format, formatMapped(format,
            cellValue(query.first.x + x, query.first.y + z), query.range.xy));
        let shift = rel % row % bytes * 8u;
        let mask = 0xFFu << (byte * 8u);
        bits = (bits & ~mask) | ((((value >> shift) & 0xFFu) << (byte * 8u)) & mask);
    }
    out[word] = bits;
}
