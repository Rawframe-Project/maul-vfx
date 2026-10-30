// The exact floor of a place over a cell, as the core's mvfxGridCell
// (src/neighbor.h), which the grid kernels and the sim kernel's rigid
// pairs share (tools/gen_kernels.py splices it where a kernel names it).

// The greatest cell coordinate in size (MVFX_MAX_CELL).
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
// values), clamped to MAX_CELL as the core's mvfxGridCell. A product
// rounded to 0 is below it for a negative x, told by its bits, which a
// device flushing subnormals keeps. A rounded product t that is not an
// integer is below 2^23, and no integer lies between it and the exact
// product, so their floors are equal. An integer t is at most MAX_CELL
// here; past 2^24 binary32 values are 2 or more apart, so the exact
// product may lie more than one from t either way, and t less one may not
// be a binary32 value. The cell is then taken in integers: t plus the
// floor of the exact remainder, itself a binary32 value. A factor past
// 2^100 (a far place, or the inverse of a cell below 2^-100, which
// contacts and self collision allow) is scaled by 2^-64 and the other one
// by 2^64 first, both exactly, so that no split overflows: the other one
// is below 2^-70 there, and below 2^-6 scaled.
fn cellOf(x: f32, inverse: f32) -> i32 {
    let t = x * inverse;
    if (!(t >= -MAX_CELL)) {
        return -1073741824;
    }
    if (t > MAX_CELL) {
        return 1073741824;
    }
    if (t == 0.0) {
        return select(0, -1, bitcast<u32>(x) > 0x80000000u);
    }
    let c = floor(t);
    if (c != t) {
        return i32(c);
    }
    let farPlace = abs(x) >= 0x1p100f;
    let farInverse = abs(inverse) >= 0x1p100f;
    let a = x * select(select(1.0, 0x1p64f, farInverse), 0x1p-64f, farPlace);
    let b = inverse * select(select(1.0, 0x1p64f, farPlace), 0x1p-64f, farInverse);
    let below = productBelow(a, b, t);
    return clamp(i32(t) + i32(floor(below)), -1073741824, 1073741824);
}
