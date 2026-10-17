// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's step on the GPU: update, room, then the survivors and the
// births kept in order by a scan in three passes (reduce, offsets,
// scatter), and their box measured. Particles are arrays of the capacity
// in one buffer of words: the emitter's values as the CPU path stores
// them, then the death tick (2) and whether it outlives the step, each
// value's first word given by the step's numbers (Words).

// A word of a value the emitter does not store.
const NONE: u32 = 0xFFFFFFFFu;
const CHUNK: u32 = 256u;
// The elements an invocation of the scan's reduce and scatter takes, in a
// row; a workgroup's block is CHUNK times as many.
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;
const TWO_PI: f32 = 6.28318530717958647692;

// include: exact.wgsl

const SHAPE_SALT: u32 = 0x53484150u;
const VELOCITY_SALT: u32 = 0x56454C4Fu;
const SIZE_SALT: u32 = 0x53495A45u;
const COLOR_SALT: u32 = 0x434F4C52u;
const FRAME_SALT: u32 = 0x46524D45u;
const CUSTOM_SALT: u32 = 0x43555354u;
const MESH_SALT: u32 = 0x4D455348u;
const ANGLE_SALT: u32 = 0x414E474Cu;
const SPIN_SALT: u32 = 0x5350494Eu;
const ORIENTATION_SALT: u32 = 0x4F524E54u;
const ANGULAR_SALT: u32 = 0x414E4755u;
const DRAG_SALT: u32 = 0x44524147u;

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

// The depth image as resolved for the step: whether it is met, its size
// and the samples a step takes at most; whether it is reversed, whether
// leaving it kills, whether a normal image is given; the radius and the
// thickness in the particles' space; the turn of its normals into the
// particles' space; the maps between the particles' space and clip space.
struct Depth {
    head: vec4<u32>,
    modes: vec4<u32>,
    sizes: vec4<f32>,
    normalTurn: vec4<f32>,
    toClip: mat4x4<f32>,
    fromClip: mat4x4<f32>,
}

// Where a particle's values are among its words: each one's first word
// (NONE for one the emitter does not store); the emitter's arrays, after
// which come the death tick's two words and whether it outlives the step
// (death and alive).
struct Words {
    place: u32,
    velocity: u32,
    serial: u32,
    birth: u32,
    inverseLife: u32,
    collisions: u32,
    death: u32,
    alive: u32,
    size: u32,
    color: u32,
    angle: u32,
    spin: u32,
    orientation: u32,
    angular: u32,
    frame: u32,
    drag: u32,
    mesh: u32,
    count: u32,
    unused0: u32,
    unused1: u32,
    custom: array<vec4<u32>, 2>,
}

// A value source as compiled (mvfxSourceView): its kind, whether one
// draw serves every component, its components and parameter; its baked
// curve's and gradient's first words in the grids buffer (NONE for
// none); its bounds.
struct Source {
    modes: vec4<u32>,
    tables: vec4<u32>,
    low: vec4<f32>,
    high: vec4<f32>,
}

