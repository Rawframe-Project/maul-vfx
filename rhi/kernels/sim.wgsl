// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's step on the GPU: update, room, then the survivors and the
// births kept in order by a scan in three passes (reduce, offsets,
// scatter), and their box measured. Particles are arrays of the capacity
// in one buffer of words (WORDS a particle): place (3), velocity (3),
// serial (2), birth tick (2), death tick (2), inverse life, collisions,
// and whether it outlives the step.

const WORDS: u32 = 15u;
const PLACE: u32 = 0u;
const VELOCITY: u32 = 3u;
const SERIAL: u32 = 6u;
const BIRTH: u32 = 8u;
const DEATH: u32 = 10u;
const INVERSE_LIFE: u32 = 12u;
const COLLISIONS: u32 = 13u;
const ALIVE: u32 = 14u;
const CHUNK: u32 = 256u;
// The elements an invocation of the scan's reduce and scatter takes, in a
// row; a workgroup's block is CHUNK times as many.
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;
const TICKS_PER_SECOND: f32 = 1048576.0;
const TWO_PI: f32 = 6.28318530717958647692;

const SHAPE_SALT: u32 = 0x53484150u;
const VELOCITY_SALT: u32 = 0x56454C4Fu;

// A force as resolved for the step, in the particles' space.
struct Force {
    // its kind, its falloff's kind, its field mode, unused
    modes: vec4<u32>,
    // its vector (the constant's acceleration, the wind's velocity, the
    // vortex's axis) and strength
    vector: vec4<f32>,
    // its point (the attractor's, the vortex's) and softening
    point: vec4<f32>,
    // its falloff's center and radius, axis and cone cosine
    center: vec4<f32>,
    axis: vec4<f32>,
    // its kill radius, pull, falloff exponent and tightness
    extra: vec4<f32>,
}

// A force's field, as resolved for the plan: its map's turn into the
// particles' space; its octaves and seed; its map's scale and the octaves'
// gain; its scroll a second in its own space; each octave's frequency and
// the lattice coordinate of the particles' origin at the plan's start, a
// cell and the fraction past it.
struct Field {
    rotation: vec4<f32>,
    counts: vec4<u32>,
    scales: vec4<f32>,
    scroll: vec4<f32>,
    frequency: array<vec4<f32>, 2>,
    cell: array<vec4<u32>, 8>,
    fraction: array<vec4<f32>, 8>,
    // a vector field's box: the particles' origin mapped back less its
    // center; its size; its turn; its grid's first word in the grids and
    // size; the grid's format (1 for binary16) and the border
    box: vec4<f32>,
    boxSize: vec4<f32>,
    boxRotation: vec4<f32>,
    grid: vec4<u32>,
    gridModes: vec4<u32>,
}

// A collider as resolved for the step, in the particles' space: its
// kind and whether it keeps them inside; its center and radius; a plane's
// normal; a box's half extents and turn; a grid collider's grid (first
// word, size), its format, and the scale of its values and its grow.
struct Collider {
    modes: vec4<u32>,
    center: vec4<f32>,
    normal: vec4<f32>,
    halfExtents: vec4<f32>,
    rotation: vec4<f32>,
    grid: vec4<u32>,
    gridModes: vec4<u32>,
    scales: vec4<f32>,
}

struct Step {
    // capacity, births, the plan's ticks, the instance's seed
    counts: vec4<u32>,
    // the plan's start (two words), the emitter's index, its space (0 for
    // the world)
    start: vec4<u32>,
    // the velocity's mode, the shape's kind, update's workgroups, unused
    modes: vec4<u32>,
    // the anchors' shift, rounded to binary32; the share of the instance's
    // velocity inherited
    shift: vec4<f32>,
    // the step's drag factors over the plan: decay, carry, push; the drag
    factors: vec4<f32>,
    // gravity in the particles' space; unused
    gravity: vec4<f32>,
    // the shape's matrix's rows, its offset and radius, its thickness
    shapeRow0: vec4<f32>,
    shapeRow1: vec4<f32>,
    shapeRow2: vec4<f32>,
    shapeOffset: vec4<f32>,
    shapeExtra: vec4<f32>,
    // the velocity's direction and cone cosine, tangents with randomize
    // and the least speed, the greatest speed
    direction: vec4<f32>,
    tangentA: vec4<f32>,
    tangentB: vec4<f32>,
    speed: vec4<f32>,
    // the instance at the plan's start and end: origin from the anchor
    // after, scale; rotations; its velocity
    fromOrigin: vec4<f32>,
    fromRotation: vec4<f32>,
    toOrigin: vec4<f32>,
    toRotation: vec4<f32>,
    instanceVelocity: vec4<f32>,
    // the forces' count, whether any kills, the drag every particle has
    // (binary32 bits), unused
    forceHead: vec4<u32>,
    forces: array<Force, 8>,
    fields: array<Field, 8>,
    // the colliders' count, whether a contact kills, unused (2); the
    // response: restitution, friction, life loss, rest speed
    collisionHead: vec4<u32>,
    response: vec4<f32>,
    colliders: array<Collider, 16>,
}

@group(0) @binding(0) var<uniform> params: Step;
@group(0) @binding(1) var<storage, read> births: array<vec4<u32>>;
@group(0) @binding(2) var<storage, read_write> source: array<u32>;
@group(0) @binding(3) var<storage, read_write> moved: array<u32>;
// The statistics record (rhi/src/record.h): counts, the growth, the box.
@group(0) @binding(4) var<storage, read_write> counts: array<u32>;
// Whether each of the plan's births has room, as the room entry decides.
@group(0) @binding(5) var<storage, read_write> accepted: array<u32>;
// The grids the forces and colliders read, binary32 values one a word,
// binary16 two.
@group(0) @binding(7) var<storage, read> grids: array<u32>;
// Each block of the sequence's count of kept elements, then its first
// place; after them (blockCount() words on), each block's box of the
// places scatter wrote: min then max, three binary32 words each.
@group(0) @binding(6) var<storage, read_write> blocks: array<u32>;

