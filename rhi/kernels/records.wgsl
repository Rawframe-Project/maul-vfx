// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's records: each live particle's values in a layout of the
// host's, written into a buffer of the host's as the CPU path's
// mvfxInstance_WriteParticles writes them, one invocation a record, in the
// pool's order, its reverse, or sorted (keys here, sort.wgsl's passes).

const NONE: u32 = 0xFFFFFFFFu;
const CURVE_SAMPLES: u32 = 256u;

const ORDER_REVERSED: u32 = 1u;
const ORDER_SORTED: u32 = 2u;
const MEASURE_DEPTH: u32 = 1u;

// A sort's block, as sort.wgsl's: CHUNK invocations of ITEMS elements.
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;

// include: shared/exact.wgsl

const FIELD_POSITION: u32 = 0u;
const FIELD_SIZE: u32 = 1u;
const FIELD_COLOR: u32 = 2u;
const FIELD_ANGLE: u32 = 3u;
const FIELD_ORIENTATION: u32 = 4u;
const FIELD_VELOCITY: u32 = 5u;
const FIELD_AGE: u32 = 6u;
const FIELD_FRAME: u32 = 7u;
const FIELD_CUSTOM: u32 = 8u;
const FIELD_SERIAL: u32 = 9u;
const FIELD_MESH: u32 = 10u;

const FORMAT_FLOAT32: u32 = 0u;
const FORMAT_FLOAT16: u32 = 1u;
const FORMAT_UNORM8: u32 = 2u;
const FORMAT_SNORM8: u32 = 3u;
const FORMAT_UNORM16: u32 = 4u;
const FORMAT_SNORM16: u32 = 5u;

const INPUT_SPEED: u32 = 1u;
const FRAME_OVER_LIFE: u32 = 1u;
const FRAME_AT_RATE: u32 = 2u;

// The values over life (lives): alpha, the three sizes', the custom
// attributes'.
const LIFE_ALPHA: u32 = 0u;
const LIFE_SIZE: u32 = 1u;
const LIFE_CUSTOM: u32 = 4u;

// Where a particle's values are among its words (NONE for one the emitter
// does not store).
struct Words {
    place: u32,
    velocity: u32,
    serial: u32,
    birth: u32,
    inverseLife: u32,
    size: u32,
    color: u32,
    angle: u32,
    orientation: u32,
    frame: u32,
    mesh: u32,
    unused: u32,
    custom: array<vec4<u32>, 2>,
}

struct Records {
    // The particle buffer's capacity, the records the host's buffer holds,
    // the record buffer's first word, a record's words.
    head: vec4<u32>,
    // The layout's field count, the workgroups dispatched, the count's
    // word in the count buffer (NONE for none), unused.
    shape: vec4<u32>,
    // The clock at the step's end (two words); unused.
    clock: vec4<u32>,
    words: Words,
    // The map into the view's space: the instance's turn (identity in the
    // world's space); the offset from the view's origin and the scale the
    // stored values are divided by; the speed's scale for values over life
    // at the speed, unused.
    turn: vec4<f32>,
    offset: vec4<f32>,
    scales: vec4<f32>,
    // Colour over life: whether, its input, the gradient's first word
    // (NONE for white), whether alpha scales; the speed range, unused.
    color: vec4<u32>,
    colorRange: vec4<f32>,
    // Size over life: whether, its input, whether an axis each, unused;
    // the speed range, unused.
    size: vec4<u32>,
    sizeRange: vec4<f32>,
    // The custom attributes: each one's components (four bits each), the
    // scaled ones (a bit each), unused; the flipbook's animation, its
    // frames, whether it loops, its cycles or rate (binary32 bits).
    custom: vec4<u32>,
    frame: vec4<u32>,
    // The values over life, two words each: the value (binary32 bits) and
    // the curve's first word (NONE for none).
    lives: array<vec4<u32>, 6>,
    // The least values at which an sRGB byte steps up, MVFX_SRGB_STEPS.
    srgb: array<vec4<f32>, 64>,
    // The fields: kind, format, components | custom << 8 | sRGB << 16,
    // byte offset.
    fields: array<vec4<u32>, 32>,
    // The order: the pool's, reversed or sorted by place; what a sort
    // measures (the distance squared or the depth); the keys' flip (all
    // ones back to front); the sort's workgroups.
    order: vec4<u32>,
    // The eye's offset from the view's origin; the view's forward
    // direction.
    eye: vec4<f32>,
    forward: vec4<f32>,
}

