// Forces as the core resolves them (src/force.c's mvfxForcesAt, each
// force's acceleration and drag at a place): spliced where a kernel names
// it, after shared/pcg4d_hash.wgsl, shared/force_numbers.wgsl and the
// kernel's fieldOf(n), a force's field among its own numbers, beside a
// storage buffer of words named grids that vector fields and gases read.

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

const FORCE_CONSTANT: u32 = 0u;
const FORCE_WIND: u32 = 1u;
const FORCE_ATTRACTOR: u32 = 2u;
const FORCE_VORTEX: u32 = 3u;
const FORCE_TURBULENCE: u32 = 4u;
const FORCE_GAS: u32 = 6u;
const FIELD_VELOCITY: u32 = 1u;
const FALLOFF_NONE: u32 = 0u;
const FALLOFF_TUBE: u32 = 2u;
const FALLOFF_CONE: u32 = 3u;

// The weight (1 - share)^exponent, 0 at or past the edge.
fn weightOf(share: f32, exponent: f32) -> f32 {
    if (!(share < 1.0)) {
        return 0.0;
    }
    return select(pow(1.0 - share, exponent), 1.0, exponent == 0.0);
}

// A force's falloff weight at x.
fn falloffAt(f: Force, x: vec3<f32>) -> f32 {
    let kind = f.modes.y;
    if (kind == FALLOFF_NONE) {
        return 1.0;
    }
    let d = x - f.center.xyz;
    let distance = sqrt(dot(d, d));
    let along = dot(d, f.axis.xyz);
    if (kind == FALLOFF_TUBE) {
        let across = d - along * f.axis.xyz;
        return weightOf(sqrt(dot(across, across)) / f.center.w, f.extra.z);
    }
    var weight = weightOf(distance / f.center.w, f.extra.z);
    if (kind == FALLOFF_CONE && distance > 0.0) {
        // The angle's share by its cosine: 0 on the axis, 1 at the edge.
        let share = (1.0 - along / distance) / (1.0 - f.axis.w);
        weight *= weightOf(share, f.extra.z);
    }
    return weight;
}

// The attractor's acceleration at x.
fn attract(f: Force, x: vec3<f32>, weight: f32) -> vec3<f32> {
    let d = f.point.xyz - x;
    let distance = sqrt(dot(d, d));
    if (!(distance > 0.0)) {
        return vec3<f32>(0.0);
    }
    let softening = f.point.w;
    let ratio = softening / distance;
    let magnitude = select(f.vector.w * ratio * ratio, f.vector.w / softening * distance,
                           distance < softening);
    return d * (weight * magnitude / distance);
}

// The vortex's acceleration at x.
fn swirl(f: Force, x: vec3<f32>, weight: f32) -> vec3<f32> {
    let u = f.vector.xyz;
    let r = x - f.point.xyz;
    let across = r - dot(r, u) * u;
    let distance = sqrt(dot(across, across));
    if (!(distance > 0.0)) {
        return vec3<f32>(0.0);
    }
    let inverse = 1.0 / distance;
    return weight * inverse * (f.vector.w * cross(u, across) - f.extra.y * across);
}

// The twelve edges of a cube, four repeated to fill sixteen (Perlin 2002).
const GRADIENTS = array<vec3<f32>, 16>(
    vec3<f32>(1.0, 1.0, 0.0), vec3<f32>(-1.0, 1.0, 0.0), vec3<f32>(1.0, -1.0, 0.0),
    vec3<f32>(-1.0, -1.0, 0.0), vec3<f32>(1.0, 0.0, 1.0), vec3<f32>(-1.0, 0.0, 1.0),
    vec3<f32>(1.0, 0.0, -1.0), vec3<f32>(-1.0, 0.0, -1.0), vec3<f32>(0.0, 1.0, 1.0),
    vec3<f32>(0.0, -1.0, 1.0), vec3<f32>(0.0, 1.0, -1.0), vec3<f32>(0.0, -1.0, -1.0),
    vec3<f32>(1.0, 1.0, 0.0), vec3<f32>(-1.0, 1.0, 0.0), vec3<f32>(0.0, -1.0, 1.0),
    vec3<f32>(0.0, -1.0, -1.0));