var<workgroup> scan: array<u32, CHUNK>;
var<workgroup> carried: u32;
var<workgroup> liveCount: u32;
var<workgroup> dead: u32;
var<workgroup> batchStart: u32;
var<workgroup> batchLength: u32;
var<workgroup> taken: u32;
var<workgroup> boxLow: array<vec4<f32>, CHUNK>;
var<workgroup> boxHigh: array<vec4<f32>, CHUNK>;

fn pcg4dMix(v: vec4<u32>) -> vec4<u32> {
    var r = v;
    r.x += r.y * r.w;
    r.y += r.z * r.x;
    r.z += r.x * r.y;
    r.w += r.y * r.z;
    return r;
}

fn pcg4d(start: vec4<u32>) -> vec4<u32> {
    var v = start * 1664525u + 1013904223u;
    v = pcg4dMix(v);
    v ^= v >> vec4<u32>(16u);
    return pcg4dMix(v);
}

// The words of a purpose at a serial, as the core draws them.
fn draw(salt: u32, serialLow: u32, serialHigh: u32) -> vec4<u32> {
    return pcg4d(vec4<u32>(params.counts.w, (params.start.z * 0x9E3779B9u) ^ salt, serialLow,
                           serialHigh));
}

fn unitFloat(word: u32) -> f32 {
    return f32(word >> 8u) * (1.0 / 16777216.0);
}

// A uniform unit vector from two words.
fn randomDirection(a: u32, b: u32) -> vec3<f32> {
    let z = 1.0 - 2.0 * unitFloat(a);
    let ring = sqrt(max(0.0, 1.0 - z * z));
    let angle = TWO_PI * unitFloat(b);
    return vec3<f32>(ring * cos(angle), ring * sin(angle), z);
}

// A sphere's sample: the place in the emitter's space, and its normal.
fn sampleSphere(words: vec4<u32>, normal: ptr<function, vec3<f32>>) -> vec3<f32> {
    *normal = randomDirection(words.x, words.y);
    let outer = params.shapeOffset.w;
    let inner = outer * (1.0 - params.shapeExtra.x);
    let innerCube = inner * inner * inner;
    let outerCube = outer * outer * outer;
    var radius = pow(innerCube + unitFloat(words.z) * (outerCube - innerCube), 1.0 / 3.0);
    if (params.shapeExtra.x == 0.0) {
        radius = outer;
    }
    return radius * *normal;
}

fn turnByShape(v: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(dot(params.shapeRow0.xyz, v), dot(params.shapeRow1.xyz, v),
                     dot(params.shapeRow2.xyz, v));
}

