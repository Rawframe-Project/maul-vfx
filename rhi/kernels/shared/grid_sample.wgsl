// A turn by a unit quaternion, and a grid's values sampled as the core
// samples them (src/grid.c): spliced where a kernel names it (through
// shared/forces.wgsl, or alone), after shared/finite.wgsl, beside a
// storage buffer of words named grids.

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

const BORDER_NONE: u32 = 0u;
const BORDER_REPEAT: u32 = 2u;

// Along an axis of n points: the two points a place lies between, and its
// share toward the second, as the core locates them (the repeat border's
// product in binary32: the core's binary64 one differs by less than the
// paths' places do).
fn locate(u: f32, n: u32, border: u32, low: ptr<function, u32>, high: ptr<function, u32>) -> f32 {
    if (border == BORDER_REPEAT) {
        let g = select(0.0, u, isFinite(u)) * f32(n);
        let whole = floor(g);
        let period = f32(n);
        let cell = i32(whole - period * floor(whole / period));
        *low = select(u32(cell), 0u, cell < 0 || cell >= i32(n));
        *high = select(*low + 1u, 0u, *low + 1u == n);
        return g - whole;
    }
    var clamped = 0.0;
    if (u > 0.0) {
        clamped = min(u, 1.0);
    }
    let g = clamped * f32(n - 1u);
    let cell = floor(g);
    *low = u32(cell);
    *high = select(*low, *low + 1u, *low + 1u < n);
    return g - cell;
}

// The value at index i of the grid from word first, of a format (1 for
// binary16).
fn gridValue(first: u32, format: u32, i: u32) -> f32 {
    if (format == 1u) {
        let pair = unpack2x16float(grids[first + i / 2u]);
        return select(pair.x, pair.y, (i & 1u) != 0u);
    }
    return bitcast<f32>(grids[first + i]);
}

// A grid's components (1 or 3) at box coordinate u, trilinear as the core
// samples; false outside under the none border.
fn sampleGrid(grid: vec4<u32>, format: u32, components: u32, u: vec3<f32>, border: u32,
              out: ptr<function, vec3<f32>>) -> bool {
    if (border == BORDER_NONE && !(all(u >= vec3<f32>(0.0)) && all(u <= vec3<f32>(1.0)))) {
        return false;
    }
    let size = grid.yzw;
    var low = vec3<u32>(0u);
    var high = vec3<u32>(0u);
    var share = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        var l = 0u;
        var h = 0u;
        share[k] = locate(u[k], size[k], border, &l, &h);
        low[k] = l;
        high[k] = h;
    }
    var sample = vec3<f32>(0.0);
    for (var corner = 0u; corner < 8u; corner++) {
        let i = (corner & 1u) != 0u;
        let j = (corner & 2u) != 0u;
        let k = (corner & 4u) != 0u;
        let weight = select(1.0 - share.x, share.x, i) * select(1.0 - share.y, share.y, j) *
                     select(1.0 - share.z, share.z, k);
        let point = select(low.x, high.x, i) +
                    (select(low.y, high.y, j) + select(low.z, high.z, k) * size.y) * size.x;
        for (var c = 0u; c < components; c++) {
            sample[c] += weight * gridValue(grid.x, format, components * point + c);
        }
    }
    *out = sample;
    return true;
}