@group(0) @binding(0) var<uniform> params: Records;
@group(0) @binding(1) var<storage, read> particles: array<u32>;
@group(0) @binding(2) var<storage, read> statistics: array<u32>;
@group(0) @binding(3) var<storage, read> grids: array<u32>;
@group(0) @binding(4) var<storage, read_write> recordWords: array<u32>;
@group(0) @binding(5) var<storage, read_write> count: array<u32>;
// The emitter's sort (an emitter that sorts; a word of the simulation's
// otherwise): keys, then indices, twice over its blocks; and two sets of
// digit counts, 256 a block each, digit-major.
@group(0) @binding(6) var<storage, read_write> sortWords: array<u32>;
@group(0) @binding(7) var<storage, read_write> digits: array<u32>;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> liveCount: u32;

fn wordAt(first: u32, i: u32) -> u32 {
    return particles[first * params.head.x + i];
}

fn floatAt(first: u32, i: u32) -> f32 {
    return bitcast<f32>(wordAt(first, i));
}

fn vectorAt(first: u32, i: u32) -> vec3<f32> {
    return vec3<f32>(floatAt(first, i), floatAt(first + 1u, i), floatAt(first + 2u, i));
}

// A baked table's binary32 at a word of the grids buffer.
fn tableAt(at: u32) -> f32 {
    return bitcast<f32>(grids[at]);
}

// A baked curve's value at a time, as the core blends it.
fn curveAt(first: u32, time: f32) -> f32 {
    let last = f32(CURVE_SAMPLES - 1u);
    var position = (time - tableAt(first + CURVE_SAMPLES)) * tableAt(first + CURVE_SAMPLES + 1u);
    position = select(position, 0.0, position < 0.0);
    position = select(position, last, position > last);
    let index = min(u32(position), CURVE_SAMPLES - 2u);
    let fraction = position - f32(index);
    let low = tableAt(first + index);
    return low + (tableAt(first + index + 1u) - low) * fraction;
}

// A baked gradient's linear RGBA at t in [0, 1].
fn gradientAt(first: u32, t: f32) -> vec4<f32> {
    let last = f32(CURVE_SAMPLES - 1u);
    var position = t * last;
    position = select(position, 0.0, position < 0.0);
    position = select(position, last, position > last);
    let index = min(u32(position), CURVE_SAMPLES - 2u);
    let fraction = position - f32(index);
    var rgba = vec4<f32>(0.0);
    for (var c = 0u; c < 4u; c++) {
        let low = tableAt(first + 4u * index + c);
        rgba[c] = low + (tableAt(first + 4u * (index + 1u) + c) - low) * fraction;
    }
    return rgba;
}

// A value over life at an input, as the core's mvfxLifeAt.
fn lifeAt(k: u32, input: f32) -> f32 {
    let life = params.lives[k / 2u];
    let value = bitcast<f32>(life[2u * (k % 2u)]);
    let first = life[2u * (k % 2u) + 1u];
    if (first == NONE) {
        return value;
    }
    return value * curveAt(first, input);
}

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * vec3<f32>(q.y * v.z - q.z * v.y, q.z * v.x - q.x * v.z, q.x * v.y - q.y * v.x);
    return v + q.w * t + vec3<f32>(q.y * t.z - q.z * t.y, q.z * t.x - q.x * t.z,
                                   q.x * t.y - q.y * t.x);
}