// 6 t^5 - 15 t^4 + 10 t^3 and its derivative.
fn fade(t: vec3<f32>) -> vec3<f32> {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

fn fadeSlope(t: vec3<f32>) -> vec3<f32> {
    let u = t * (t - 1.0);
    return 30.0 * u * u;
}

// One octave's curl at a lattice cell and the fraction past it: the six
// gradient entries the curl reads, summed over the corners in the core's
// operations and order.
fn curlOctave(cell: vec3<u32>, f: vec3<f32>, seed: u32) -> vec3<f32> {
    let w1 = fade(f);
    let s1 = fadeSlope(f);
    var g21 = 0.0;
    var g12 = 0.0;
    var g02 = 0.0;
    var g20 = 0.0;
    var g10 = 0.0;
    var g01 = 0.0;
    for (var corner = 0u; corner < 8u; corner++) {
        let i = corner & 1u;
        let j = (corner >> 1u) & 1u;
        let k = (corner >> 2u) & 1u;
        let h = pcg4d(vec4<u32>(cell.x + i, cell.y + j, cell.z + k, seed));
        let d = f - vec3<f32>(f32(i), f32(j), f32(k));
        let wx = select(1.0 - w1.x, w1.x, i != 0u);
        let wy = select(1.0 - w1.y, w1.y, j != 0u);
        let wz = select(1.0 - w1.z, w1.z, k != 0u);
        let sx = select(-s1.x, s1.x, i != 0u);
        let sy = select(-s1.y, s1.y, j != 0u);
        let sz = select(-s1.z, s1.z, k != 0u);
        let weight = wx * wy * wz;
        let dw = vec3<f32>(sx * wy * wz, wx * sy * wz, wx * wy * sz);
        let a = GRADIENTS[h.x >> 28u];
        let b = GRADIENTS[h.y >> 28u];
        let c = GRADIENTS[h.z >> 28u];
        let da = a.x * d.x + a.y * d.y + a.z * d.z;
        let db = b.x * d.x + b.y * d.y + b.z * d.z;
        let dc = c.x * d.x + c.y * d.y + c.z * d.z;
        g01 += dw.y * da + weight * a.y;
        g02 += dw.z * da + weight * a.z;
        g10 += dw.x * db + weight * b.x;
        g12 += dw.z * db + weight * b.z;
        g20 += dw.x * dc + weight * c.x;
        g21 += dw.y * dc + weight * c.y;
    }
    return vec3<f32>(g21 - g12, g02 - g20, g10 - g01);
}

// A turbulence's curl at x, seconds after the plan's start, in its own
// space: x mapped back, scrolled, added to each octave's split coordinate
// times its frequency, the cell carried with 32-bit wrapping.
fn turbulence(n: u32, x: vec3<f32>, seconds: f32) -> vec3<f32> {
    var field = fieldOf(n);
    let q = field.rotation;
    let y = rotate(vec4<f32>(-q.xyz, q.w), x) / field.scales.x + field.scroll.xyz * seconds;
    var curl = vec3<f32>(0.0);
    var weight = 1.0;
    for (var o = 0u; o < field.counts.x; o++) {
        let s = field.fraction[o].xyz + y * field.frequency[o / 4u][o % 4u];
        let whole = floor(s);
        let cell = field.cell[o].xyz + bitcast<vec3<u32>>(vec3<i32>(whole));
        curl += weight * curlOctave(cell, s - whole, field.counts.y + o * 0x9E3779B9u);
        weight *= field.scales.y;
    }
    return curl;
}

const BORDER_NONE: u32 = 0u;
const BORDER_REPEAT: u32 = 2u;

// Along an axis of n points: the two points a place lies between, and its
// share toward the second, as the core locates them (the repeat border's
// product in binary32: the core's binary64 one differs by less than the
// paths' places do).
fn locate(u: f32, n: u32, border: u32, low: ptr<function, u32>, high: ptr<function, u32>) -> f32 {
    if (border == BORDER_REPEAT) {
        let finite = u == u && abs(u) <= 3.40282347e38;
        let g = select(0.0, u, finite) * f32(n);
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

// A vector field's vector at x in its own space: the grid sampled in the
// box's frame, turned out with it; false outside under the none border.
fn vectorField(n: u32, x: vec3<f32>, out: ptr<function, vec3<f32>>) -> bool {
    var field = fieldOf(n);
    let q = field.rotation;
    let d = rotate(vec4<f32>(-q.xyz, q.w), x) / field.scales.x + field.box.xyz;
    let r = field.boxRotation;
    let u = rotate(vec4<f32>(-r.xyz, r.w), d) / field.boxSize.xyz + 0.5;
    var sample = vec3<f32>(0.0);
    if (!sampleGrid(field.grid, field.gridModes.x, 3u, u, field.gridModes.y, &sample)) {
        return false;
    }
    *out = rotate(r, sample);
    return true;
}

// A gas's faces across axis at place p (in cells), from word start of the
// grids, as the core's mvfxGasSample: trilinear, p held within the
// array's points (a 2D array's four terms alone, which are the trilinear
// sum's bit for bit).
fn gasSample(start: u32, n: vec3<u32>, axis: u32, p: vec3<f32>) -> f32 {
    var m = n;
    m[axis] += 1u;
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
        sum += wx[0] * wy[0] * bitcast<f32>(grids[lower + x[0]]);
        sum += wx[1] * wy[0] * bitcast<f32>(grids[lower + x[1]]);
        sum += wx[0] * wy[1] * bitcast<f32>(grids[upper + x[0]]);
        sum += wx[1] * wy[1] * bitcast<f32>(grids[upper + x[1]]);
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
        sum += weight * bitcast<f32>(grids[start + (z[d] * m.y + y[b]) * m.x + x[a]]);
    }
    return sum;
}

// A gas's velocity at x in its own space (the core's SampleGas): x in
// cells of the box, the faces copied to the grids at grid.x, each axis's
// array in turn; false outside the box (a 2D gas's Z reaches without
// end).
fn gasField(n: u32, x: vec3<f32>, out: ptr<function, vec3<f32>>) -> bool {
    var field = fieldOf(n);
    let q = field.rotation;
    let place = rotate(vec4<f32>(-q.xyz, q.w), x) / field.scales.x + field.box.xyz;
    let size = field.grid.yzw;
    var p: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        p[k] = place[k] / field.boxSize.x + 0.5 * f32(size[k]);
        if (size[k] > 1u && !(p[k] >= 0.0 && p[k] <= f32(size[k]))) {
            return false;
        }
    }
    let cells = size.x * size.y * size.z;
    var start = field.grid.x;
    var value = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        if (size[k] > 1u) {
            value[k] = gasSample(start, size, k, p);
        }
        start += cells + cells / size[k];
    }
    *out = value;
    return true;
}

