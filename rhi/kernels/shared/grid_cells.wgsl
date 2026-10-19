// A neighbor grid's cells and keys, as the core's (src/neighbor.h), which
// the grid and contacts kernels share (tools/gen_kernels.py splices it
// where a kernel names it).

// include: shared/pcg4d_hash.wgsl

// The salt that sets grid keys apart (the core's MVFX_GRID_SALT), and the
// greatest cell coordinate in size (MVFX_MAX_CELL).
const GRID_SALT: u32 = 0x4E474244u;
const MAX_CELL: f32 = 1073741824.0;

// A binary32 value's halves by Veltkamp's split, 12 bits each, so that
// products of halves are exact.
fn halvesOf(a: f32) -> vec2<f32> {
    let c = 4097.0 * a;
    let high = c - (c - a);
    return vec2<f32>(high, a - high);
}

// The exact remainder of x times y less t, their product rounded, by
// Dekker's TwoProduct: each product of halves is exact, and so is the sum.
// WGSL's fma may round twice (lavapipe's does), so it cannot give this;
// a compiler fusing these terms leaves them exact.
fn productBelow(x: f32, y: f32, t: f32) -> f32 {
    let a = halvesOf(x);
    let b = halvesOf(y);
    return ((a.x * b.x - t) + a.x * b.y + a.y * b.x) + a.y * b.y;
}

// The binary64 floor of x times inverse (exact: a product of two binary32
// values), clamped to MAX_CELL as the core's mvfxGridCell: the rounded
// product, one less where it is an integer the exact product lies below.
// A product rounded to 0 is below it for a negative x, told by its bits,
// which a device flushing subnormals keeps.
fn cellOf(x: f32, inverse: f32) -> i32 {
    let t = x * inverse;
    var c = floor(t);
    if (c == t) {
        let negative = select(productBelow(x, inverse, t) < 0.0, bitcast<u32>(x) > 0x80000000u,
                              t == 0.0);
        c -= select(0.0, 1.0, negative);
    }
    c = select(-MAX_CELL, c, c >= -MAX_CELL);
    return i32(min(c, MAX_CELL));
}

fn keyOf(cell: vec3<i32>, mask: u32) -> u32 {
    let c = bitcast<vec3<u32>>(cell);
    return pcg4d(vec4<u32>(c, GRID_SALT)).x & mask;
}