// A kill rule as resolved (mvfxPlannedKillRule): its kind, the
// collisions that kill, its least age in ticks (two words); its center
// and radius; its half sizes and speed; its rotation; its normal.
struct KillRule {
    modes: vec4<u32>,
    center: vec4<f32>,
    half: vec4<f32>,
    rotation: vec4<f32>,
    normal: vec4<f32>,
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
    // the colliders' count, whether a contact kills, whether it meets
    // anything (colliders or a depth image), unused; the response:
    // restitution, friction, life loss, rest speed
    collisionHead: vec4<u32>,
    response: vec4<f32>,
    depth: Depth,
    colliders: array<Collider, 16>,
    words: Words,
    // The instance's parameters.
    parameters: array<vec4<f32>, 16>,
    // The angular drag's factors over the plan: decay, carry; the speed
    // limit's decay over the plan; unused.
    angular: vec4<f32>,
    // The velocity modules in the particles' space: the added direction
    // and the scale into the particles' space; the orbit's axis; its
    // center; the radial center.
    modules: array<vec4<f32>, 4>,
    // The kill rules' count, the events the emitter writes (a mask of
    // EVENT_ kinds), at most how many, the events' first word in the
    // events buffer; the rules.
    fateHead: vec4<u32>,
    kills: array<KillRule, 8>,
    // The sources: the births' size, colour, angle, spin, orientation,
    // angular velocity, drag scale and custom values.
    sources: array<Source, 15>,
    // The frame's and the mesh index's draws: first, last, whether random.
    draws: array<vec4<u32>, 2>,
    // The bounds' growth by size: the most the size over life scales each
    // axis; the margin for rounding.
    growth: vec4<f32>,
    // How particles turn and move: the orientation's mode, the angular
    // drag (binary32 bits), whether the speed is limited (1) and modules
    // move them (2), how fast the speed falls to the limit (bits).
    turns: vec4<u32>,
    // The values over life (LIFE_ROTATION and on), two words each: the
    // value (binary32 bits) and the baked curve's first word (NONE for
    // none).
    lives: array<vec4<u32>, 3>,
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
// The host's depth image and its normals in the view's space, when the
// step's depth is met and they are given (else the simulation's own).
@group(0) @binding(9) var depthImage: texture_depth_2d;
@group(0) @binding(10) var normalImage: texture_2d<f32>;
// An emitter's events (a word of the simulation's for one that writes
// none): each live particle's facts of the step, EVENT_FACTS words (the
// flags EVENT_ENDED and EVENT_CONTACT, the first contact's share, point
// and normal, the inverse life it met); each block of the sequence's
// counts of the four groups' events, then their first places; the
// events, EVENT_WORDS each.
@group(0) @binding(8) var<storage, read_write> events: array<u32>;
// Each block of the sequence's count of kept elements, then its first
// place; after them (blockCount() words on), each block's box of the
// places scatter wrote: min then max, three binary32 words each; after
// them (7 blockCount() words on), each block's growth by size.
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

const SOURCE_SIZE: u32 = 0u;
const SOURCE_COLOR: u32 = 1u;
const SOURCE_ANGLE: u32 = 2u;
const SOURCE_SPIN: u32 = 3u;
const SOURCE_ORIENTATION: u32 = 4u;
const SOURCE_ANGULAR: u32 = 5u;
const SOURCE_DRAG: u32 = 6u;
const SOURCE_CUSTOM: u32 = 7u;
const LIFE_ROTATION: u32 = 0u;
const LIFE_SPEED_LIMIT: u32 = 1u;
const LIFE_MOTION_SCALE: u32 = 2u;
const LIFE_ADDED_SPEED: u32 = 3u;
const LIFE_RADIAL_SPEED: u32 = 4u;
const LIFE_ORBIT_RATE: u32 = 5u;
const LIMITS_SPEED: u32 = 1u;
const MODULES_MOVE: u32 = 2u;
const KILL_INSIDE_SPHERE: u32 = 0u;
const KILL_OUTSIDE_SPHERE: u32 = 1u;
const KILL_INSIDE_BOX: u32 = 2u;
const KILL_OUTSIDE_BOX: u32 = 3u;
const KILL_PAST_PLANE: u32 = 4u;
const KILL_COLLISIONS: u32 = 6u;
const ORIENTATION_EULER: u32 = 1u;
const ORIENTATION_RANDOM: u32 = 2u;
const MAX_DRAG: f32 = 1000.0;
const SOURCE_CURVE: u32 = 2u;
const SOURCE_PARAMETER: u32 = 3u;
const SOURCE_GRADIENT: u32 = 4u;
const SOURCE_GRADIENT_OVER_AGE: u32 = 5u;
const SOURCE_RANGE: u32 = 1u;
const CURVE_SAMPLES: u32 = 256u;

// A table's binary32 at a word of the grids buffer.
fn tableAt(at: u32) -> f32 {
    return bitcast<f32>(grids[at]);
}

// A baked curve's value at a time, as the core blends it: its samples,
// then its domain's start and the factor to a sample position.
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
fn gradientTableAt(first: u32, t: f32) -> vec4<f32> {
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

// A source's vector at a source age from four drawn words, as the core's
// mvfxDrawVector: a base by its kind times a draw between its bounds for
// each component it has.
fn drawVector(s: u32, age: f32, words: vec4<u32>) -> vec4<f32> {
    let source = params.sources[s];
    var base = vec4<f32>(1.0);
    switch source.modes.x {
        case SOURCE_CURVE: {
            base = vec4<f32>(curveAt(source.tables.x, age));
        }
        case SOURCE_PARAMETER: {
            base = params.parameters[source.modes.w];
        }
        case SOURCE_GRADIENT: {
            base = gradientTableAt(source.tables.y, unitFloat(pcg4dMix(words).x));
        }
        case SOURCE_GRADIENT_OVER_AGE: {
            base = gradientTableAt(source.tables.y, age);
        }
        case SOURCE_RANGE: {
        }
        default: {
            return vec4<f32>(0.0);
        }
    }
    var out = vec4<f32>(0.0);
    for (var c = 0u; c < 4u; c++) {
        let u = unitFloat(select(words[c], words.x, source.modes.y != 0u));
        let value = source.low[c] + (source.high[c] - source.low[c]) * u;
        out[c] = select(0.0, base[c] * value, c < source.modes.z);
    }
    return out;
}

// The high word of a 32-bit product.
fn productHigh(a: u32, b: u32) -> u32 {
    let al = a & 0xFFFFu;
    let ah = a >> 16u;
    let bl = b & 0xFFFFu;
    let bh = b >> 16u;
    let low = al * bl;
    let middle1 = ah * bl;
    let middle2 = al * bh;
    let carry = ((low >> 16u) + (middle1 & 0xFFFFu) + (middle2 & 0xFFFFu)) >> 16u;
    return ah * bh + (middle1 >> 16u) + (middle2 >> 16u) + carry;
}

// An index drawn at birth: first, or uniform up to last when random.
fn drawIndex(d: vec4<u32>, salt: u32, serialLow: u32, serialHigh: u32) -> u32 {
    if (d.z == 0u) {
        return d.x;
    }
    return d.x + productHigh(draw(salt, serialLow, serialHigh).x, d.y - d.x + 1u);
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
// accelerations in order, and the drag the winds and velocity fields add
// (the emitter's own added after, as the core sums them).
fn forcesAt(x: vec3<f32>, seconds: f32, a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) {
    *a = params.gravity.xyz;
    *drag = 0.0;
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

// Whether a segment passes within radius of a center: its closest point
// to it, as the core's mvfxSegmentMeetsSphere.
fn segmentMeetsSphere(s: vec3<f32>, e: vec3<f32>, center: vec3<f32>, radius: f32) -> bool {
    let d = e - s;
    let f = s - center;
    let length = dot3(d, d);
    var t = select(0.0, -dot3(f, d) / length, length > 0.0);
    t = min(max(t, 0.0), 1.0);
    let closest = f + t * d;
    return dot3(closest, closest) <= radius * radius;
}

// A place in a kill rule's box's own frame.
fn intoBox(rule: KillRule, place: vec3<f32>) -> vec3<f32> {
    let q = rule.rotation;
    return rotate(vec4<f32>(-q.xyz, q.w), place - rule.center.xyz);
}

// Whether a segment meets a kill rule's box: the slabs' overlap along it.
fn segmentMeetsBox(rule: KillRule, s: vec3<f32>, e: vec3<f32>) -> bool {
    let a = intoBox(rule, s);
    let b = intoBox(rule, e);
    var low = 0.0;
    var high = 1.0;
    for (var k = 0u; k < 3u; k++) {
        let d = b[k] - a[k];
        let h = rule.half[k];
        if (d == 0.0) {
            if (abs(a[k]) > h) {
                return false;
            }
            continue;
        }
        let t1 = (-h - a[k]) / d;
        let t2 = (h - a[k]) / d;
        low = max(low, min(t1, t2));
        high = min(high, max(t1, t2));
    }
    return low <= high;
}

// Whether a kill rule kills a particle that went from start to end and
// ends with velocity v, age ticks old (two words) and collisions
// contacts, as the core's.
fn ruleKills(rule: KillRule, start: vec3<f32>, end: vec3<f32>, v: vec3<f32>, age: vec2<u32>,
             collisions: u32) -> bool {
    switch rule.modes.x {
        case KILL_INSIDE_SPHERE: {
            return segmentMeetsSphere(start, end, rule.center.xyz, rule.center.w);
        }
        case KILL_OUTSIDE_SPHERE: {
            let d = end - rule.center.xyz;
            return dot3(d, d) > rule.center.w * rule.center.w;
        }
        case KILL_INSIDE_BOX: {
            return segmentMeetsBox(rule, start, end);
        }
        case KILL_OUTSIDE_BOX: {
            let local = abs(intoBox(rule, end));
            return !(local.x <= rule.half.x && local.y <= rule.half.y && local.z <= rule.half.z);
        }
        case KILL_PAST_PLANE: {
            return dot3(end - rule.center.xyz, rule.normal.xyz) > 0.0;
        }
        case KILL_COLLISIONS: {
            return collisions >= rule.modes.y;
        }
        default: {
            let old = age.y > rule.modes.w || (age.y == rule.modes.w && age.x >= rule.modes.z);
            return old && dot3(v, v) < rule.half.w * rule.half.w;
        }
    }
}

// Whether any kill rule kills a particle.
fn killedByRules(start: vec3<f32>, end: vec3<f32>, v: vec3<f32>, age: vec2<u32>,
                 collisions: u32) -> bool {
    for (var r = 0u; r < params.fateHead.x; r++) {
        if (ruleKills(params.kills[r], start, end, v, age, collisions)) {
            return true;
        }
    }
    return false;
}

// Whether a step from start to end is killed: by a kill rule (with the
// particle's velocity, age in ticks and contacts at its end), or by
// passing within an attractor's kill radius of its point.
fn killed(start: vec3<f32>, end: vec3<f32>, v: vec3<f32>, age: vec2<u32>, collisions: u32) -> bool {
    if (killedByRules(start, end, v, age, collisions)) {
        return true;
    }
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

// Drag factors over seconds for a particle with the winds' drag wind:
// when the emitter scales drag, the emitter's times the particle's scale,
// none unless above 0 and at most the most, as the core's ParticleDrag;
// else the emitter's, the shared factors over the plan when it comes to
// the shared drag.
fn factorsFor(wind: f32, scale: f32, seconds: f32, whole: bool) -> vec3<f32> {
    if (params.words.drag != NONE) {
        let own = params.factors.w * scale;
        return factors(select(0.0, min(own, MAX_DRAG), own > 0.0) + wind, seconds);
    }
    let drag = params.factors.w + wind;
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

// A place projected into the depth image: its device coordinates and its
// pixel.
struct Projected {
    ndc: vec3<f32>,
    column: i32,
    row: i32,
}

// Whether a place in the particles' space projects into the image, in
// front of the camera, and where.
fn project(x: vec3<f32>, p: ptr<function, Projected>) -> bool {
    let clip = params.depth.toClip * vec4<f32>(x, 1.0);
    if (!(clip.w > 0.0)) {
        return false;
    }
    let ndc = clip.xyz / clip.w;
    if (!(abs(ndc.x) <= 1.0 && abs(ndc.y) <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0)) {
        return false;
    }
    let w = i32(params.depth.head.y);
    let h = i32(params.depth.head.z);
    (*p).ndc = ndc;
    (*p).column = min(i32((ndc.x * 0.5 + 0.5) * f32(w)), w - 1);
    (*p).row = min(i32((0.5 - ndc.y * 0.5) * f32(h)), h - 1);
    return true;
}

// A pixel's device depth; false where it holds none.
fn depthAt(column: i32, row: i32, d: ptr<function, f32>) -> bool {
    if (column < 0 || row < 0 || column >= i32(params.depth.head.y) ||
        row >= i32(params.depth.head.z)) {
        return false;
    }
    *d = textureLoad(depthImage, vec2<i32>(column, row), 0);
    let far = select(1.0, 0.0, params.depth.modes.x != 0u);
    return *d >= 0.0 && *d <= 1.0 && *d != far;
}

// The particles' place at device x, y and depth; false at infinity.
fn unproject(x: f32, y: f32, d: f32, out: ptr<function, vec3<f32>>) -> bool {
    let h = params.depth.fromClip * vec4<f32>(x, y, d, 1.0);
    if (h.w == 0.0) {
        return false;
    }
    *out = h.xyz / h.w;
    return all(abs(*out) <= vec3<f32>(LARGEST));
}

// A pixel's surface place, at its center; false where it has none.
fn pixelPoint(column: i32, row: i32, out: ptr<function, vec3<f32>>) -> bool {
    var d = 0.0;
    if (!depthAt(column, row, &d)) {
        return false;
    }
    let x = (f32(column) + 0.5) / f32(params.depth.head.y) * 2.0 - 1.0;
    let y = 1.0 - (f32(row) + 0.5) / f32(params.depth.head.z) * 2.0;
    return unproject(x, y, d, out);
}

// The shorter of the differences to a pixel's two neighbors along an
// axis; false with neither.
fn across(center: vec3<f32>, column: i32, row: i32, dc: i32, dr: i32,
          out: ptr<function, vec3<f32>>) -> bool {
    var after = vec3<f32>(0.0);
    var before = vec3<f32>(0.0);
    let hasAfter = pixelPoint(column + dc, row + dr, &after);
    let hasBefore = pixelPoint(column - dc, row - dr, &before);
    let a = after - center;
    let b = center - before;
    if (!hasAfter && !hasBefore) {
        return false;
    }
    let useAfter = hasAfter && (!hasBefore || sqrt(dot3(a, a)) <= sqrt(dot3(b, b)));
    *out = select(b, a, useAfter);
    return true;
}

// The direction toward the camera along the ray at device x, y.
fn towardCamera(x: f32, y: f32) -> vec3<f32> {
    let reversed = params.depth.modes.x != 0u;
    var nearer = vec3<f32>(0.0);
    var farther = vec3<f32>(0.0);
    if (unproject(x, y, select(0.25, 0.75, reversed), &nearer) &&
        unproject(x, y, select(0.75, 0.25, reversed), &farther)) {
        return nearer - farther;
    }
    return rotate(params.depth.normalTurn, vec3<f32>(0.0, 0.0, 1.0));
}

// The surface's unit normal at a pixel, toward the camera: the normal
// image's turned, or the cross of the shorter neighbor differences.
fn depthNormal(p: Projected) -> vec3<f32> {
    let toward = towardCamera(p.ndc.x, p.ndc.y);
    var n = toward;
    var center = vec3<f32>(0.0);
    var dx = vec3<f32>(0.0);
    var dy = vec3<f32>(0.0);
    if (params.depth.modes.z != 0u) {
        let given = textureLoad(normalImage, vec2<i32>(p.column, p.row), 0).xyz;
        let finite = abs(given) <= vec3<f32>(LARGEST);
        n = rotate(params.depth.normalTurn, select(vec3<f32>(0.0), given, finite));
    } else if (pixelPoint(p.column, p.row, &center) && across(center, p.column, p.row, 1, 0, &dx) &&
               across(center, p.column, p.row, 0, 1, &dy)) {
        n = vec3<f32>(dx.y * dy.z - dx.z * dy.y, dx.z * dy.x - dx.x * dy.z,
                      dx.x * dy.y - dx.y * dy.x);
    }
    var length = sqrt(dot3(n, n));
    if (!(length > 0.0 && length <= LARGEST)) {
        n = toward;
        length = sqrt(dot3(n, n));
    }
    let sign = select(1.0, -1.0, dot3(n, toward) < 0.0);
    return sign * n / length;
}

// Whether a place meets the image: along its ray, within the radius in
// front of the pixel's surface or less than the thickness behind it; the
// surface's place on that ray.
fn meetsDepth(place: vec3<f32>, p: ptr<function, Projected>, surface: ptr<function, vec3<f32>>)
    -> bool {
    var d = 0.0;
    if (!project(place, p) || !depthAt((*p).column, (*p).row, &d) ||
        !unproject((*p).ndc.x, (*p).ndc.y, d, surface)) {
        return false;
    }
    let toward = towardCamera((*p).ndc.x, (*p).ndc.y);
    let behind = -dot3(place - *surface, toward) / sqrt(dot3(toward, toward));
    return behind > -params.depth.sizes.x && behind < params.depth.sizes.y;
}

// The samples a segment takes: one a pixel it crosses and one each half
// thickness, at least one, at most the emitter's.
fn depthSamples(s: vec3<f32>, e: vec3<f32>) -> u32 {
    let d = e - s;
    var wanted = sqrt(dot3(d, d)) / (0.5 * params.depth.sizes.y);
    var ps: Projected;
    var pe: Projected;
    let most = f32(params.depth.head.w);
    if (!project(s, &ps) || !project(e, &pe)) {
        wanted = most;
    } else {
        let pixels = max(abs(f32(pe.column - ps.column)), abs(f32(pe.row - ps.row)));
        wanted = max(wanted, pixels);
    }
    return u32(min(max(ceil(wanted), 1.0), most));
}

// The halvings that find a crossing between two samples.
const DEPTH_HALVINGS: u32 = 10u;

// The first share of the segment that meets the image, sampled and then
// halved toward from the sample before; -1 when none does.
fn firstMeeting(s: vec3<f32>, e: vec3<f32>, p: ptr<function, Projected>,
                surface: ptr<function, vec3<f32>>) -> f32 {
    let n = depthSamples(s, e);
    for (var i = 0u; i <= n; i++) {
        var hi = f32(i) / f32(n);
        if (!meetsDepth(s + hi * (e - s), p, surface)) {
            continue;
        }
        var lo = select(hi, f32(i - 1u) / f32(n), i > 0u);
        for (var halving = 0u; halving < DEPTH_HALVINGS && i > 0u; halving++) {
            let middle = 0.5 * (lo + hi);
            var q: Projected;
            var at = vec3<f32>(0.0);
            if (meetsDepth(s + middle * (e - s), &q, &at)) {
                hi = middle;
                *p = q;
                *surface = at;
            } else {
                lo = middle;
            }
        }
        return hi;
    }
    return -1.0;
}

// The segment's first meeting with the depth image: the surface grown by
// the radius along its normal.
fn hitDepth(s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    var p: Projected;
    var surface = vec3<f32>(0.0);
    let at = firstMeeting(s, e, &p, &surface);
    if (at < 0.0) {
        return false;
    }
    let normal = depthNormal(p);
    (*hit).t = at;
    (*hit).normal = normal;
    (*hit).point = surface + params.depth.sizes.x * normal;
    return true;
}

// The segment's earliest contact with the depth image or any collider.
fn firstHit(s: vec3<f32>, e: vec3<f32>, best: ptr<function, Hit>) -> bool {
    var found = params.depth.head.x != 0u && hitDepth(s, e, best);
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
           velocity: ptr<function, vec3<f32>>, first: ptr<function, Hit>) -> u32 {
    var s = start;
    var contacts = 0u;
    var hit: Hit;
    while (contacts < MAX_CONTACTS && firstHit(s, *place, &hit)) {
        if (contacts == 0u) {
            *first = hit;
        }
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
// inverse life inverse, as the core's.
fn deathAfter(birth: vec2<u32>, inverse: f32) -> vec2<u32> {
    return add64(birth, lifeTicks(inverse));
}

// What a move's contacts did to a particle: its contacts, whether one
// kills it, its inverse life and death tick after the lives they took.
struct Fate {
    contacts: u32,
    killed: bool,
    inverseLife: f32,
    death: vec2<u32>,
    // Whether its contacts make an event (none when leaving the view
    // kills it), the first of them.
    contacted: bool,
    first: Hit,
}

// A move from start met with the colliders, for a particle born at birth
// with inverse life inverse, dying at death.
fn meet(start: vec3<f32>, place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>,
        birth: vec2<u32>, inverse: f32, death: vec2<u32>) -> Fate {
    var fate = Fate(0u, false, inverse, death, false, Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0)));
    if (params.collisionHead.z == 0u) {
        return fate;
    }
    // Through a variable of its own: naga's SPIR-V of a pointer to a
    // struct member passed on fails.
    var first = Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0));
    fate.contacts = collide(start, place, velocity, &first);
    fate.first = first;
    var seen: Projected;
    if (params.depth.modes.y != 0u && !project(*place, &seen)) {
        // Out of the view, where leaving it kills.
        fate.killed = true;
        return fate;
    }
    if (fate.contacts == 0u) {
        return fate;
    }
    fate.contacted = true;
    if (params.collisionHead.y != 0u) {
        fate.killed = true;
        return fate;
    }
    let loss = params.response.z;
    if (loss > 0.0) {
        // Each contact takes its share of the life left.
        for (var n = 0u; n < fate.contacts; n++) {
            fate.inverseLife = divide(fate.inverseLife, 1.0 - loss);
        }
        fate.death = deathAfter(birth, fate.inverseLife);
    }
    return fate;
}

// The words of a live particle's facts in the events buffer, and its
// flags: its life ended within the plan; its contacts make an event.
const EVENT_FACTS: u32 = 9u;
const EVENT_ENDED: u32 = 1u;
const EVENT_CONTACT: u32 = 2u;

// Notes a live particle's facts of the step for its events, when the
// emitter writes any: its flags, its first contact, the inverse life it
// met it with.
fn noteFacts(i: u32, flags: u32, first: Hit, inverse: f32) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let at = EVENT_FACTS * i;
    events[at] = flags;
    events[at + 1u] = bitcast<u32>(first.t);
    for (var k = 0u; k < 3u; k++) {
        events[at + 2u + k] = bitcast<u32>(first.point[k]);
        events[at + 5u + k] = bitcast<u32>(first.normal[k]);
    }
    events[at + 8u] = bitcast<u32>(inverse);
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
// it outlives the plan. One whose life ends within the plan is taken out
// unmoved, as the CPU path takes the ended out before the moves: it dies
// where the step found it.
fn moveLive(i: u32) {
    let c = params.counts.x;
    if (!outlives(source[params.words.death * c + i], source[(params.words.death + 1u) * c + i])) {
        source[params.words.alive * c + i] = 0u;
        noteFacts(i, EVENT_ENDED, Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0)), 0.0);
        return;
    }
    let start = readVector(0u, i, params.words.place) + params.shift.xyz;
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(start, 0.0, &a, &wind);
    var scale = 1.0;
    if (params.words.drag != NONE) {
        scale = bitcast<f32>(source[params.words.drag * c + i]);
    }
    let seconds = secondsOf(vec2<u32>(params.counts.z, 0u));
    let f = factorsFor(wind, scale, seconds, true);
    let birth = vec2<u32>(source[params.words.birth * c + i], source[(params.words.birth + 1u) * c + i]);
    let death = vec2<u32>(source[params.words.death * c + i], source[(params.words.death + 1u) * c + i]);
    let inverseLife = bitcast<f32>(source[params.words.inverseLife * c + i]);
    let age = ageAt(birth, inverseLife);
    let v0 = limitSpeed(readVector(0u, i, params.words.velocity), age, params.angular.z);
    var p = moveByModules(start, start + v0 * f.y + a * f.z, age, seconds);
    var v = v0 * f.x + a * f.y;
    let fate = meet(start, &p, &v, birth, inverseLife, death);
    noteFacts(i, select(0u, EVENT_CONTACT, fate.contacted), fate.first, inverseLife);
    for (var k = 0u; k < 3u; k++) {
        source[(params.words.place + k) * c + i] = bitcast<u32>(p[k]);
        source[(params.words.velocity + k) * c + i] = bitcast<u32>(v[k]);
    }
    let collisions = params.words.collisions;
    var contacts = 0u;
    if (collisions != NONE) {
        let count = source[collisions * c + i];
        contacts = select(count + fate.contacts, 0xFFFFFFFFu, count > 0xFFFFFFFFu - fate.contacts);
        source[collisions * c + i] = contacts;
    }
    source[params.words.inverseLife * c + i] = bitcast<u32>(fate.inverseLife);
    turn(0u, i, params.angular.xy, lifeAt(LIFE_ROTATION, ageAt(birth, fate.inverseLife)));
    source[params.words.death * c + i] = fate.death.x;
    source[(params.words.death + 1u) * c + i] = fate.death.y;
    // Its age at the plan's end, in ticks.
    let endLow = params.start.x + params.counts.z;
    let endHigh = params.start.y + select(0u, 1u, endLow < params.start.x);
    let ageTicks = vec2<u32>(endLow - birth.x, endHigh - birth.y - select(0u, 1u, endLow < birth.x));
    source[params.words.alive * c + i] =
        select(0u, 1u, outlives(fate.death.x, fate.death.y) && !fate.killed &&
                           !killed(start, p, v, ageTicks, contacts));
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
        return source[params.words.alive * c + i];
    }
    let j = i - live;
    if (j >= params.counts.y) {
        return 0u;
    }
    let b0 = births[3u * j];
    let b1 = births[3u * j + 1u];
    return select(0u, 1u, (accepted[j] & 3u) == 1u && (b1.x > 0u || b0.w > params.counts.z));
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
    var born = vec3<f32>(0.0);
    var bornVelocity = vec3<f32>(0.0);
    return bornFacts(j, &born, &bornVelocity, place, velocity);
}

// bornAt with the birth's place and velocity at its tick, before its
// move, as its birth event gives them.
fn bornFacts(j: u32, born: ptr<function, vec3<f32>>, bornVelocity: ptr<function, vec3<f32>>,
             place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>) -> Fate {
    let b0 = births[3u * j];
    let b1 = births[3u * j + 1u];
    var normal = vec3<f32>(0.0);
    let shapeWords = draw(SHAPE_SALT, b0.x, b0.y);
    let local_place = turnByShape(sampleSphere(shapeWords, &normal));
    let drawn = local_place + params.shapeOffset.xyz;
    var v = drawVelocity(draw(VELOCITY_SALT, b0.x, b0.y), turnByShape(normal));
    var p = drawn;
    let host = births[3u * j + 2u].y;
    if (host != NONE) {
        // The host's particle: its place and velocity as the core gives them.
        let record = hostBirthAt(host);
        p = record.place.xyz;
        v = record.velocity.xyz;
    } else if (params.start.w == 0u) {
        var origin = vec3<f32>(0.0);
        var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        let scale = placeAt(b0.z, &origin, &rotation);
        p = origin + rotate(rotation, drawn * scale);
        v = rotate(rotation, v * scale) + params.shift.w * params.instanceVelocity.xyz;
    }
    *born = p;
    *bornVelocity = v;
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(p, secondsOf(vec2<u32>(b0.z, 0u)), &a, &wind);
    let seconds = remainderOf(j);
    let f = factorsFor(wind, dragScaleOf(j), seconds, false);
    // Born at age 0, its speed limit's decay its own.
    v = limitSpeed(v, 0.0, factors(bitcast<f32>(params.turns.w), seconds).x);
    *place = moveByModules(p, p + v * f.y + a * f.z, 0.0, seconds);
    *velocity = v * f.x + a * f.y;
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    var fate = meet(p, place, velocity, birth, bitcast<f32>(b1.y), death);
    let age = vec2<u32>(params.counts.z - b0.z, 0u);
    fate.killed = fate.killed || killed(p, *place, *velocity, age, fate.contacts);
    return fate;
}

// A host's particle of the plan after the births' room (twice the
// capacity's, three words each): its place in the particles' space and
// what it gives (HOST_GIVES_COLOR, HOST_GIVES_SIZE); its velocity; its
// colour; its size.
struct HostBirth {
    place: vec4<f32>,
    velocity: vec4<f32>,
    color: vec4<f32>,
    size: vec4<f32>,
}

fn hostBirthAt(h: u32) -> HostBirth {
    let at = 6u * params.counts.x + 4u * h;
    return HostBirth(bitcast<vec4<f32>>(births[at]), bitcast<vec4<f32>>(births[at + 1u]),
                     bitcast<vec4<f32>>(births[at + 2u]), bitcast<vec4<f32>>(births[at + 3u]));
}

const HOST_GIVES_COLOR: u32 = 1u;
const HOST_GIVES_SIZE: u32 = 2u;

// A birth written to place at of the other buffer; its place.
fn spawnAt(j: u32, at: u32) -> vec3<f32> {
    let c = params.counts.x;
    let b0 = births[3u * j];
    let b1 = births[3u * j + 1u];
    var moved_place = vec3<f32>(0.0);
    var moved_velocity = vec3<f32>(0.0);
    let fate = bornAt(j, &moved_place, &moved_velocity);
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    for (var k = 0u; k < 3u; k++) {
        moved[(params.words.place + k) * c + at] = bitcast<u32>(moved_place[k]);
        moved[(params.words.velocity + k) * c + at] = bitcast<u32>(moved_velocity[k]);
    }
    moved[params.words.serial * c + at] = b0.x;
    moved[params.words.birth * c + at] = birth.x;
    moved[(params.words.birth + 1u) * c + at] = birth.y;
    moved[params.words.death * c + at] = fate.death.x;
    moved[(params.words.death + 1u) * c + at] = fate.death.y;
    moved[params.words.inverseLife * c + at] = bitcast<u32>(fate.inverseLife);
    if (params.words.collisions != NONE) {
        moved[params.words.collisions * c + at] = fate.contacts;
    }
    storeValues(j, at);
    // Born at age 0, turned over the rest of the plan.
    let angular = factors(bitcast<f32>(params.turns.y), remainderOf(j)).xy;
    turn(1u, at, angular, lifeAt(LIFE_ROTATION, 0.0));
    return moved_place;
}

// The seconds from a birth of the plan to the plan's end.
fn remainderOf(j: u32) -> f32 {
    return secondsOf(vec2<u32>(params.counts.z - births[3u * j].z, 0u));
}

// A birth's drag scale, as the core draws it (1 when the emitter has
// none).
fn dragScaleOf(j: u32) -> f32 {
    if (params.words.drag == NONE) {
        return 1.0;
    }
    let b0 = births[3u * j];
    let age = bitcast<f32>(births[3u * j + 2u].x);
    return drawVector(SOURCE_DRAG, age, draw(DRAG_SALT, b0.x, b0.y)).x;
}

// A live particle's normalized age at the plan's start, as the core's:
// its seconds since birth times its inverse life, held to [0, 1].
fn ageAt(birth: vec2<u32>, inverseLife: f32) -> f32 {
    let age = secondsOf(subtract64(params.start.xy, birth)) * inverseLife;
    return select(min(age, 1.0), 0.0, !(age > 0.0));
}

// A value over life at a normalized age, as the core's mvfxLifeAt.
fn lifeAt(k: u32, age: f32) -> f32 {
    let life = params.lives[k / 2u];
    let value = bitcast<f32>(life[2u * (k % 2u)]);
    let first = life[2u * (k % 2u) + 1u];
    if (first == NONE) {
        return value;
    }
    return value * curveAt(first, age);
}

// A velocity held to the speed limit at a normalized age: past it, its
// speed falls toward it by decay, as the core's mvfxLimitSpeed.
fn limitSpeed(v: vec3<f32>, age: f32, decay: f32) -> vec3<f32> {
    if ((params.turns.z & LIMITS_SPEED) == 0u) {
        return v;
    }
    let limit = max(lifeAt(LIFE_SPEED_LIMIT, age), 0.0) * params.modules[0].w;
    let speed = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!(speed > limit)) {
        return v;
    }
    return v * ((limit + (speed - limit) * decay) / speed);
}