// A field's vector at x added to a, or, as a velocity, its pull and its
// drag.
fn fieldAt(n: u32, f: Force, x: vec3<f32>, seconds: f32, weight: f32,
           a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) {
    var field = fieldOf(n);
    var value = vec3<f32>(0.0);
    if (f.modes.x == FORCE_TURBULENCE) {
        value = turbulence(n, x, seconds);
    } else if (f.modes.x == FORCE_GAS) {
        if (!gasField(n, x, &value)) {
            return;
        }
    } else if (!vectorField(n, x, &value)) {
        return;
    }
    let mapped = rotate(field.rotation, value * (field.scales.x * f.vector.w));
    var scale = weight;
    if (f.modes.z == FIELD_VELOCITY) {
        *drag += weight * f.extra.w;
        scale *= f.extra.w;
    }
    *a += scale * mapped;
}

// Force n's acceleration at x of a weight added to a, and the drag it
// adds.
fn forceAt(n: u32, f: Force, x: vec3<f32>, seconds: f32, weight: f32,
           a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) {
    switch f.modes.x {
        case FORCE_CONSTANT: {
            *a += weight * f.vector.w * f.vector.xyz;
        }
        case FORCE_WIND: {
            *drag += weight * f.vector.w;
            *a += weight * f.vector.w * f.vector.xyz;
        }
        case FORCE_ATTRACTOR: {
            *a += attract(f, x, weight);
        }
        case FORCE_VORTEX: {
            *a += swirl(f, x, weight);
        }
        default: {
            fieldAt(n, f, x, seconds, weight, a, drag);
        }
    }
}

// Whether a force drags, its drag above 0 as the core's mvfxForceDrag: a
// wind of a strength above 0, a field pulling toward its velocity at a
// tightness above 0.
fn forceDrags(f: Force) -> bool {
    if (f.modes.x == FORCE_WIND) {
        return f.vector.w > 0.0;
    }
    let field = f.modes.x != FORCE_CONSTANT && f.modes.x != FORCE_ATTRACTOR && f.modes.x != FORCE_VORTEX;
    return field && f.modes.z == FIELD_VELOCITY && f.extra.w > 0.0;
}
