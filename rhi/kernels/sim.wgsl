// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's step on the GPU: update, room, then the survivors and the
// births kept in order by a scan in three passes (reduce, offsets,
// scatter), and their box measured. Particles are arrays of the capacity in one buffer of words
// (WORDS a particle): place (3), velocity (3), serial (2), birth tick (2),
// death tick (2), inverse life, and whether it outlives the step.

const WORDS: u32 = 14u;
const PLACE: u32 = 0u;
const VELOCITY: u32 = 3u;
const SERIAL: u32 = 6u;
const BIRTH: u32 = 8u;
const DEATH: u32 = 10u;
const INVERSE_LIFE: u32 = 12u;
const ALIVE: u32 = 13u;
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
}

@group(0) @binding(0) var<uniform> params: Step;
@group(0) @binding(1) var<storage, read> births: array<vec4<u32>>;
@group(0) @binding(2) var<storage, read_write> source: array<u32>;
@group(0) @binding(3) var<storage, read_write> moved: array<u32>;
// The statistics record (rhi/src/record.h): counts, the growth, the box.
@group(0) @binding(4) var<storage, read_write> counts: array<u32>;
// Whether each of the plan's births has room, as the room entry decides.
@group(0) @binding(5) var<storage, read_write> accepted: array<u32>;
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

// The forces at x: gravity and their accelerations in order, and the
// emitter's drag and what the winds add.
fn forcesAt(x: vec3<f32>, a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) {
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
            default: {
                *a += swirl(f, x, weight);
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
    forcesAt(start, &a, &drag);
    let f = factorsFor(drag, f32(params.counts.z) / TICKS_PER_SECOND, true);
    let v0 = readVector(0u, i, VELOCITY);
    let p = start + v0 * f.y + a * f.z;
    let v = v0 * f.x + a * f.y;
    for (var k = 0u; k < 3u; k++) {
        source[(PLACE + k) * c + i] = bitcast<u32>(p[k]);
        source[(VELOCITY + k) * c + i] = bitcast<u32>(v[k]);
    }
    source[ALIVE * c + i] = select(0u, 1u, outlives(source[DEATH * c + i],
                                                     source[(DEATH + 1u) * c + i]) &&
                                               !killed(start, p));
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

// A birth of the plan drawn, placed, and moved from its tick to the plan's
// end under its forces at its birth place: its place and velocity then;
// whether an attractor kills it on the way.
fn bornAt(j: u32, place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>) -> bool {
    let b0 = births[2u * j];
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
    forcesAt(p, &a, &drag);
    let f = factorsFor(drag, f32(params.counts.z - b0.z) / TICKS_PER_SECOND, false);
    *place = p + v * f.y + a * f.z;
    *velocity = v * f.x + a * f.y;
    return killed(p, *place);
}

// A birth written to place at of the other buffer; its place.
fn spawnAt(j: u32, at: u32) -> vec3<f32> {
    let c = params.counts.x;
    let b0 = births[2u * j];
    let b1 = births[2u * j + 1u];
    var moved_place = vec3<f32>(0.0);
    var moved_velocity = vec3<f32>(0.0);
    _ = bornAt(j, &moved_place, &moved_velocity);
    // The birth and death ticks made absolute.
    let birthLow = params.start.x + b0.z;
    let birthHigh = params.start.y + select(0u, 1u, birthLow < params.start.x);
    let deathLow = params.start.x + b0.w;
    let deathHigh = params.start.y + b1.x + select(0u, 1u, deathLow < params.start.x);
    for (var k = 0u; k < 3u; k++) {
        moved[(PLACE + k) * c + at] = bitcast<u32>(moved_place[k]);
        moved[(VELOCITY + k) * c + at] = bitcast<u32>(moved_velocity[k]);
    }
    moved[SERIAL * c + at] = b0.x;
    moved[(SERIAL + 1u) * c + at] = b0.y;
    moved[BIRTH * c + at] = birthLow;
    moved[(BIRTH + 1u) * c + at] = birthHigh;
    moved[DEATH * c + at] = deathLow;
    moved[(DEATH + 1u) * c + at] = deathHigh;
    moved[INVERSE_LIFE * c + at] = b1.y;
    return moved_place;
}

// Kill births: each birth with room an attractor kills on its way to the
// plan's end loses its room's mark (2), so the scan keeps it not; it
// held its room for the step, as a killed particle does until the step's
// end. The workgroups stride over the births as update's over the
// particles.
@compute @workgroup_size(64, 1, 1)
fn killBirths(@builtin(global_invocation_id) id: vec3<u32>) {
    if (params.forceHead.y == 0u) {
        return;
    }
    for (var j = id.x; j < params.counts.y; j += params.modes.z * 64u) {
        var p = vec3<f32>(0.0);
        var v = vec3<f32>(0.0);
        if (accepted[j] == 1u && bornAt(j, &p, &v)) {
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