// A place moved over seconds from start by the velocity modules at a
// normalized age: the motion scaled, the added and radial speeds' moves,
// then the orbit, as the core's mvfxMoveByModules.
fn moveByModules(start: vec3<f32>, place: vec3<f32>, age: f32, h: f32) -> vec3<f32> {
    if ((params.turns.z & MODULES_MOVE) == 0u) {
        return place;
    }
    let sigma = params.modules[0].w;
    let scale = lifeAt(LIFE_MOTION_SCALE, age);
    let added = lifeAt(LIFE_ADDED_SPEED, age) * sigma * h;
    let radial = lifeAt(LIFE_RADIAL_SPEED, age) * sigma * h;
    let out = start - params.modules[3].xyz;
    let square = dot3(out, out);
    let away = square > 0.0 && square < LARGEST;
    let unit = out * inverseSqrt(square);
    var p = start + scale * (place - start) + added * params.modules[0].xyz;
    if (away) {
        p += radial * unit;
    }
    let angle = lifeAt(LIFE_ORBIT_RATE, age) * h;
    if (angle != 0.0) {
        let center = params.modules[2].xyz;
        let q = vec4<f32>(params.modules[1].xyz * sin(0.5 * angle), cos(0.5 * angle));
        p = center + rotate(q, p - center);
    }
    return p;
}