// The quaternion product a b: b's turn, then a's, as the core's.
fn multiply(a: vec4<f32>, b: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                     a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                     a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

// The 64-bit product of two words, as two.
fn mulWide(x: u32, y: u32) -> vec2<u32> {
    let xl = x & 0xFFFFu;
    let xh = x >> 16u;
    let yl = y & 0xFFFFu;
    let yh = y >> 16u;
    let ll = xl * yl;
    let lh = xl * yh;
    let hl = xh * yl;
    let mid = (ll >> 16u) + (lh & 0xFFFFu) + (hl & 0xFFFFu);
    return vec2<u32>((ll & 0xFFFFu) | (mid << 16u),
                     xh * yh + (lh >> 16u) + (hl >> 16u) + (mid >> 16u));
}

// The age at the clock in ticks (two words) of a particle born at a
// tick (two words).
fn ticksSince(birth: vec2<u32>) -> vec2<u32> {
    return subtract64(params.clock.xy, birth);
}

fn ticksOf(i: u32) -> vec2<u32> {
    let w = params.words;
    return ticksSince(vec2<u32>(wordAt(w.birth, i), wordAt(w.birth + 1u, i)));
}

// A normalized age at the clock, as the core's: the seconds since birth
// rounded to binary32 times the inverse life, held to [0, 1].
fn ageAt(birth: vec2<u32>, inverseLife: f32) -> f32 {
    let ticks = ticksSince(birth);
    let age = secondsOf(ticks) * inverseLife;
    return select(min(age, 1.0), 0.0, !(age > 0.0));
}

fn ageOf(i: u32) -> f32 {
    let w = params.words;
    return ageAt(vec2<u32>(wordAt(w.birth, i), wordAt(w.birth + 1u, i)),
                 floatAt(w.inverseLife, i));
}

// Particle i's speed in the particles' space.
fn speedOf(i: u32) -> f32 {
    let v = vectorAt(params.words.velocity, i);
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// The input a value over life is taken at: the age, or the speed's share
// of a range, the speed in the emitter's units.
fn inputAt(speed: f32, age: f32, input: u32, range: vec4<f32>) -> f32 {
    if (input != INPUT_SPEED) {
        return age;
    }
    var units = speed;
    if (params.scales.y != 1.0) {
        units /= params.scales.y;
    }
    let share = (units - range.x) / (range.y - range.x);
    return select(min(share, 1.0), 0.0, !(share > 0.0));
}

// A stored vector of n components, or ones where the emitter stores none.
fn storedOf(first: u32, i: u32, n: u32) -> vec4<f32> {
    var out = vec4<f32>(1.0);
    for (var k = 0u; first != NONE && k < n; k++) {
        out[k] = floatAt(first + k, i);
    }
    return out;
}

fn colorOf(i: u32, age: f32) -> vec4<f32> {
    return colorAt(storedOf(params.words.color, i, 4u), speedOf(i), age);
}

// A stored colour (ones for none) with the values over life at a speed
// and an age.
fn colorAt(stored: vec4<f32>, speed: f32, age: f32) -> vec4<f32> {
    var out = stored;
    let c = params.color;
    if (c.x == 0u) {
        return out;
    }
    if (c.z != NONE) {
        out *= gradientAt(c.z, inputAt(speed, age, c.y, params.colorRange));
    }
    if (c.w != 0u) {
        out.w *= lifeAt(LIFE_ALPHA, age);
    }
    return out;
}

fn sizeOf(i: u32, age: f32) -> vec3<f32> {
    return sizeAt(storedOf(params.words.size, i, 3u).xyz, speedOf(i), age);
}

// A stored size (ones for none) with the values over life, as a colour.
fn sizeAt(stored: vec3<f32>, speed: f32, age: f32) -> vec3<f32> {
    var out = stored;
    let s = params.size;
    if (s.x != 0u) {
        let t = inputAt(speed, age, s.y, params.sizeRange);
        let scale = lifeAt(LIFE_SIZE, t);
        for (var k = 0u; k < 3u; k++) {
            out[k] *= select(scale, lifeAt(LIFE_SIZE + k, t), s.z != 0u);
        }
    }
    return out;
}

fn customOf(i: u32, age: f32, c: u32) -> vec4<f32> {
    let n = (params.custom.x >> (4u * c)) & 0xFu;
    var scale = 1.0;
    if (((params.custom.y >> c) & 1u) != 0u) {
        scale = lifeAt(LIFE_CUSTOM + c, age);
    }
    let first = params.words.custom[c / 4u][c % 4u];
    var out = vec4<f32>(0.0);
    for (var k = 0u; first != NONE && k < n; k++) {
        out[k] = scale * floatAt(first + k, i);
    }
    return out;
}

// A binary32 above or at 0 as its integer mantissa and its exponent.
fn partsOf(x: f32) -> vec2<i32> {
    let bits = bitcast<u32>(x);
    let biased = (bits >> 23u) & 0xFFu;
    if (biased == 0u) {
        return vec2<i32>(i32(bits & 0x007FFFFFu), -149);
    }
    return vec2<i32>(i32((bits & 0x007FFFFFu) | 0x00800000u), i32(biased) - 150);
}

// a times b, a 64-bit number (two words) times a word, as three words.
fn productOf(a: vec2<u32>, b: u32) -> vec3<u32> {
    let p0 = mulWide(a.x, b);
    let p1 = mulWide(a.y, b);
    let middle = p0.y + p1.x;
    return vec3<u32>(p0.x, middle, p1.y + select(0u, 1u, middle < p0.y));
}

// The floor of a three-word number over 2^shift, held to 64 bits.
fn shiftDown(x: vec3<u32>, shift: u32) -> vec2<u32> {
    if (shift >= 96u) {
        return vec2<u32>(0u);
    }
    var words = x;
    var s = shift;
    while (s >= 32u) {
        words = vec3<u32>(words.y, words.z, 0u);
        s -= 32u;
    }
    if (s == 0u) {
        return words.xy;
    }
    return vec2<u32>((words.x >> s) | (words.y << (32u - s)), (words.y >> s) | (words.z << (32u - s)));
}

// A 64-bit number modulo a count below 2^17.
fn modulo(x: vec2<u32>, m: u32) -> u32 {
    let base = (0xFFFFFFFFu % m + 1u) % m;
    return ((x.y % m) * base % m + x.x % m) % m;
}

// Particle i's frame, as the core's mvfxFrameOf: its start frame plus the
// frames moved, the floor of an exact product, wrapped or held.
fn frameOf(i: u32, age: f32) -> u32 {
    var start = 0u;
    if (params.words.frame != NONE) {
        start = wordAt(params.words.frame, i);
    }
    let f = params.frame;
    var moved = vec2<u32>(0u);
    if (f.x == FRAME_OVER_LIFE) {
        let a = partsOf(age);
        let c = partsOf(bitcast<f32>(f.w));
        // The mantissas' product below 2^48, times the frames.
        let ab = productOf(vec2<u32>(u32(a.x), 0u), u32(c.x));
        let product = productOf(ab.xy, f.y);
        moved = shiftDown(product, u32(-(a.y + c.y)));
    } else if (f.x == FRAME_AT_RATE) {
        let r = partsOf(bitcast<f32>(f.w));
        moved = shiftDown(productOf(ticksOf(i), u32(r.x)), u32(20 - r.y));
    } else {
        return start;
    }
    let low = start + moved.x;
    let at = vec2<u32>(low, moved.y + select(0u, 1u, low < start));
    if (f.z != 0u) {
        return modulo(at, f.y);
    }
    return select(f.y - 1u, at.x, at.y == 0u && at.x < f.y);
}

// Particle i's place in the view's space, from the view's origin.
fn placeOf(i: u32) -> vec3<f32> {
    let p = rotate(params.turn, vectorAt(params.words.place, i));
    return p / params.scales.x + params.offset.xyz;
}

// A field's values of particle i: four floats, or words for the word
// fields.
fn valuesOf(field: vec4<u32>, i: u32, age: f32, words: ptr<function, vec4<u32>>) -> vec4<f32> {
    let w = params.words;
    var v = vec4<f32>(0.0);
    switch field.x {
        case FIELD_POSITION: {
            v = vec4<f32>(placeOf(i), 0.0);
        }
        case FIELD_SIZE: {
            v = vec4<f32>(sizeOf(i, age) / params.scales.x, 0.0);
        }
        case FIELD_COLOR: {
            v = colorOf(i, age);
        }
        case FIELD_ANGLE: {
            if (w.angle != NONE) {
                v.x = floatAt(w.angle, i);
            }
        }
        case FIELD_ORIENTATION: {
            var q = vec4<f32>(0.0, 0.0, 0.0, 1.0);
            if (w.orientation != NONE) {
                q = vec4<f32>(vectorAt(w.orientation, i), floatAt(w.orientation + 3u, i));
            }
            v = multiply(params.turn, q);
        }
        case FIELD_VELOCITY: {
            v = vec4<f32>(rotate(params.turn, vectorAt(w.velocity, i)) / params.scales.x, 0.0);
        }
        case FIELD_AGE: {
            v.x = age;
        }
        case FIELD_FRAME: {
            (*words).x = frameOf(i, age);
            v.x = f32((*words).x);
        }
        case FIELD_CUSTOM: {
            v = customOf(i, age, (field.z >> 8u) & 0xFFu);
        }
        case FIELD_MESH: {
            if (w.mesh != NONE) {
                (*words).x = wordAt(w.mesh, i);
            }
            v.x = f32((*words).x);
        }
        default: {
            (*words).x = wordAt(w.serial, i);
        }
    }
    return v;
}

// A binary32's half float, as the core's integer conversion.
fn halfOf(value: f32) -> u32 {
    let bits = bitcast<u32>(value);
    let sign = (bits >> 16u) & 0x8000u;
    let magnitude = bits & 0x7FFFFFFFu;
    if (magnitude >= 0x7F800000u) {
        return sign | select(0x7C00u, 0x7E00u, magnitude > 0x7F800000u);
    }
    if (magnitude >= 0x477FF000u) {
        return sign | 0x7C00u;
    }
    if (magnitude >= 0x38800000u) {
        let rebased = magnitude - 0x38000000u;
        let odd = (rebased >> 13u) & 1u;
        return sign | ((rebased + 0x0FFFu + odd) >> 13u);
    }
    if (magnitude < 0x33000000u) {
        return sign;
    }
    let exponent = magnitude >> 23u;
    let mantissa = (magnitude & 0x007FFFFFu) | 0x00800000u;
    let shift = 126u - exponent;
    let kept = mantissa >> shift;
    let rest = mantissa & ((1u << shift) - 1u);
    let half = 1u << (shift - 1u);
    let up = rest > half || (rest == half && (kept & 1u) == 1u);
    return sign | (kept + select(0u, 1u, up));
}

// A value clamped to 0 to 1 (NaN to 0) in steps of 1 / top, rounded half
// up, as the core's.
fn unormOf(value: f32, top: f32) -> u32 {
    let x = select(0.0, min(value, 1.0), value >= 0.0);
    return u32(x * top + 0.5);
}

// A value clamped to -1 to 1 (NaN to 0) in steps of 1 / top, rounded half
// away from 0, as a two's complement word.
fn snormOf(value: f32, top: f32) -> u32 {
    var x = 0.0;
    if (value >= -1.0) {
        x = min(value, 1.0);
    } else if (value < -1.0) {
        x = -1.0;
    }
    let scaled = x * top;
    let magnitude = i32(abs(scaled) + 0.5);
    return bitcast<u32>(select(magnitude, -magnitude, scaled < 0.0));
}

// A linear channel's sRGB byte: the steps it is not below.
fn srgbOf(value: f32) -> u32 {
    var low = 0u;
    var high = 255u;
    while (low < high) {
        let middle = (low + high) / 2u;
        if (value >= params.srgb[middle / 4u][middle % 4u]) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

fn formatBytes(format: u32) -> u32 {
    switch format {
        case FORMAT_FLOAT16, FORMAT_UNORM16, FORMAT_SNORM16: {
            return 2u;
        }
        case FORMAT_UNORM8, FORMAT_SNORM8: {
            return 1u;
        }
        default: {
            return 4u;
        }
    }
}

// A value's bits in a format (a word for uint32).
fn bitsOf(format: u32, srgb: bool, value: f32, word: u32) -> u32 {
    switch format {
        case FORMAT_FLOAT32: {
            return bitcast<u32>(value);
        }
        case FORMAT_FLOAT16: {
            return halfOf(value);
        }
        case FORMAT_UNORM8: {
            if (srgb) {
                return srgbOf(value);
            }
            return unormOf(value, 255.0);
        }
        case FORMAT_SNORM8: {
            return snormOf(value, 127.0) & 0xFFu;
        }
        case FORMAT_UNORM16: {
            return unormOf(value, 65535.0);
        }
        case FORMAT_SNORM16: {
            return snormOf(value, 32767.0) & 0xFFFFu;
        }
        default: {
            return word;
        }
    }
}

// Writes a value's bytes at a byte of the record, the record's other
// bytes kept.
fn writeBytes(record: u32, byte: u32, bytes: u32, bits: u32) {
    let at = record + byte / 4u;
    if (bytes == 4u) {
        recordWords[at] = bits;
        return;
    }
    let shift = (byte % 4u) * 8u;
    let mask = select(0xFFu, 0xFFFFu, bytes == 2u) << shift;
    recordWords[at] = (recordWords[at] & ~mask) | ((bits << shift) & mask);
}

// Record r: particle i's values.
fn writeRecord(r: u32, i: u32) {
    let age = ageOf(i);
    let record = params.head.z + r * params.head.w;
    for (var f = 0u; f < params.shape.x; f++) {
        let field = params.fields[f];
        var words = vec4<u32>(0u);
        let values = valuesOf(field, i, age, &words);
        let bytes = formatBytes(field.y);
        let srgb = ((field.z >> 16u) & 1u) != 0u;
        for (var k = 0u; k < (field.z & 0xFFu); k++) {
            let bits = bitsOf(field.y, srgb && k < 3u, values[k], words[k]);
            writeBytes(record, field.w + k * bytes, bytes, bits);
        }
    }
}

// The words of one of the sort's arrays: its blocks' elements.
fn sortSpan() -> u32 {
    return (params.head.x + BLOCK - 1u) / BLOCK * BLOCK;
}

// The particle record r shows, of live: in the pool's order, its reverse,
// or the sort's (its indices in the first set after four passes).
fn particleAt(r: u32, live: u32) -> u32 {
    switch params.order.x {
        case ORDER_REVERSED: {
            return live - 1u - r;
        }
        case ORDER_SORTED: {
            return sortWords[sortSpan() + r];
        }
        default: {
            return r;
        }
    }
}

// Records: the live particles' up to the capacity, the workgroups (at
// most a dispatch's along one axis) striding over them; the count by the
// first invocation. The workgroups come from the numbers, as the step's:
// num_workgroups would take a constant buffer of its own on D3D12.
@compute @workgroup_size(64, 1, 1)
fn records(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    let n = min(live, params.head.y);
    if (id.x == 0u && params.shape.z != NONE) {
        count[params.shape.z] = n;
    }
    for (var r = id.x; r < n; r += params.shape.y * 64u) {
        writeRecord(r, particleAt(r, live));
    }
}

// A binary32's key, ordered as the values are, as the core's
// mvfxOrderedKey: a negative value's bits flipped, a positive one's sign
// set; -0 as +0.
fn orderedKey(value: f32) -> u32 {
    var bits = bitcast<u32>(value);
    bits = select(bits, 0u, bits == 0x80000000u);
    return bits ^ select(0x80000000u, 0xFFFFFFFFu, (bits >> 31u) != 0u);
}

// Particle i's sort key: its distance squared or depth from the eye in
// binary32, as the core's binary64 measure rounded, flipped back to front.
fn keyOf(i: u32) -> u32 {
    let d = placeOf(i) - params.eye.xyz;
    var measure = d.x * d.x + d.y * d.y + d.z * d.z;
    if (params.order.y == MEASURE_DEPTH) {
        let f = params.forward;
        measure = d.x * f.x + d.y * f.y + d.z * f.z;
    }
    return orderedKey(measure) ^ params.order.z;
}

// Keys: each live particle's key and index in the first set, a block a
// workgroup, and each block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn keys(@builtin(local_invocation_index) local: u32,
        @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    let n = (params.head.x + BLOCK - 1u) / BLOCK;
    let span = n * BLOCK;
    let blocks = (live + BLOCK - 1u) / BLOCK;
    for (var b = group.x; b < blocks; b += params.order.w) {
        // An exchange, not a store: naga's HLSL of an atomic store names a
        // result it never declares.
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < live) {
                let key = keyOf(i);
                sortWords[i] = key;
                sortWords[span + i] = i;
                atomicAdd(&tally[key & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        digits[local * n + b] = atomicLoad(&tally[local]);
        workgroupBarrier();
    }
}

// An event's words in an emitter's events buffer, as sim.wgsl's.
const EVENT_WORDS: u32 = 28u;

// Event values: each of the step's events (statistics word 28) given its
// colour and size with the values over life, at the age and speed it
// keeps in its last words; the events from word head.z of the events
// buffer bound for the records.
@compute @workgroup_size(64, 1, 1)
fn eventValues(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = statistics[28];
    for (var e = id.x; e < n; e += params.shape.y * 64u) {
        let w = params.head.z + EVENT_WORDS * e;
        let age = ageAt(vec2<u32>(recordWords[w + 20u], recordWords[w + 21u]),
                        bitcast<f32>(recordWords[w + 22u]));
        let speed = bitcast<f32>(recordWords[w + 23u]);
        var color = vec4<f32>(0.0);
        var size = vec3<f32>(0.0);
        for (var k = 0u; k < 4u; k++) {
            color[k] = bitcast<f32>(recordWords[w + 13u + k]);
        }
        for (var k = 0u; k < 3u; k++) {
            size[k] = bitcast<f32>(recordWords[w + 17u + k]);
        }
        color = colorAt(color, speed, age);
        size = sizeAt(size, speed, age);
        for (var k = 0u; k < 4u; k++) {
            recordWords[w + 13u + k] = bitcast<u32>(color[k]);
        }
        for (var k = 0u; k < 3u; k++) {
            recordWords[w + 17u + k] = bitcast<u32>(size[k]);
        }
    }
}