// The initial velocity from the shape's normal, in the emitter's space:
// the cone and fixed modes, the normal's for the others.
fn drawVelocity(words: vec4<u32>, normal: vec3<f32>) -> vec3<f32> {
    var direction = normal;
    if (params.modes.x == 2u) {
        let z = 1.0 - unitFloat(words.x) * (1.0 - params.direction.w);
        let across = sqrt(max(0.0, 1.0 - z * z));
        let angle = TWO_PI * unitFloat(words.y);
        direction = across * cos(angle) * params.tangentA.xyz +
                    across * sin(angle) * params.tangentB.xyz + z * params.direction.xyz;
    } else if (params.modes.x == 3u) {
        direction = params.direction.xyz;
    }
    let speedWord = ((words.x & 0xFFu) << 24u) | ((words.y & 0xFFu) << 16u) |
                    ((words.z & 0xFFu) << 8u) | (words.w & 0xFFu);
    let speed = params.tangentB.w + (params.speed.x - params.tangentB.w) * unitFloat(speedWord);
    return direction * speed;
}

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Drag factors over seconds: decay, carry, push (the integrator's exact form).
fn factors(drag: f32, seconds: f32) -> vec3<f32> {
    let x = drag * seconds;
    if (x < 1e-4) {
        return vec3<f32>(1.0 - x, seconds * (1.0 - 0.5 * x), seconds * seconds * (0.5 - x / 6.0));
    }
    let decay = exp(-x);
    let phi1 = (1.0 - decay) / x;
    let phi2 = (1.0 - phi1) / x;
    return vec3<f32>(decay, seconds * phi1, seconds * seconds * phi2);
}

fn readVector(buffer: u32, i: u32, first: u32) -> vec3<f32> {
    let c = params.counts.x;
    if (buffer == 0u) {
        return vec3<f32>(bitcast<f32>(source[first * c + i]),
                         bitcast<f32>(source[(first + 1u) * c + i]),
                         bitcast<f32>(source[(first + 2u) * c + i]));
    }
    return vec3<f32>(bitcast<f32>(moved[first * c + i]), bitcast<f32>(moved[(first + 1u) * c + i]),
                     bitcast<f32>(moved[(first + 2u) * c + i]));
}

// Whether a 64-bit tick (two words) is past the plan's end.
fn outlives(low: u32, high: u32) -> bool {
    let endLow = params.start.x + params.counts.z;
    let endHigh = params.start.y + select(0u, 1u, endLow < params.start.x);
    return high > endHigh || (high == endHigh && low > endLow);
}

const FORCE_CONSTANT: u32 = 0u;
const FORCE_WIND: u32 = 1u;
const FORCE_ATTRACTOR: u32 = 2u;
const FORCE_VORTEX: u32 = 3u;
const FORCE_TURBULENCE: u32 = 4u;
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
    let field = &params.fields[n];
    let q = (*field).rotation;
    let y = rotate(vec4<f32>(-q.xyz, q.w), x) / (*field).scales.x + (*field).scroll.xyz * seconds;
    var curl = vec3<f32>(0.0);
    var weight = 1.0;
    for (var o = 0u; o < (*field).counts.x; o++) {
        let s = (*field).fraction[o].xyz + y * (*field).frequency[o / 4u][o % 4u];
        let whole = floor(s);
        let cell = (*field).cell[o].xyz + bitcast<vec3<u32>>(vec3<i32>(whole));
        curl += weight * curlOctave(cell, s - whole, (*field).counts.y + o * 0x9E3779B9u);
        weight *= (*field).scales.y;
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
    let field = &params.fields[n];
    let q = (*field).rotation;
    let d = rotate(vec4<f32>(-q.xyz, q.w), x) / (*field).scales.x + (*field).box.xyz;
    let r = (*field).boxRotation;
    let u = rotate(vec4<f32>(-r.xyz, r.w), d) / (*field).boxSize.xyz + 0.5;
    var sample = vec3<f32>(0.0);
    if (!sampleGrid((*field).grid, (*field).gridModes.x, 3u, u, (*field).gridModes.y, &sample)) {
        return false;
    }
    *out = rotate(r, sample);
    return true;
}

// A field's vector at x added to a, or, as a velocity, its pull and its
// drag.
fn fieldAt(n: u32, f: Force, x: vec3<f32>, seconds: f32, weight: f32,
           a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) {
    let field = &params.fields[n];
    var value = vec3<f32>(0.0);
    if (f.modes.x == FORCE_TURBULENCE) {
        value = turbulence(n, x, seconds);
    } else if (!vectorField(n, x, &value)) {
        return;
    }
    let mapped = rotate((*field).rotation, value * ((*field).scales.x * f.vector.w));
    var scale = weight;
    if (f.modes.z == FIELD_VELOCITY) {
        *drag += weight * f.extra.w;
        scale *= f.extra.w;
    }
    *a += scale * mapped;
}

// The forces at x, seconds after the plan's start: gravity and their
// accelerations in order, and the emitter's drag and what the winds and
// velocity fields add.
fn forcesAt(x: vec3<f32>, seconds: f32, a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) {
    *a = params.gravity.xyz;
    *drag = params.factors.w;
    for (var n = 0u; n < params.forceHead.x; n++) {
        let f = params.forces[n];
        let weight = falloffAt(f, x);
        if (!(weight > 0.0)) {
            continue;
        }
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
}

// Whether a step from start to end passes within an attractor's kill
// radius of its point.
fn killed(start: vec3<f32>, end: vec3<f32>) -> bool {
    if (params.forceHead.y == 0u) {
        return false;
    }
    for (var n = 0u; n < params.forceHead.x; n++) {
        let f = params.forces[n];
        let r = f.extra.x;
        if (f.modes.x != FORCE_ATTRACTOR || !(r > 0.0)) {
            continue;
        }
        let d = end - start;
        let e = start - f.point.xyz;
        let length = dot(d, d);
        let t = clamp(select(0.0, -dot(e, d) / length, length > 0.0), 0.0, 1.0);
        let closest = e + t * d;
        if (dot(closest, closest) <= r * r) {
            return true;
        }
    }
    return false;
}

// Drag factors over seconds for a particle's drag: the shared ones over
// the plan when its drag is the shared drag, else its own.
fn factorsFor(drag: f32, seconds: f32, whole: bool) -> vec3<f32> {
    if (whole && drag == bitcast<f32>(params.forceHead.z)) {
        return params.factors.xyz;
    }
    return factors(drag, seconds);
}

const COLLIDER_PLANE: u32 = 0u;
const COLLIDER_SPHERE: u32 = 1u;
const COLLIDER_BOX: u32 = 2u;
const COLLIDER_HEIGHTFIELD: u32 = 3u;
const COLLIDER_DISTANCE: u32 = 4u;
const MAX_CONTACTS: u32 = 4u;
const LARGEST: f32 = 3.40282347e38;

fn dot3(a: vec3<f32>, b: vec3<f32>) -> f32 {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// A contact: how far along the segment (0 for a start on the wrong side),
// where, and the surface's normal toward the allowed side.
struct Hit {
    t: f32,
    point: vec3<f32>,
    normal: vec3<f32>,
}

fn hitPlane(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let n = c.normal.xyz;
    let d0 = dot3(s - c.center.xyz, n);
    let d1 = dot3(e - c.center.xyz, n);
    (*hit).normal = n;
    if (d0 < 0.0) {
        (*hit).t = 0.0;
        (*hit).point = s - d0 * n;
        return true;
    }
    if (!(d1 < 0.0)) {
        return false;
    }
    (*hit).t = d0 / (d0 - d1);
    (*hit).point = s + (*hit).t * (e - s);
    return true;
}

// A sphere's surface point along a direction from its center (up when the
// direction is 0), the normal outward or inward.
fn spherePoint(c: Collider, toward: vec3<f32>, hit: ptr<function, Hit>) {
    var dir = toward - c.center.xyz;
    let length = sqrt(dot3(dir, dir));
    if (length > 0.0) {
        dir = dir / length;
    } else {
        dir = vec3<f32>(0.0, 0.0, 1.0);
    }
    (*hit).point = c.center.xyz + c.center.w * dir;
    (*hit).normal = select(1.0, -1.0, c.modes.y != 0u) * dir;
}

fn hitSphere(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let inside = c.modes.y != 0u;
    let f = s - c.center.xyz;
    let g = e - c.center.xyz;
    let d = e - s;
    let r2 = c.center.w * c.center.w;
    let ff = dot3(f, f);
    let outside = r2 < ff;
    if (select(ff < r2, outside, inside)) {
        (*hit).t = 0.0;
        spherePoint(c, s, hit);
        return true;
    }
    let crosses = !inside || dot3(g, g) > r2;
    let a = dot3(d, d);
    if (!crosses || !(a > 0.0)) {
        return false;
    }
    let b = dot3(f, d);
    let disc = b * b - a * (ff - r2);
    if (!(disc >= 0.0)) {
        return false;
    }
    let root = sqrt(disc);
    let t = select((-b - root) / a, (-b + root) / a, inside);
    if (!(t >= 0.0 && t <= 1.0)) {
        return false;
    }
    (*hit).t = t;
    spherePoint(c, s + t * d, hit);
    return true;
}

// The axis a start inside a box is nearest a face on, or outside it
// farthest past one.
fn pushAxis(h: vec3<f32>, a: vec3<f32>, inside: bool) -> u32 {
    var axis = 0u;
    var best = select(LARGEST, -LARGEST, inside);
    for (var k = 0u; k < 3u; k++) {
        let room = h[k] - abs(a[k]);
        let deeper = best < -room;
        if (select(room < best, deeper, inside)) {
            best = select(room, -room, inside);
            axis = k;
        }
    }
    return axis;
}

fn isInside(h: vec3<f32>, a: vec3<f32>) -> bool {
    return abs(a.x) <= h.x && abs(a.y) <= h.y && abs(a.z) <= h.z;
}

// A box's contact in its own frame, from local start a to local end b.
fn hitBoxLocal(h: vec3<f32>, inside: bool, a: vec3<f32>, b: vec3<f32>,
               hit: ptr<function, Hit>) -> bool {
    if (isInside(h, a) != inside) {
        // A start on the wrong side: put on the surface.
        (*hit).t = 0.0;
        let axis = pushAxis(h, a, inside);
        let sign = select(1.0, -1.0, a[axis] < 0.0);
        var point = select(a, min(max(a, -h), h), inside);
        point[axis] = sign * h[axis];
        var normal = vec3<f32>(0.0);
        normal[axis] = select(sign, -sign, inside);
        (*hit).point = point;
        (*hit).normal = normal;
        return true;
    }
    if (inside && isInside(h, b)) {
        return false;
    }
    var low = 0.0;
    var high = 1.0;
    var lowAxis = -1;
    var highAxis = -1;
    for (var k = 0; k < 3; k++) {
        let d = b[k] - a[k];
        if (d == 0.0) {
            if (abs(a[k]) > h[k]) {
                return false;
            }
            continue;
        }
        let t1 = (-h[k] - a[k]) / d;
        let t2 = (h[k] - a[k]) / d;
        let near = min(t1, t2);
        let far = max(t1, t2);
        lowAxis = select(lowAxis, k, near > low);
        low = max(low, near);
        highAxis = select(highAxis, k, far < high);
        high = min(high, far);
    }
    let axis = select(lowAxis, highAxis, inside);
    if (low > high || axis < 0) {
        return false;
    }
    (*hit).t = select(low, high, inside);
    (*hit).point = a + (*hit).t * (b - a);
    var normal = vec3<f32>(0.0);
    normal[axis] = select(1.0, -1.0, b[axis] - a[axis] > 0.0);
    (*hit).normal = normal;
    return true;
}

fn hitBox(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let q = c.rotation;
    let inverse = vec4<f32>(-q.xyz, q.w);
    let a = rotate(inverse, s - c.center.xyz);
    let b = rotate(inverse, e - c.center.xyz);
    var local: Hit;
    if (!hitBoxLocal(c.halfExtents.xyz, c.modes.y != 0u, a, b, &local)) {
        return false;
    }
    (*hit).t = local.t;
    (*hit).point = rotate(q, local.point) + c.center.xyz;
    (*hit).normal = rotate(q, local.normal);
    return true;
}

// The most samples a grid collider takes along a segment, and the
// halvings that find a crossing between two.
const FIELD_SAMPLES: u32 = 256u;
const FIELD_HALVINGS: u32 = 10u;

// A grid collider's field at a place in its box's frame: at or above 0 on
// the allowed side; LARGEST (allowed) outside its box.
fn gridField(c: Collider, local: vec3<f32>) -> f32 {
    var u = local / (2.0 * c.halfExtents.xyz) + 0.5;
    let height = c.modes.x == COLLIDER_HEIGHTFIELD;
    if (height) {
        u.y = 0.5;
    }
    var value = vec3<f32>(0.0);
    if (!sampleGrid(c.grid, c.gridModes.x, 1u, u, BORDER_NONE, &value)) {
        return LARGEST;
    }
    let v = select(value.x * c.scales.x, local.y - value.x * c.scales.x, height);
    return select(v, -v, c.modes.y != 0u) - c.scales.y;
}

// The span between a grid's points along the axes it varies on, the least
// of them; 0 for a grid of one point.
fn cellOf(c: Collider) -> f32 {
    var cell = LARGEST;
    for (var k = 0u; k < 3u; k++) {
        let n = c.grid[1u + k];
        if (n > 1u) {
            cell = min(cell, 2.0 * c.halfExtents[k] / f32(n - 1u));
        }
    }
    return select(0.0, cell, cell < LARGEST);
}

// The field's gradient at a local place, unit length, toward the allowed
// side; the box's up where it is flat or a difference leaves the box.
fn gradientAt(c: Collider, local: vec3<f32>) -> vec3<f32> {
    let cell = cellOf(c);
    let h = select(0.5 * c.halfExtents.x, 0.5 * cell, cell > 0.0);
    let f0 = gridField(c, local);
    var g = vec3<f32>(0.0);
    var lost = false;
    for (var k = 0u; k < 3u; k++) {
        var a = local;
        var b = local;
        a[k] += h;
        b[k] -= h;
        let fa = gridField(c, a);
        let fb = gridField(c, b);
        let aIn = fa < LARGEST;
        let bIn = fb < LARGEST;
        // At the box's edge one side is outside: a one-sided difference.
        if (aIn && bIn) {
            g[k] = (fa - fb) / (2.0 * h);
        } else if (aIn || bIn) {
            lost = lost || !(f0 < LARGEST);
            g[k] = select((f0 - fb) / h, (fa - f0) / h, aIn);
        }
    }
    let square = dot3(g, g);
    if (lost || !(square > 0.0 && square <= LARGEST)) {
        return vec3<f32>(0.0, 1.0, 0.0);
    }
    return g * (1.0 / sqrt(square));
}

// A local place on the wrong side put back on the surface: straight up or
// down onto a heightfield, along the gradient out of a distance grid.
fn pushOutOfField(c: Collider, start: vec3<f32>) -> vec3<f32> {
    var local = start;
    for (var turn = 0u; turn < 4u; turn++) {
        let f = gridField(c, local);
        if (!(f < 0.0)) {
            break;
        }
        if (c.modes.x == COLLIDER_HEIGHTFIELD) {
            local.y += select(-f, f, c.modes.y != 0u);
            continue;
        }
        local -= f * gradientAt(c, local);
    }
    return local;
}

// The part [low, high] of the local segment a to b within the grid's box
// (a heightfield's X and Z); false when it misses it.
fn clipToBox(c: Collider, a: vec3<f32>, b: vec3<f32>, low: ptr<function, f32>,
             high: ptr<function, f32>) -> bool {
    *low = 0.0;
    *high = 1.0;
    for (var k = 0u; k < 3u; k++) {
        if (k == 1u && c.modes.x == COLLIDER_HEIGHTFIELD) {
            continue;
        }
        let h = c.halfExtents[k];
        let d = b[k] - a[k];
        if (d == 0.0) {
            if (abs(a[k]) > h) {
                return false;
            }
            continue;
        }
        let t1 = (-h - a[k]) / d;
        let t2 = (h - a[k]) / d;
        *low = max(*low, min(t1, t2));
        *high = min(*high, max(t1, t2));
    }
    return *low <= *high;
}

// The local segment's first crossing into a grid's wrong side, sampled
// every half cell: its share of the segment, on the allowed side.
fn crossField(c: Collider, a: vec3<f32>, b: vec3<f32>, t: ptr<function, f32>) -> bool {
    var low = 0.0;
    var high = 1.0;
    if (!clipToBox(c, a, b, &low, &high)) {
        return false;
    }
    let d = b - a;
    let length = sqrt(dot3(d, d)) * (high - low);
    let cell = cellOf(c);
    let wanted = select(1.0, ceil(length / (0.5 * cell)), cell > 0.0);
    let n = u32(min(max(wanted, 1.0), f32(FIELD_SAMPLES)));
    var before = low;
    for (var i = 0u; i <= n; i++) {
        var at = low + (high - low) * f32(i) / f32(n);
        if (!(gridField(c, a + at * d) < 0.0)) {
            before = at;
            continue;
        }
        // Halve [before, at] toward the crossing, keeping the allowed end.
        for (var halving = 0u; halving < FIELD_HALVINGS && i > 0u; halving++) {
            let middle = 0.5 * (before + at);
            if (gridField(c, a + middle * d) < 0.0) {
                at = middle;
            } else {
                before = middle;
            }
        }
        *t = before;
        return true;
    }
    return false;
}

fn hitField(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let q = c.rotation;
    let inverse = vec4<f32>(-q.xyz, q.w);
    let a = rotate(inverse, s - c.center.xyz);
    let b = rotate(inverse, e - c.center.xyz);
    var local = a;
    var t = 0.0;
    if (gridField(c, a) < 0.0) {
        local = pushOutOfField(c, a);
    } else if (crossField(c, a, b, &t)) {
        local = a + t * (b - a);
    } else {
        return false;
    }
    (*hit).t = t;
    (*hit).point = rotate(q, local) + c.center.xyz;
    (*hit).normal = rotate(q, gradientAt(c, local));
    return true;
}

// The segment's earliest contact with any collider.
fn firstHit(s: vec3<f32>, e: vec3<f32>, best: ptr<function, Hit>) -> bool {
    var found = false;
    for (var i = 0u; i < params.collisionHead.x; i++) {
        let c = params.colliders[i];
        var hit: Hit;
        var met = false;
        switch c.modes.x {
            case COLLIDER_PLANE: {
                met = hitPlane(c, s, e, &hit);
            }
            case COLLIDER_SPHERE: {
                met = hitSphere(c, s, e, &hit);
            }
            case COLLIDER_BOX: {
                met = hitBox(c, s, e, &hit);
            }
            case COLLIDER_HEIGHTFIELD, COLLIDER_DISTANCE: {
                met = hitField(c, s, e, &hit);
            }
            default: {
            }
        }
        if (met && (!found || hit.t < (*best).t)) {
            *best = hit;
            found = true;
        }
    }
    return found;
}

// A vector bounced off a surface: its part into it reversed by
// restitution, its part along it cut by friction.
fn bounce(n: vec3<f32>, v: vec3<f32>) -> vec3<f32> {
    let into = dot3(v, n);
    let normal = select(into, -params.response.x * into, into < 0.0);
    let keep = 1.0 - params.response.y;
    return normal * n + keep * (v - into * n);
}

// A move from start to place met with the colliders: the place and the
// velocity after its contacts (at most MAX_CONTACTS), which it returns.
fn collide(start: vec3<f32>, place: ptr<function, vec3<f32>>,
           velocity: ptr<function, vec3<f32>>) -> u32 {
    var s = start;
    var contacts = 0u;
    var hit: Hit;
    while (contacts < MAX_CONTACTS && firstHit(s, *place, &hit)) {
        contacts += 1u;
        var rest = (*place - s) * (1.0 - hit.t);
        *velocity = bounce(hit.normal, *velocity);
        rest = bounce(hit.normal, rest);
        // Off the surface by a hair, so the next test does not meet it again.
        let p = hit.point;
        let hair = 1e-6 * (1.0 + abs(p.x) + abs(p.y) + abs(p.z));
        let stops = dot3(*velocity, *velocity) < params.response.w * params.response.w;
        s = p + hair * hit.normal;
        if (stops) {
            *velocity = vec3<f32>(0.0);
            *place = s;
            return contacts;
        }
        *place = s + rest;
    }
    if (contacts == MAX_CONTACTS && firstHit(s, *place, &hit)) {
        // Out of contacts with a path still into a surface: stop at it.
        *place = hit.point;
    }
    return contacts;
}

// The first tick a particle born at birth (two words) reaches its life at
// inverse life inverse: its age in binary32 (the tick difference scaled
// by 2^-20) times the inverse; old when the life passes 2^32 ticks.
fn deathAfter(birth: vec2<u32>, inverse: f32, old: vec2<u32>) -> vec2<u32> {
    let span = ceil(TICKS_PER_SECOND / inverse);
    if (!(span < 4.0e9)) {
        return old;
    }
    var tick = max(u32(span), 1u);
    for (var n = 0; n < 64 && tick > 1u && f32(tick - 1u) / TICKS_PER_SECOND * inverse >= 1.0;
         n++) {
        tick -= 1u;
    }
    for (var n = 0; n < 64 && f32(tick) / TICKS_PER_SECOND * inverse < 1.0; n++) {
        tick += 1u;
    }
    let low = birth.x + tick;
    return vec2<u32>(low, birth.y + select(0u, 1u, low < birth.x));
}

// What a move's contacts did to a particle: its contacts, whether one
// kills it, its inverse life and death tick after the lives they took.
struct Fate {
    contacts: u32,
    killed: bool,
    inverseLife: f32,
    death: vec2<u32>,
}

// A move from start met with the colliders, for a particle born at birth
// with inverse life inverse, dying at death.
fn meet(start: vec3<f32>, place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>,
        birth: vec2<u32>, inverse: f32, death: vec2<u32>) -> Fate {
    var fate = Fate(0u, false, inverse, death);
    if (params.collisionHead.x == 0u) {
        return fate;
    }
    fate.contacts = collide(start, place, velocity);
    if (fate.contacts == 0u) {
        return fate;
    }
    if (params.collisionHead.y != 0u) {
        fate.killed = true;
        return fate;
    }
    let loss = params.response.z;
    if (loss > 0.0) {
        // Each contact takes its share of the life left.
        for (var n = 0u; n < fate.contacts; n++) {
            fate.inverseLife /= 1.0 - loss;
        }
        fate.death = deathAfter(birth, fate.inverseLife, death);
    }
    return fate;
}

// Update: each live particle moved, the workgroups (at most a dispatch's
// along one axis) striding over them.
@compute @workgroup_size(64, 1, 1)
fn update(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = counts[0];
    for (var i = id.x; i < live; i += params.modes.z * 64u) {
        moveLive(i);
    }
}

// A live particle shifted with the anchor and moved over the plan; whether
// it outlives the plan.
fn moveLive(i: u32) {
    let c = params.counts.x;
    let start = readVector(0u, i, PLACE) + params.shift.xyz;
    var a = vec3<f32>(0.0);
    var drag = 0.0;
    forcesAt(start, 0.0, &a, &drag);
    let f = factorsFor(drag, f32(params.counts.z) / TICKS_PER_SECOND, true);
    let v0 = readVector(0u, i, VELOCITY);
    var p = start + v0 * f.y + a * f.z;
    var v = v0 * f.x + a * f.y;
    let birth = vec2<u32>(source[BIRTH * c + i], source[(BIRTH + 1u) * c + i]);
    let death = vec2<u32>(source[DEATH * c + i], source[(DEATH + 1u) * c + i]);
    let fate = meet(start, &p, &v, birth, bitcast<f32>(source[INVERSE_LIFE * c + i]), death);
    for (var k = 0u; k < 3u; k++) {
        source[(PLACE + k) * c + i] = bitcast<u32>(p[k]);
        source[(VELOCITY + k) * c + i] = bitcast<u32>(v[k]);
    }
    let count = source[COLLISIONS * c + i];
    source[COLLISIONS * c + i] = select(count + fate.contacts, 0xFFFFFFFFu,
                                        count > 0xFFFFFFFFu - fate.contacts);
    source[INVERSE_LIFE * c + i] = bitcast<u32>(fate.inverseLife);
    source[DEATH * c + i] = fate.death.x;
    source[(DEATH + 1u) * c + i] = fate.death.y;
    source[ALIVE * c + i] = select(0u, 1u, outlives(fate.death.x, fate.death.y) &&
                                               !fate.killed && !killed(start, p));
}

// Adds keep's values of the workgroup in order: each invocation's sum of
// those before it.
fn exclusiveScan(local: u32, keep: u32) -> u32 {
    scan[local] = keep;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var add = 0u;
        if (local >= offset) {
            add = scan[local - offset];
        }
        workgroupBarrier();
        scan[local] += add;
        workgroupBarrier();
    }
    return scan[local] - keep;
}

// Whether element i of the step's sequence is kept: the live particles
// first (live of them), those that outlive the plan; then the plan's
// births, those with room that outlive it, their death from the plan's
// start in two words past its ticks when the high word is set or the low
// passes them.
fn keepAt(i: u32, live: u32) -> u32 {
    let c = params.counts.x;
    if (i < live) {
        return source[ALIVE * c + i];
    }
    let j = i - live;
    if (j >= params.counts.y) {
        return 0u;
    }
    let b0 = births[2u * j];
    let b1 = births[2u * j + 1u];
    return select(0u, 1u, accepted[j] == 1u && (b1.x > 0u || b0.w > params.counts.z));
}

// The workgroups of the reduce and scatter entries: the sequence is at
// most the capacity's live particles and twice the capacity's births.
fn blockCount() -> u32 {
    return (3u * params.counts.x + BLOCK - 1u) / BLOCK;
}

// Reduce: each workgroup's count of kept elements, in blocks.
@compute @workgroup_size(256, 1, 1)
fn reduce(@builtin(local_invocation_index) local: u32,
          @builtin(workgroup_id) group: vec3<u32>) {
    let live = counts[0];
    let first = group.x * BLOCK + local * ITEMS;
    var kept = 0u;
    for (var k = 0u; k < ITEMS; k++) {
        kept += keepAt(first + k, live);
    }
    let before = exclusiveScan(local, kept);
    if (local == CHUNK - 1u) {
        blocks[group.x] = before + kept;
    }
}

// Offsets, in one workgroup: the blocks' counts made each block's first
// place, in order; the live count before the step kept in counts[1], the
// new one in counts[0], those past the capacity in counts[2].
@compute @workgroup_size(256, 1, 1)
fn offsets(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    let n = blockCount();
    if (local == 0u) {
        carried = 0u;
    }
    for (var base = 0u; base < n; base += CHUNK) {
        let k = base + local;
        var count = 0u;
        if (k < n) {
            count = blocks[k];
        }
        let before = exclusiveScan(local, count);
        if (k < n) {
            blocks[k] = carried + before;
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried += before + count;
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        counts[1] = counts[0];
        counts[0] = min(carried, c);
        counts[2] = carried - min(carried, c);
    }
}

// The instance's place at a tick of the plan, between its places at the
// plan's ends: origins from the anchor after, rotation and scale.
fn placeAt(tick: u32, origin: ptr<function, vec3<f32>>, rotation: ptr<function, vec4<f32>>) -> f32 {
    let t = select(1.0, f32(tick) / f32(params.counts.z), params.counts.z > 0u);
    *origin = mix(params.fromOrigin.xyz, params.toOrigin.xyz, t);
    let a = params.fromRotation;
    let b = params.toRotation * select(1.0, -1.0, dot(params.fromRotation, params.toRotation) < 0.0);
    *rotation = normalize(mix(a, b, t));
    return mix(params.fromOrigin.w, params.toOrigin.w, t);
}

// A birth's tick and death tick made absolute (two words each).
fn birthTicks(b0: vec4<u32>, b1: vec4<u32>, birth: ptr<function, vec2<u32>>,
              death: ptr<function, vec2<u32>>) {
    let birthLow = params.start.x + b0.z;
    *birth = vec2<u32>(birthLow, params.start.y + select(0u, 1u, birthLow < params.start.x));
    let deathLow = params.start.x + b0.w;
    *death = vec2<u32>(deathLow,
                       params.start.y + b1.x + select(0u, 1u, deathLow < params.start.x));
}

// A birth of the plan drawn, placed, and moved from its tick to the plan's
// end under its forces at its birth place and met with the colliders: its
// place and velocity then, and its fate, killed too when an attractor
// kills it on the way.
fn bornAt(j: u32, place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>) -> Fate {
    let b0 = births[2u * j];
    let b1 = births[2u * j + 1u];
    var normal = vec3<f32>(0.0);
    let shapeWords = draw(SHAPE_SALT, b0.x, b0.y);
    let local_place = turnByShape(sampleSphere(shapeWords, &normal));
    let drawn = local_place + params.shapeOffset.xyz;
    var v = drawVelocity(draw(VELOCITY_SALT, b0.x, b0.y), turnByShape(normal));
    var p = drawn;
    if (params.start.w == 0u) {
        var origin = vec3<f32>(0.0);
        var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        let scale = placeAt(b0.z, &origin, &rotation);
        p = origin + rotate(rotation, drawn * scale);
        v = rotate(rotation, v * scale) + params.shift.w * params.instanceVelocity.xyz;
    }
    var a = vec3<f32>(0.0);
    var drag = 0.0;
    forcesAt(p, f32(b0.z) / TICKS_PER_SECOND, &a, &drag);
    let f = factorsFor(drag, f32(params.counts.z - b0.z) / TICKS_PER_SECOND, false);
    *place = p + v * f.y + a * f.z;
    *velocity = v * f.x + a * f.y;
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    var fate = meet(p, place, velocity, birth, bitcast<f32>(b1.y), death);
    fate.killed = fate.killed || killed(p, *place);
    return fate;
}

// A birth written to place at of the other buffer; its place.
fn spawnAt(j: u32, at: u32) -> vec3<f32> {
    let c = params.counts.x;
    let b0 = births[2u * j];
    let b1 = births[2u * j + 1u];
    var moved_place = vec3<f32>(0.0);
    var moved_velocity = vec3<f32>(0.0);
    let fate = bornAt(j, &moved_place, &moved_velocity);
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    for (var k = 0u; k < 3u; k++) {
        moved[(PLACE + k) * c + at] = bitcast<u32>(moved_place[k]);
        moved[(VELOCITY + k) * c + at] = bitcast<u32>(moved_velocity[k]);
    }
    moved[SERIAL * c + at] = b0.x;
    moved[(SERIAL + 1u) * c + at] = b0.y;
    moved[BIRTH * c + at] = birth.x;
    moved[(BIRTH + 1u) * c + at] = birth.y;
    moved[DEATH * c + at] = fate.death.x;
    moved[(DEATH + 1u) * c + at] = fate.death.y;
    moved[INVERSE_LIFE * c + at] = bitcast<u32>(fate.inverseLife);
    moved[COLLISIONS * c + at] = fate.contacts;
    return moved_place;
}

// Kill births: each birth with room killed on its way to the plan's end
// (an attractor's kill radius, a contact that kills, a contact's life loss
// that ends its life within the plan) loses its room's mark (2), so the
// scan keeps it not; it held its room for the step, as a killed particle
// does until the step's end. The workgroups stride over the births as
// update's over the particles.
@compute @workgroup_size(64, 1, 1)
fn killBirths(@builtin(global_invocation_id) id: vec3<u32>) {
    if (params.forceHead.y == 0u && params.collisionHead.x == 0u) {
        return;
    }
    for (var j = id.x; j < params.counts.y; j += params.modes.z * 64u) {
        if (accepted[j] != 1u) {
            continue;
        }
        var p = vec3<f32>(0.0);
        var v = vec3<f32>(0.0);
        let fate = bornAt(j, &p, &v);
        if (fate.killed || !outlives(fate.death.x, fate.death.y)) {
            accepted[j] = 2u;
        }
    }
}

// The box around a box and a place, by comparisons that pass a NaN over.
fn lowerOf(low: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(low, p, p < low);
}

fn higherOf(high: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(high, p, p > high);
}

// The workgroup's box from each invocation's, by a fixed tree; whole in
// boxLow[0] and boxHigh[0].
fn reduceBox(local: u32, low: vec3<f32>, high: vec3<f32>) {
    boxLow[local] = vec4<f32>(low, 0.0);
    boxHigh[local] = vec4<f32>(high, 0.0);
    workgroupBarrier();
    for (var half = CHUNK / 2u; half > 0u; half >>= 1u) {
        if (local < half) {
            boxLow[local] = vec4<f32>(lowerOf(boxLow[local].xyz, boxLow[local + half].xyz), 0.0);
            boxHigh[local] =
                vec4<f32>(higherOf(boxHigh[local].xyz, boxHigh[local + half].xyz), 0.0);
        }
        workgroupBarrier();
    }
}

// The empty box: min at the largest binary32, max at the least.
const EMPTY_LOW: f32 = 3.40282347e38;

// Scatter: each kept element to its place in the other buffer, its block's
// first place and those kept before it in the block; a live particle
// moved, a birth spawned; none past the capacity.
@compute @workgroup_size(256, 1, 1)
fn scatter(@builtin(local_invocation_index) local: u32,
           @builtin(workgroup_id) group: vec3<u32>) {
    let c = params.counts.x;
    let live = counts[1];
    let first = group.x * BLOCK + local * ITEMS;
    var keep: array<u32, ITEMS>;
    var kept = 0u;
    for (var k = 0u; k < ITEMS; k++) {
        keep[k] = keepAt(first + k, live);
        kept += keep[k];
    }
    var at = blocks[group.x] + exclusiveScan(local, kept);
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    for (var k = 0u; k < ITEMS; k++) {
        let i = first + k;
        if (keep[k] == 1u && at < c) {
            var p = vec3<f32>(0.0);
            if (i < live) {
                for (var w = 0u; w < ALIVE; w++) {
                    moved[w * c + at] = source[w * c + i];
                }
                p = readVector(0u, i, PLACE);
            } else {
                p = spawnAt(i - live, at);
            }
            low = lowerOf(low, p);
            high = higherOf(high, p);
        }
        at += keep[k];
    }
    reduceBox(local, low, high);
    if (local == 0u) {
        let box = blockCount() + 6u * group.x;
        for (var k = 0u; k < 3u; k++) {
            blocks[box + k] = bitcast<u32>(boxLow[0][k]);
            blocks[box + 3u + k] = bitcast<u32>(boxHigh[0][k]);
        }
    }
}

// Measure, in one workgroup: the blocks' boxes reduced by a fixed tree to
// the emitter's, in the particles' space, into the statistics record with
// the growth by the largest size (1: the part runs no sizes yet).
@compute @workgroup_size(256, 1, 1)
fn measure(@builtin(local_invocation_index) local: u32) {
    let n = blockCount();
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    for (var g = local; g < n; g += CHUNK) {
        let box = n + 6u * g;
        low = lowerOf(low, vec3<f32>(bitcast<f32>(blocks[box]), bitcast<f32>(blocks[box + 1u]),
                                     bitcast<f32>(blocks[box + 2u])));
        high = higherOf(high, vec3<f32>(bitcast<f32>(blocks[box + 3u]),
                                        bitcast<f32>(blocks[box + 4u]),
                                        bitcast<f32>(blocks[box + 5u])));
    }
    reduceBox(local, low, high);
    if (local == 0u) {
        counts[3] = bitcast<u32>(1.0);
        for (var k = 0u; k < 3u; k++) {
            counts[4u + k] = bitcast<u32>(boxLow[0][k]);
            counts[7u + k] = bitcast<u32>(boxHigh[0][k]);
        }
    }
}

// Whether a birth of the plan dies by tick t of the plan, its death in two
// words from the plan's start.
fn birthDiesBy(j: u32, t: u32) -> bool {
    let b0 = births[2u * j];
    let b1 = births[2u * j + 1u];
    return b1.x == 0u && b0.w <= t;
}

// Whether a live particle dies by tick t of the plan, its death absolute.
fn liveDiesBy(i: u32, t: u32) -> bool {
    let c = params.counts.x;
    let low = source[DEATH * c + i];
    let high = source[(DEATH + 1u) * c + i];
    let tickLow = params.start.x + t;
    let tickHigh = params.start.y + select(0u, 1u, tickLow < params.start.x);
    return high < tickHigh || (high == tickHigh && low <= tickLow);
}

// Room: which of the plan's births the emitter takes, as the CPU path's
// full step takes them. When the live particles and the births fit the
// capacity, all; else, batch by batch in order, as many of each as the
// capacity has room for at its tick, the deaths by then of the live
// particles and of the births taken before counted across the workgroup.
// It runs before update, as the CPU path takes a full step's births
// before it moves: the deaths are those the step began with (and the
// plan's for births), not those its contacts shorten or its kills.
@compute @workgroup_size(256, 1, 1)
fn room(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    let count = params.counts.y;
    if (local == 0u) {
        liveCount = counts[0];
        carried = 0u;
        batchStart = 0u;
    }
    let live = workgroupUniformLoad(&liveCount);
    if (live + count <= c) {
        for (var j = local; j < count; j += CHUNK) {
            accepted[j] = 1u;
        }
        return;
    }
    loop {
        let start = workgroupUniformLoad(&batchStart);
        if (start >= count) {
            break;
        }
        let tick = births[2u * start].z;
        if (local == 0u) {
            // The batch's births in the plan: from its first on, its places
            // in order at one tick.
            var n = 1u;
            while (start + n < count && births[2u * (start + n) + 1u].z == n &&
                   births[2u * (start + n)].z == tick) {
                n++;
            }
            batchLength = n;
        }
        workgroupBarrier();
        var mine = 0u;
        for (var i = local; i < live; i += CHUNK) {
            mine += select(0u, 1u, liveDiesBy(i, tick));
        }
        for (var j = local; j < start; j += CHUNK) {
            mine += select(0u, 1u, accepted[j] == 1u && birthDiesBy(j, tick));
        }
        // The workgroup's sum, by its scan: the last invocation's place
        // plus its own.
        let before = exclusiveScan(local, mine);
        if (local == CHUNK - 1u) {
            dead = before + mine;
        }
        workgroupBarrier();
        if (local == 0u) {
            let held = live + carried - dead;
            let space = select(0u, c - held, held < c);
            // The batch's births in the plan are at most those it owes.
            taken = min(space, batchLength);
            carried += taken;
            batchStart = start + batchLength;
        }
        workgroupBarrier();
        let n = workgroupUniformLoad(&batchLength);
        let k = workgroupUniformLoad(&taken);
        for (var j = local; j < n; j += CHUNK) {
            accepted[start + j] = select(0u, 1u, j < k);
        }
        storageBarrier();
        workgroupBarrier();
    }
}