// A word of buffer (0 the current, 1 the other) at an index, and its
// write.
fn wordIn(buffer: u32, at: u32) -> u32 {
    if (buffer == 0u) {
        return source[at];
    }
    return moved[at];
}

fn setWordIn(buffer: u32, at: u32, value: u32) {
    if (buffer == 0u) {
        source[at] = value;
    } else {
        moved[at] = value;
    }
}

// Turns particle i of a buffer as the core's MoveAngular: its angle by
// its spin and its orientation by its angular velocity, over the angular
// factors (decay, carry), the carry scaled by the rotation speed; its
// spin and angular velocity decayed.
fn turn(buffer: u32, i: u32, f: vec2<f32>, scale: f32) {
    let c = params.counts.x;
    let w = params.words;
    if (w.angle != NONE) {
        let angle = bitcast<f32>(wordIn(buffer, w.angle * c + i));
        let spin = bitcast<f32>(wordIn(buffer, w.spin * c + i));
        setWordIn(buffer, w.angle * c + i, bitcast<u32>(angle + spin * (f.y * scale)));
        setWordIn(buffer, w.spin * c + i, bitcast<u32>(spin * f.x));
    }
    if (w.orientation != NONE) {
        var q = vec4<f32>(0.0);
        for (var k = 0u; k < 4u; k++) {
            q[k] = bitcast<f32>(wordIn(buffer, (w.orientation + k) * c + i));
        }
        let omega = readVector(buffer, i, w.angular);
        q = turnBy(q, omega * (f.y * scale));
        for (var k = 0u; k < 4u; k++) {
            setWordIn(buffer, (w.orientation + k) * c + i, bitcast<u32>(q[k]));
        }
        for (var k = 0u; k < 3u; k++) {
            setWordIn(buffer, (w.angular + k) * c + i, bitcast<u32>(omega[k] * f.x));
        }
    }
}

