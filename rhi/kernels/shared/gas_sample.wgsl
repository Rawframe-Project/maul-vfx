// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's lattice sampled at a place, as the core's mvfxGasSample (after
// shared/gas_numbers.wgsl and shared/gas_lattice.wgsl, in a kernel with
// the gas's fields).

// A lattice's values at place p (in cells): the cells' for axis 3, else
// the faces' across axis, as mvfxGasSample: trilinear, p held within the
// array's points (a 2D array's four terms alone, which are the trilinear
// sum's bit for bit).
fn sampleAt(start: u32, axis: u32, p: vec3<f32>) -> f32 {
    let m = pointsOf(sizeOf(), axis);
    var low: vec3<u32>;
    var high: vec3<u32>;
    var t: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        let shift = select(0.5, 0.0, k == axis);
        let place = min(max(p[k] - shift, 0.0), f32(m[k] - 1u));
        low[k] = u32(place);
        high[k] = select(low[k], low[k] + 1u, low[k] + 1u < m[k]);
        t[k] = place - f32(low[k]);
    }
    var wx = vec2<f32>(1.0 - t.x, t.x);
    var wy = vec2<f32>(1.0 - t.y, t.y);
    var x = vec2<u32>(low.x, high.x);
    if (m.z == 1u) {
        let lower = start + low.y * m.x;
        let upper = start + high.y * m.x;
        var sum = 0.0;
        sum += wx[0] * wy[0] * fields[lower + x[0]];
        sum += wx[1] * wy[0] * fields[lower + x[1]];
        sum += wx[0] * wy[1] * fields[upper + x[0]];
        sum += wx[1] * wy[1] * fields[upper + x[1]];
        return sum;
    }
    var wz = vec2<f32>(1.0 - t.z, t.z);
    var y = vec2<u32>(low.y, high.y);
    var z = vec2<u32>(low.z, high.z);
    var sum = 0.0;
    for (var corner = 0u; corner < 8u; corner++) {
        let a = corner & 1u;
        let b = (corner >> 1u) & 1u;
        let d = corner >> 2u;
        let weight = wx[a] * wy[b] * wz[d];
        sum += weight * fields[start + (z[d] * m.y + y[b]) * m.x + x[a]];
    }
    return sum;
}
