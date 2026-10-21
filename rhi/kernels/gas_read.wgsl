// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A smoke gas's field written over a box of its cells in a host's format,
// as the core's mvfxInstance_ReadGasAs (src/gas_read.c): an invocation a
// word of the host's buffer, which writes the bytes of the values within
// it and keeps the rest, so values sharing a word never race.

// include: shared/gas_numbers.wgsl

// include: shared/formats.wgsl

// include: shared/format_bits.wgsl

const ROW: u32 = 2097152u;

// A read: the field, its format, the buffer current, the words written;
// the box's first cell and its size, and a value's bytes; the row and
// layer pitches, the read's first byte in the buffer, the bytes it
// reaches; the range a normalized format maps from.
struct Read {
    head: vec4<u32>,
    first: vec4<u32>,
    size: vec4<u32>,
    pitch: vec4<u32>,
    range: vec4<f32>,
}

@group(0) @binding(0) var<uniform> gas: Gas;
@group(0) @binding(1) var<storage, read> fields: array<f32>;
@group(0) @binding(2) var<uniform> query: Read;
@group(0) @binding(3) var<storage, read_write> out: array<u32>;

// A field's value at a cell: a scalar's, or a velocity's component the
// mean of the two faces' about the center (0 across an axis of one cell).
fn cellValue(at: vec3<u32>) -> f32 {
    let n = gas.size.xyz;
    let field = query.head.x;
    let b = query.head.z;
    if (field < 3u) {
        return fields[gas.cells[b][field] + (at.z * n.y + at.y) * n.x + at.x];
    }
    let k = field - 3u;
    if (n[k] == 1u) {
        return 0.0;
    }
    var m = n;
    m[k] += 1u;
    let step = vec3<u32>(1u, m.x, m.x * m.y);
    let low = gas.faces[b][k] + (at.z * m.y + at.y) * m.x + at.x;
    return 0.5 * (fields[low] + fields[low + step[k]]);
}

@compute @workgroup_size(64, 1, 1)
fn gasRead(@builtin(global_invocation_id) id: vec3<u32>) {
    let w = id.x + id.y * ROW;
    if (w >= query.head.w) {
        return;
    }
    let format = query.head.y;
    let bytes = query.size.w;
    let row = query.pitch.x;
    let layer = query.pitch.y;
    let first = query.pitch.z;
    let word = first / 4u + w;
    var bits = out[word];
    for (var byte = 0u; byte < 4u; byte++) {
        let at = word * 4u + byte;
        if (at < first || at - first >= query.pitch.w) {
            continue;
        }
        let rel = at - first;
        let z = rel / layer;
        let y = rel % layer / row;
        let x = rel % layer % row / bytes;
        if (x >= query.size.x || y >= query.size.y || z >= query.size.z) {
            continue;
        }
        let cell = query.first.xyz + vec3<u32>(x, y, z);
        let value = formatBits(format, formatMapped(format, cellValue(cell), query.range.xy));
        let shift = rel % layer % row % bytes * 8u;
        let mask = 0xFFu << (byte * 8u);
        bits = (bits & ~mask) | ((((value >> shift) & 0xFFu) << (byte * 8u)) & mask);
    }
    out[word] = bits;
}