// A unit quaternion turned by a rotation vector (the turn's axis times
// its angle) and normalized, as the core's TurnBy.
fn turnBy(q: vec4<f32>, theta: vec3<f32>) -> vec4<f32> {
    let angle = sqrt(theta.x * theta.x + theta.y * theta.y + theta.z * theta.z);
    if (!(angle > 0.0)) {
        return q;
    }
    let s = sin(0.5 * angle) / angle;
    let r = vec4<f32>(theta * s, cos(0.5 * angle));
    let out = vec4<f32>(r.w * q.x + r.x * q.w + r.y * q.z - r.z * q.y,
                        r.w * q.y - r.x * q.z + r.y * q.w + r.z * q.x,
                        r.w * q.z + r.x * q.y - r.y * q.x + r.z * q.w,
                        r.w * q.w - r.x * q.x - r.y * q.y - r.z * q.z);
    let length = sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
    return out / length;
}

// Writes a value's components (those it has, from its first word) at
// place at of the other buffer; nothing for a value not stored.
fn storeValue(first: u32, components: u32, at: u32, value: vec4<f32>) {
    let c = params.counts.x;
    if (first == NONE) {
        return;
    }
    for (var k = 0u; k < components; k++) {
        moved[(first + k) * c + at] = bitcast<u32>(value[k]);
    }
}

