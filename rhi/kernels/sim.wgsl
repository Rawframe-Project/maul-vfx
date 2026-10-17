// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's step on the GPU: update, room, compact, spawn. Particles are
// arrays of the capacity in one buffer of words (WORDS a particle):
// place (3), velocity (3), serial (2), birth tick (2), death tick (2),
// inverse life, and whether it outlives the step.

const WORDS: u32 = 14u;
const PLACE: u32 = 0u;
const VELOCITY: u32 = 3u;
const SERIAL: u32 = 6u;
const BIRTH: u32 = 8u;
const DEATH: u32 = 10u;
const INVERSE_LIFE: u32 = 12u;
const ALIVE: u32 = 13u;
const CHUNK: u32 = 256u;
const TICKS_PER_SECOND: f32 = 1048576.0;
const TWO_PI: f32 = 6.28318530717958647692;

const SHAPE_SALT: u32 = 0x53484150u;
const VELOCITY_SALT: u32 = 0x56454C4Fu;

struct Step {
    // capacity, births, the plan's ticks, the instance's seed
    counts: vec4<u32>,
    // the plan's start (two words), the emitter's index, its space (0 for
    // the world)
    start: vec4<u32>,
    // the velocity's mode, the shape's kind, unused (2)
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
}

@group(0) @binding(0) var<uniform> params: Step;
@group(0) @binding(1) var<storage, read> births: array<vec4<u32>>;
@group(0) @binding(2) var<storage, read_write> source: array<u32>;
@group(0) @binding(3) var<storage, read_write> moved: array<u32>;
@group(0) @binding(4) var<storage, read_write> counts: array<u32>;
// Whether each of the plan's births has room, as the room entry decides.
@group(0) @binding(5) var<storage, read_write> accepted: array<u32>;

var<workgroup> scan: array<u32, CHUNK>;
var<workgroup> carried: u32;
var<workgroup> liveCount: u32;
var<workgroup> dead: u32;
var<workgroup> batchStart: u32;
var<workgroup> batchLength: u32;
var<workgroup> taken: u32;

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
fn factors(seconds: f32) -> vec3<f32> {
    let drag = params.factors.w;
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

// Update: each live particle shifted with the anchor and moved over the
// plan; whether it outlives the plan.
@compute @workgroup_size(64, 1, 1)
fn update(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    let c = params.counts.x;
    if (i >= counts[0]) {
        return;
    }
    let a = params.gravity.xyz;
    let f = params.factors.xyz;
    let v0 = readVector(0u, i, VELOCITY);
    let p = readVector(0u, i, PLACE) + params.shift.xyz + v0 * f.y + a * f.z;
    let v = v0 * f.x + a * f.y;
    for (var k = 0u; k < 3u; k++) {
        source[(PLACE + k) * c + i] = bitcast<u32>(p[k]);
        source[(VELOCITY + k) * c + i] = bitcast<u32>(v[k]);
    }
    source[ALIVE * c + i] = select(0u, 1u, outlives(source[DEATH * c + i],
                                                     source[(DEATH + 1u) * c + i]));
}

// Adds keep's flags of the workgroup in order: each invocation's place
// among those kept before it in this chunk, the chunk's count in carried's
// next value.
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

// Compact: the particles that outlive the plan moved, in order, to the
// other buffer; their count in counts[1].
@compute @workgroup_size(256, 1, 1)
fn compact(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    if (local == 0u) {
        carried = 0u;
        liveCount = counts[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    for (var base = 0u; base < live; base += CHUNK) {
        let i = base + local;
        var keep = 0u;
        if (i < live) {
            keep = source[ALIVE * c + i];
        }
        let before = exclusiveScan(local, keep);
        let at = carried + before;
        if (keep == 1u) {
            for (var w = 0u; w < ALIVE; w++) {
                moved[w * c + at] = source[w * c + i];
            }
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried += before + keep;
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        counts[1] = carried;
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

// Spawn: the plan's births that outlive it, from counts[1] on in order,
// each drawn, placed, and moved from its tick to the plan's end; the new
// count in counts[0], refusing past the capacity.
@compute @workgroup_size(256, 1, 1)
fn spawn(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    let count = params.counts.y;
    if (local == 0u) {
        carried = counts[1];
    }
    workgroupBarrier();
    for (var base = 0u; base < count; base += CHUNK) {
        let i = base + local;
        var keep = 0u;
        var b0 = vec4<u32>(0u);
        var b1 = vec4<u32>(0u);
        if (i < count) {
            b0 = births[2u * i];
            b1 = births[2u * i + 1u];
            // The death from the plan's start in two words: past the plan's
            // ticks when its high word is set or its low passes them.
            if (accepted[i] == 1u && (b1.x > 0u || b0.w > params.counts.z)) {
                keep = 1u;
            }
        }
        let before = exclusiveScan(local, keep);
        let at = carried + before;
        if (keep == 1u && at < c) {
            var normal = vec3<f32>(0.0);
            let shapeWords = draw(SHAPE_SALT, b0.x, b0.y);
            let local_place = turnByShape(sampleSphere(shapeWords, &normal));
            let place = local_place + params.shapeOffset.xyz;
            var velocity = drawVelocity(draw(VELOCITY_SALT, b0.x, b0.y), turnByShape(normal));
            var p = place;
            if (params.start.w == 0u) {
                var origin = vec3<f32>(0.0);
                var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
                let scale = placeAt(b0.z, &origin, &rotation);
                p = origin + rotate(rotation, place * scale);
                velocity = rotate(rotation, velocity * scale) +
                           params.shift.w * params.instanceVelocity.xyz;
            }
            let seconds = f32(params.counts.z - b0.z) / TICKS_PER_SECOND;
            let f = factors(seconds);
            let a = params.gravity.xyz;
            let moved_place = p + velocity * f.y + a * f.z;
            let moved_velocity = velocity * f.x + a * f.y;
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
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried += before + keep;
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        counts[0] = min(carried, c);
        counts[2] = carried - min(carried, c);
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