// A birth's orientation from its words, as the core draws it: random, by
// Euler angles from its source (about X, then Y, then Z), or none.
fn orientationOf(age: f32, words: vec4<u32>) -> vec4<f32> {
    let mode = params.turns.x;
    if (mode == ORIENTATION_RANDOM) {
        let u = unitFloat(words.x);
        let low = sqrt(1.0 - u);
        let high = sqrt(u);
        let a1 = TWO_PI * unitFloat(words.y);
        let a2 = TWO_PI * unitFloat(words.z);
        return vec4<f32>(low * sin(a1), low * cos(a1), high * sin(a2), high * cos(a2));
    }
    if (mode == ORIENTATION_EULER) {
        let half = 0.5 * drawVector(SOURCE_ORIENTATION, age, words).xyz;
        let s = sin(half);
        let c = cos(half);
        return vec4<f32>(s.x * c.y * c.z - c.x * s.y * s.z, c.x * s.y * c.z + s.x * c.y * s.z,
                         c.x * c.y * s.z - s.x * s.y * c.z, c.x * c.y * c.z + s.x * s.y * s.z);
    }
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}

// A birth's drawn values, each the emitter stores, as the core's
// AppendValues and StoreWords draw them: size, colour, angle and spin,
// orientation and angular velocity, drag scale and custom values over
// the birth's source age, frame and mesh index.
fn storeValues(j: u32, at: u32) {
    let c = params.counts.x;
    let b0 = births[3u * j];
    let age = bitcast<f32>(births[3u * j + 2u].x);
    let w = params.words;
    if (w.size != NONE) {
        storeValue(w.size, 3u, at, vec4<f32>(bornSize(j), 0.0));
    }
    if (w.color != NONE) {
        storeValue(w.color, 4u, at, bornColor(j));
    }
    if (w.frame != NONE) {
        moved[w.frame * c + at] = drawIndex(params.draws[0], FRAME_SALT, b0.x, b0.y);
    }
    if (w.mesh != NONE) {
        moved[w.mesh * c + at] = drawIndex(params.draws[1], MESH_SALT, b0.x, b0.y);
    }
    if (w.angle != NONE) {
        moved[w.angle * c + at] =
            bitcast<u32>(drawVector(SOURCE_ANGLE, age, draw(ANGLE_SALT, b0.x, b0.y)).x);
        moved[w.spin * c + at] =
            bitcast<u32>(drawVector(SOURCE_SPIN, age, draw(SPIN_SALT, b0.x, b0.y)).x);
    }
    if (w.orientation != NONE) {
        storeValue(w.orientation, 4u, at, orientationOf(age, draw(ORIENTATION_SALT, b0.x, b0.y)));
        storeValue(w.angular, 3u, at, drawVector(SOURCE_ANGULAR, age, draw(ANGULAR_SALT, b0.x, b0.y)));
    }
    if (w.drag != NONE) {
        moved[w.drag * c + at] = bitcast<u32>(dragScaleOf(j));
    }
    for (var i = 0u; i < 8u; i++) {
        let first = w.custom[i / 4u][i % 4u];
        if (first != NONE) {
            let s = SOURCE_CUSTOM + i;
            let value = drawVector(s, age, draw(CUSTOM_SALT + i, b0.x, b0.y));
            storeValue(first, params.sources[s].modes.z, at, value);
        }
    }
}

// A birth's colour (ones where the emitter stores none): drawn, or a host
// particle's as it gives it.
fn bornColor(j: u32) -> vec4<f32> {
    if (params.words.color == NONE) {
        return vec4<f32>(1.0);
    }
    let host = births[3u * j + 2u].y;
    if (host != NONE) {
        let record = hostBirthAt(host);
        if ((bitcast<u32>(record.place.w) & HOST_GIVES_COLOR) != 0u) {
            return record.color;
        }
    }
    let b0 = births[3u * j];
    let age = bitcast<f32>(births[3u * j + 2u].x);
    return drawVector(SOURCE_COLOR, age, draw(COLOR_SALT, b0.x, b0.y));
}

// A birth's size (ones where the emitter stores none), as its colour.
fn bornSize(j: u32) -> vec3<f32> {
    if (params.words.size == NONE) {
        return vec3<f32>(1.0);
    }
    let host = births[3u * j + 2u].y;
    if (host != NONE) {
        let record = hostBirthAt(host);
        if ((bitcast<u32>(record.place.w) & HOST_GIVES_SIZE) != 0u) {
            return record.size.xyz;
        }
    }
    let b0 = births[3u * j];
    let age = bitcast<f32>(births[3u * j + 2u].x);
    return drawVector(SOURCE_SIZE, age, draw(SIZE_SALT, b0.x, b0.y)).xyz;
}

// Kill births: each birth with room killed on its way to the plan's end
// (a kill rule, an attractor's kill radius, a contact that kills, a
// contact's life loss that ends its life within the plan) loses its
// room's mark (2), so the scan keeps it not; it held its room for the
// step, as a killed particle does until the step's end. One whose
// contacts make an event is marked so too. The workgroups stride over
// the births as update's over the particles.
@compute @workgroup_size(64, 1, 1)
fn killBirths(@builtin(global_invocation_id) id: vec3<u32>) {
    if (params.forceHead.y == 0u && params.collisionHead.z == 0u && params.fateHead.x == 0u) {
        return;
    }
    for (var j = id.x; j < params.counts.y; j += params.modes.z * 64u) {
        if (accepted[j] != 1u) {
            continue;
        }
        var p = vec3<f32>(0.0);
        var v = vec3<f32>(0.0);
        let fate = bornAt(j, &p, &v);
        var mark = 1u;
        if (fate.killed || !outlives(fate.death.x, fate.death.y)) {
            mark = 2u;
        }
        // Its contacts make an event (BIRTH_CONTACT), for the events'
        // count.
        accepted[j] = mark | select(0u, BIRTH_CONTACT, fate.contacted);
    }
}

// A birth's mark beside its room's: its contacts make an event.
const BIRTH_CONTACT: u32 = 4u;

// The box around a box and a place, by comparisons that pass a NaN over.
fn lowerOf(low: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(low, p, p < low);
}

fn higherOf(high: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(high, p, p > high);
}

// The workgroup's box from each invocation's, by a fixed tree; whole in
// boxLow[0] and boxHigh[0], the largest growth in boxHigh[0].w.
fn reduceBox(local: u32, low: vec3<f32>, high: vec3<f32>, growth: f32) {
    boxLow[local] = vec4<f32>(low, 0.0);
    boxHigh[local] = vec4<f32>(high, growth);
    workgroupBarrier();
    for (var half = CHUNK / 2u; half > 0u; half >>= 1u) {
        if (local < half) {
            boxLow[local] = vec4<f32>(lowerOf(boxLow[local].xyz, boxLow[local + half].xyz), 0.0);
            boxHigh[local] = vec4<f32>(higherOf(boxHigh[local].xyz, boxHigh[local + half].xyz),
                                       max(boxHigh[local].w, boxHigh[local + half].w));
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
    var growth = 0.0;
    for (var k = 0u; k < ITEMS; k++) {
        let i = first + k;
        if (keep[k] == 1u && at < c) {
            var p = vec3<f32>(0.0);
            if (i < live) {
                for (var w = 0u; w < params.words.alive; w++) {
                    moved[w * c + at] = source[w * c + i];
                }
                p = readVector(0u, i, params.words.place);
            } else {
                p = spawnAt(i - live, at);
            }
            low = lowerOf(low, p);
            high = higherOf(high, p);
            growth = max(growth, growthOf(at));
        }
        at += keep[k];
    }
    reduceBox(local, low, high, growth);
    if (local == 0u) {
        let n = blockCount();
        let box = n + 6u * group.x;
        for (var k = 0u; k < 3u; k++) {
            blocks[box + k] = bitcast<u32>(boxLow[0][k]);
            blocks[box + 3u + k] = bitcast<u32>(boxHigh[0][k]);
        }
        blocks[7u * n + group.x] = bitcast<u32>(boxHigh[0].w);
    }
}

// How much a kept particle grows the bounds: its largest size component
// times the most the size over life scales that axis, 1 for a size it
// does not store, as the CPU path's bounds (the largest over particles of
// each axis's stored size is the largest product, the factors being
// positive).
fn growthOf(at: u32) -> f32 {
    var size = vec3<f32>(1.0);
    if (params.words.size != NONE) {
        size = abs(readVector(1u, at, params.words.size));
    }
    let g = size * params.growth.xyz;
    return max(g.x, max(g.y, g.z));
}

// Measure, in one workgroup: the blocks' boxes reduced by a fixed tree to
// the emitter's, in the particles' space, into the statistics record with
// the growth by the largest size, with its margin.
@compute @workgroup_size(256, 1, 1)
fn measure(@builtin(local_invocation_index) local: u32) {
    let n = blockCount();
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    var growth = 0.0;
    for (var g = local; g < n; g += CHUNK) {
        let box = n + 6u * g;
        growth = max(growth, bitcast<f32>(blocks[7u * n + g]));
        low = lowerOf(low, vec3<f32>(bitcast<f32>(blocks[box]), bitcast<f32>(blocks[box + 1u]),
                                     bitcast<f32>(blocks[box + 2u])));
        high = higherOf(high, vec3<f32>(bitcast<f32>(blocks[box + 3u]),
                                        bitcast<f32>(blocks[box + 4u]),
                                        bitcast<f32>(blocks[box + 5u])));
    }
    reduceBox(local, low, high, growth);
    if (local == 0u) {
        counts[3] = bitcast<u32>(boxHigh[0].w * params.growth.w);
        for (var k = 0u; k < 3u; k++) {
            counts[4u + k] = bitcast<u32>(boxLow[0][k]);
            counts[7u + k] = bitcast<u32>(boxHigh[0][k]);
        }
    }
}

// Whether a birth of the plan dies by tick t of the plan, its death in two
// words from the plan's start.
fn birthDiesBy(j: u32, t: u32) -> bool {
    let b0 = births[3u * j];
    let b1 = births[3u * j + 1u];
    return b1.x == 0u && b0.w <= t;
}

// Whether a live particle dies by tick t of the plan, its death absolute.
fn liveDiesBy(i: u32, t: u32) -> bool {
    let c = params.counts.x;
    let low = source[params.words.death * c + i];
    let high = source[(params.words.death + 1u) * c + i];
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
        let tick = births[3u * start].z;
        if (local == 0u) {
            // The batch's births in the plan: from its first on, its places
            // in order at one tick.
            var n = 1u;
            while (start + n < count && births[3u * (start + n) + 1u].z == n &&
                   births[3u * (start + n)].z == tick) {
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

// Events: what the step's particles did, written in the CPU path's order
// (ended deaths, the live particles' collisions, the births with their
// birth and collision events, the killed), at most the emitter's capacity
// of them, the rest dropped. Each element of the step's sequence (live
// particles, then births) counts its events in four groups; a scan of
// each group gives an event's place after the groups before it.
const EVENT_BIRTH: u32 = 0u;
const EVENT_DEATH: u32 = 1u;
const EVENT_COLLISION: u32 = 2u;
// An event's words: kind | killed << 8, serial, tick (two words), place
// (an offset from the plan's anchor after, in the world), velocity,
// normal, colour, size; then the particle's birth tick (two words),
// inverse life and speed in the particles' space, for the values over
// life the records kernel applies.
const EVENT_WORDS: u32 = 24u;

fn writes(kind: u32) -> bool {
    return (params.fateHead.y & (1u << kind)) != 0u;
}

// Where the blocks' counts of the four groups are in the events buffer.
fn eventBlocks() -> u32 {
    return EVENT_FACTS * params.counts.x;
}

// Whether birth j joins the particles: it had room and outlives the plan
// unmet (it may be killed on its way).
fn appended(j: u32) -> bool {
    let mark = accepted[j] & 3u;
    let b0 = births[3u * j];
    let b1 = births[3u * j + 1u];
    return (mark == 1u || mark == 2u) && (b1.x > 0u || b0.w > params.counts.z);
}

// Element e's events in each group, of live particles alive at the
// step's start: ended deaths; collisions; births and their collisions;
// kills.
fn eventCounts(e: u32, live: u32) -> vec4<u32> {
    var g = vec4<u32>(0u);
    let c = params.counts.x;
    if (e < live) {
        let flags = events[EVENT_FACTS * e];
        if ((flags & EVENT_ENDED) != 0u) {
            g.x = select(0u, 1u, writes(EVENT_DEATH));
            return g;
        }
        g.y = select(0u, 1u, writes(EVENT_COLLISION) && (flags & EVENT_CONTACT) != 0u);
        g.w = select(0u, 1u, writes(EVENT_DEATH) && source[params.words.alive * c + e] == 0u);
        return g;
    }
    let j = e - live;
    if (j >= params.counts.y || !appended(j)) {
        return g;
    }
    let mark = accepted[j];
    g.z = select(0u, 1u, writes(EVENT_BIRTH)) +
          select(0u, 1u, writes(EVENT_COLLISION) && (mark & BIRTH_CONTACT) != 0u);
    g.w = select(0u, 1u, writes(EVENT_DEATH) && (mark & 3u) == 2u);
    return g;
}

// Event count: each block's events in each group.
@compute @workgroup_size(256, 1, 1)
fn eventCount(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let live = counts[1];
    let first = group.x * BLOCK + local * ITEMS;
    var mine = vec4<u32>(0u);
    for (var k = 0u; k < ITEMS; k++) {
        mine += eventCounts(first + k, live);
    }
    for (var g = 0u; g < 4u; g++) {
        let before = exclusiveScan(local, mine[g]);
        if (local == CHUNK - 1u) {
            events[eventBlocks() + 4u * group.x + g] = before + mine[g];
        }
    }
}

// Event offsets, in one workgroup: each group's blocks' counts made each
// block's first place in the group, in order; the groups' first places
// (statistics words 30 to 33), the events written (word 28, at most the
// capacity) and those dropped (word 29).
@compute @workgroup_size(256, 1, 1)
fn eventOffsets(@builtin(local_invocation_index) local: u32) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let n = blockCount();
    var base = 0u;
    for (var g = 0u; g < 4u; g++) {
        if (local == 0u) {
            carried = 0u;
        }
        workgroupBarrier();
        for (var start = 0u; start < n; start += CHUNK) {
            let k = start + local;
            var count = 0u;
            if (k < n) {
                count = events[eventBlocks() + 4u * k + g];
            }
            let before = exclusiveScan(local, count);
            if (k < n) {
                events[eventBlocks() + 4u * k + g] = carried + before;
            }
            workgroupBarrier();
            if (local == CHUNK - 1u) {
                carried += before + count;
            }
            workgroupBarrier();
        }
        if (local == 0u) {
            counts[30u + g] = base;
        }
        base += workgroupUniformLoad(&carried);
    }
    if (local == 0u) {
        let written = min(base, params.fateHead.z);
        counts[28] = written;
        counts[29] = base - written;
    }
}

// A place, velocity or normal of the particles' space at a tick of the
// plan in the world, from the plan's anchor after: a world-space
// emitter's as they are, a local one's by the instance's place then.
fn worldPlace(p: vec3<f32>, tick: u32) -> vec3<f32> {
    if (params.start.w == 0u) {
        return p;
    }
    var origin = vec3<f32>(0.0);
    var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    let scale = placeAt(tick, &origin, &rotation);
    return origin + rotate(rotation, p * scale);
}

fn worldVector(v: vec3<f32>, tick: u32) -> vec3<f32> {
    if (params.start.w == 0u) {
        return v;
    }
    var origin = vec3<f32>(0.0);
    var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    let scale = placeAt(tick, &origin, &rotation);
    return rotate(rotation, v * scale);
}

// A plan's tick as the instance's (two words).
fn planTick(tick: u32) -> vec2<u32> {
    let low = params.start.x + tick;
    return vec2<u32>(low, params.start.y + select(0u, 1u, low < params.start.x));
}

// The tick of a contact at a share t of a move from a tick of the plan
// over ticks of it, rounded half up, as the CPU path's.
fn contactTick(start: u32, ticks: u32, t: f32) -> u32 {
    return start + u32(floor(t * f32(ticks) + 0.5));
}

// What an event says, in the particles' space, its ticks the plan's: when
// it happened, and the tick whose place of the instance maps it into the
// world (an ended death's the step's start).
struct Event {
    kind: u32,
    killed: bool,
    serial: u32,
    tick: u32,
    placeTick: u32,
    place: vec3<f32>,
    velocity: vec3<f32>,
    normal: vec3<f32>,
    color: vec4<f32>,
    size: vec3<f32>,
    birth: vec2<u32>,
    inverseLife: f32,
}

// Writes an event at its place, in the world, unless past the capacity.
fn writeEvent(at: u32, e: Event) {
    if (at >= params.fateHead.z) {
        return;
    }
    let w = params.fateHead.w + EVENT_WORDS * at;
    let tick = planTick(e.tick);
    let place = worldPlace(e.place, e.placeTick);
    let velocity = worldVector(e.velocity, e.placeTick);
    var normal = vec3<f32>(0.0);
    if (dot3(e.normal, e.normal) > 0.0) {
        normal = normalize(worldVector(e.normal, e.placeTick));
    }
    events[w] = e.kind | (select(0u, 1u, e.killed) << 8u);
    events[w + 1u] = e.serial;
    events[w + 2u] = tick.x;
    events[w + 3u] = tick.y;
    for (var k = 0u; k < 3u; k++) {
        events[w + 4u + k] = bitcast<u32>(place[k]);
        events[w + 7u + k] = bitcast<u32>(velocity[k]);
        events[w + 10u + k] = bitcast<u32>(normal[k]);
        events[w + 17u + k] = bitcast<u32>(e.size[k]);
    }
    for (var k = 0u; k < 4u; k++) {
        events[w + 13u + k] = bitcast<u32>(e.color[k]);
    }
    let v = e.velocity;
    events[w + 20u] = e.birth.x;
    events[w + 21u] = e.birth.y;
    events[w + 22u] = bitcast<u32>(e.inverseLife);
    events[w + 23u] = bitcast<u32>(sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
}

// A live particle's stored vector of n components, ones where the
// emitter stores none.
fn liveStored(first: u32, i: u32, n: u32) -> vec4<f32> {
    var out = vec4<f32>(1.0);
    for (var k = 0u; first != NONE && k < n; k++) {
        out[k] = bitcast<f32>(source[(first + k) * params.counts.x + i]);
    }
    return out;
}

// Live particle i's event of a kind, its place and velocity as given.
fn liveEvent(i: u32, kind: u32, tick: u32, place: vec3<f32>, velocity: vec3<f32>) -> Event {
    let c = params.counts.x;
    let w = params.words;
    return Event(kind, false, source[w.serial * c + i], tick, tick, place, velocity, vec3<f32>(0.0),
                 liveStored(w.color, i, 4u), liveStored(w.size, i, 3u).xyz,
                 vec2<u32>(source[w.birth * c + i], source[(w.birth + 1u) * c + i]),
                 bitcast<f32>(source[w.inverseLife * c + i]));
}

// Live particle i's events of its groups, at their places.
fn writeLiveEvents(i: u32, g: vec4<u32>, at: ptr<function, vec4<u32>>) {
    let c = params.counts.x;
    let w = params.words;
    let place = readVector(0u, i, w.place);
    let velocity = readVector(0u, i, w.velocity);
    if (g.x > 0u) {
        // Ended: where the step found it, at its death tick.
        var e = liveEvent(i, EVENT_DEATH, 0u, place + params.shift.xyz, velocity);
        e.tick = source[w.death * c + i] - params.start.x;
        writeEvent((*at).x, e);
        (*at).x += 1u;
        return;
    }
    if (g.y > 0u) {
        let facts = EVENT_FACTS * i;
        let t = bitcast<f32>(events[facts + 1u]);
        var point = vec3<f32>(0.0);
        var normal = vec3<f32>(0.0);
        for (var k = 0u; k < 3u; k++) {
            point[k] = bitcast<f32>(events[facts + 2u + k]);
            normal[k] = bitcast<f32>(events[facts + 5u + k]);
        }
        var e = liveEvent(i, EVENT_COLLISION, contactTick(0u, params.counts.z, t), point, velocity);
        e.normal = normal;
        e.inverseLife = bitcast<f32>(events[facts + 8u]);
        writeEvent((*at).y, e);
        (*at).y += 1u;
    }
    if (g.w > 0u) {
        var e = liveEvent(i, EVENT_DEATH, params.counts.z, place, velocity);
        e.killed = true;
        writeEvent((*at).w, e);
        (*at).w += 1u;
    }
}

// Birth j's events of its groups, at their places.
fn writeBirthEvents(j: u32, g: vec4<u32>, at: ptr<function, vec4<u32>>) {
    let b0 = births[3u * j];
    let b1 = births[3u * j + 1u];
    var born = vec3<f32>(0.0);
    var bornVelocity = vec3<f32>(0.0);
    var place = vec3<f32>(0.0);
    var velocity = vec3<f32>(0.0);
    let fate = bornFacts(j, &born, &bornVelocity, &place, &velocity);
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    var e = Event(EVENT_BIRTH, false, b0.x, b0.z, b0.z, born, bornVelocity, vec3<f32>(0.0),
                  bornColor(j), bornSize(j), birth, bitcast<f32>(b1.y));
    if (writes(EVENT_BIRTH)) {
        writeEvent((*at).z, e);
        (*at).z += 1u;
    }
    if (writes(EVENT_COLLISION) && (accepted[j] & BIRTH_CONTACT) != 0u) {
        e.kind = EVENT_COLLISION;
        e.tick = contactTick(b0.z, params.counts.z - b0.z, fate.first.t);
        e.placeTick = e.tick;
        e.place = fate.first.point;
        e.velocity = velocity;
        e.normal = fate.first.normal;
        writeEvent((*at).z, e);
        (*at).z += 1u;
    }
    if (g.w > 0u) {
        e.kind = EVENT_DEATH;
        e.killed = true;
        e.tick = params.counts.z;
        e.placeTick = params.counts.z;
        e.place = place;
        e.velocity = velocity;
        e.normal = vec3<f32>(0.0);
        e.inverseLife = fate.inverseLife;
        writeEvent((*at).w, e);
        (*at).w += 1u;
    }
}

// Event write: each block's elements' events at their places.
@compute @workgroup_size(256, 1, 1)
fn eventWrite(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let live = counts[1];
    let first = group.x * BLOCK + local * ITEMS;
    var mine: array<vec4<u32>, ITEMS>;
    var sum = vec4<u32>(0u);
    for (var k = 0u; k < ITEMS; k++) {
        mine[k] = eventCounts(first + k, live);
        sum += mine[k];
    }
    var at = vec4<u32>(0u);
    for (var g = 0u; g < 4u; g++) {
        at[g] = counts[30u + g] + events[eventBlocks() + 4u * group.x + g] +
                exclusiveScan(local, sum[g]);
    }
    for (var k = 0u; k < ITEMS; k++) {
        let e = first + k;
        let g = mine[k];
        if (g.x + g.y + g.z + g.w == 0u) {
            continue;
        }
        if (e < live) {
            writeLiveEvents(e, g, &at);
        } else {
            writeBirthEvents(e - live, g, &at);
        }
    }
}
