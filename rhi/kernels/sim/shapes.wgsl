// The sim kernel's spawn shapes (spliced into sim.wgsl): each shape's
// sample, the arc fraction it reads and the initial velocity, as the CPU
// path's src/shape.c and src/velocity.c write them (research 29).

const SHAPE_LINE: u32 = 1u;
const SHAPE_CIRCLE: u32 = 2u;
const SHAPE_SPHERE: u32 = 3u;
const SHAPE_HEMISPHERE: u32 = 4u;
const SHAPE_BOX: u32 = 5u;
const SHAPE_CONE: u32 = 6u;
const ARC_LOOP: u32 = 1u;
const ARC_PING_PONG: u32 = 2u;
const ARC_BURST_SPREAD: u32 = 3u;
const DIRECTION_FROM_CENTER: u32 = 1u;
const DIRECTION_CONE: u32 = 2u;
const DIRECTION_FIXED: u32 = 3u;

// a times b, 64 bits from 32-bit halves.
fn product32(a: u32, b: u32) -> vec2<u32> {
    let a0 = a & 0xFFFFu;
    let a1 = a >> 16u;
    let b0 = b & 0xFFFFu;
    let b1 = b >> 16u;
    let p00 = a0 * b0;
    let p01 = a0 * b1;
    let p10 = a1 * b0;
    let mid = (p00 >> 16u) + (p01 & 0xFFFFu) + (p10 & 0xFFFFu);
    return vec2<u32>((p00 & 0xFFFFu) | (mid << 16u),
                     a1 * b1 + (p01 >> 16u) + (p10 >> 16u) + (mid >> 16u));
}

// The low k bits of a word (all of them from 32).
fn lowBits(word: u32, k: i32) -> u32 {
    if (k >= 32) {
        return word;
    }
    if (k <= 0) {
        return 0u;
    }
    return word & ((1u << u32(k)) - 1u);
}

// The fraction of ticks times speed over 2^20 (times a half where halved):
// speed is m 2^e, so the turns are ticks m 2^-k, k = 20 - e, whose
// fraction is the product's low k bits over 2^k, the 88-bit product exact;
// within a few ulps of the CPU path's binary64 (research 29).
fn turnsFraction(ticks: vec2<u32>, speed: f32, halved: bool) -> f32 {
    let bits = bitcast<u32>(speed);
    let biased = i32((bits >> 23u) & 0xFFu);
    var m = bits & 0x007FFFFFu;
    var e = -149;
    if (biased != 0) {
        m |= 0x00800000u;
        e = biased - 150;
    }
    if (m == 0u) {
        return 0.0;
    }
    let k = 20 - e + select(0, 1, halved);
    let low = product32(ticks.x, m);
    let high = product32(ticks.y, m);
    let w1 = low.y + high.x;
    let w2 = high.y + select(0u, 1u, w1 < low.y);
    let value = f32(lowBits(w2, k - 64)) * 18446744073709551616.0 +
                f32(lowBits(w1, k - 32)) * 4294967296.0 + f32(lowBits(low.x, k));
    return ldexp(value, -k);
}

// The arc fraction birth j's shape reads: by its age past the emitter's
// delay looping or going and coming at the arc speed, by its place in its
// batch spread over the arc, or drawn.
fn arcFraction(j: u32, words: vec4<u32>) -> f32 {
    let mode = params.shapeModes.x;
    if (mode == ARC_LOOP || mode == ARC_PING_PONG) {
        let tickLow = params.start.x + births[3u * j].z;
        let tick = vec2<u32>(tickLow, params.start.y + select(0u, 1u, tickLow < params.start.x));
        let delay = params.shapeModes.zw;
        var age = vec2<u32>(0u);
        if (!less64(tick, delay)) {
            age = subtract64(tick, delay);
        }
        if (mode == ARC_LOOP) {
            return turnsFraction(age, params.shapeArc.y, false);
        }
        let there = 2.0 * turnsFraction(age, params.shapeArc.y, true);
        return select(2.0 - there, there, there <= 1.0);
    }
    if (mode == ARC_BURST_SPREAD) {
        let batch = births[3u * j + 1u];
        return select(0.0, ratio(vec2<u32>(batch.z, 0u), vec2<u32>(batch.w, 0u)), batch.w > 1u);
    }
    return unitFloat(words.w);
}

// The radius of a point uniform by area over the ring between (1 -
// thickness) radius and radius.
fn ringRadius(radius: f32, thickness: f32, u: f32) -> f32 {
    let inner = radius * (1.0 - thickness);
    let innerSquare = inner * inner;
    return sqrt(innerSquare + u * (radius * radius - innerSquare));
}

// v made unit as the CPU path's mvfxNormalize makes it, false (v as it
// was) for a zero or unbounded v: scaled by its largest component first,
// so a v whose square underflows binary32 still turns unit.
fn normalized(v: ptr<function, vec3<f32>>) -> bool {
    let largest = max(abs((*v).x), max(abs((*v).y), abs((*v).z)));
    if (!(largest > 0.0) || largest > 3.4028234e38) {
        return false;
    }
    let u = *v / largest;
    *v = u / sqrt(dot3(u, u));
    return true;
}

fn sampleSphere(words: vec4<u32>, hemisphere: bool, normal: ptr<function, vec3<f32>>) -> vec3<f32> {
    *normal = randomDirection(words.x, words.y);
    if (hemisphere) {
        (*normal).z = abs((*normal).z);
    }
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

// A box's volume, uniformly, the normal along its nearest face.
fn sampleBoxVolume(words: vec4<u32>, normal: ptr<function, vec3<f32>>) -> vec3<f32> {
    let h = params.shapeSize.yzw;
    let draws = words.xyz;
    var position = vec3<f32>(0.0);
    var best = -1.0;
    var axis = 2u;
    for (var i = 0u; i < 3u; i++) {
        position[i] = (2.0 * unitFloat(draws[i]) - 1.0) * h[i];
        let reach = select(0.0, abs(position[i]) / h[i], h[i] > 0.0);
        if (reach > best) {
            best = reach;
            axis = i;
        }
    }
    *normal = vec3<f32>(0.0);
    (*normal)[axis] = select(1.0, -1.0, position[axis] < 0.0);
    return position;
}

// A box's surface or shell: one of six slabs chosen by volume (by area on
// the surface), the normal along its axis; the slabs along Z span X and
// Y, those along Y X and the inner Z, those along X the inner Y and Z.
fn sampleBoxShell(words: vec4<u32>, normal: ptr<function, vec3<f32>>) -> vec3<f32> {
    let h = params.shapeSize.yzw;
    let t = params.shapeExtra.x;
    let inner = h * (1.0 - t);
    var depth = vec3<f32>(1.0);
    if (t > 0.0) {
        depth = h - inner;
    }
    let weights = vec3<f32>(inner.y * inner.z * depth.x, h.x * inner.z * depth.y,
                            h.x * h.y * depth.z);
    let total = weights.x + weights.y + weights.z;
    if (!(total > 0.0)) {
        *normal = randomDirection(words.x, words.y);
        return vec3<f32>(0.0);
    }
    // The slab a draw chooses; should the product round onto the total,
    // the heaviest axis rather than an empty one.
    let pick = unitFloat(words.x) * total;
    var axis = select(select(2u, 1u, pick < weights.x + weights.y), 0u, pick < weights.x);
    if (!(weights[axis] > 0.0)) {
        let heaviest = select(0u, 1u, weights.y > weights.x);
        axis = select(heaviest, 2u, weights.z > weights[heaviest]);
    }
    let spans = vec3<f32>(select(h.x, inner.x, axis == 0u), select(inner.y, h.y, axis == 2u),
                          inner.z);
    let side = select(1.0, -1.0, (words.x & 1u) != 0u);
    let free = vec2<u32>(words.y, words.z);
    var used = 0u;
    var position = vec3<f32>(0.0);
    for (var i = 0u; i < 3u; i++) {
        if (i == axis) {
            position[i] = side * (inner[i] + unitFloat(words.w) * (h[i] - inner[i]));
        } else {
            position[i] = (2.0 * unitFloat(free[used]) - 1.0) * spans[i];
            used += 1u;
        }
    }
    *normal = vec3<f32>(0.0);
    (*normal)[axis] = side;
    return position;
}

// A cone on its base or through its volume (a height by the cube of its
// radius there), a ring there; the direction leans from +Z along its slant,
// or from an apex uniformly within its angle.
fn sampleCone(words: vec4<u32>, arc: f32, normal: ptr<function, vec3<f32>>) -> vec3<f32> {
    let angle = arc * params.shapeArc.x;
    let sine = sin(angle);
    let cosine = cos(angle);
    let base = params.shapeOffset.w;
    let length = params.shapeSize.x;
    let k = params.shapeArc.z;
    var height = 0.0;
    var radius = base;
    if (params.shapeModes.y != 0u) {
        let u = unitFloat(words.z);
        if (k > 0.0 && length > 0.0) {
            let top = base + k * length;
            let baseCube = base * base * base;
            let topCube = top * top * top;
            let reached = pow(baseCube + u * (topCube - baseCube), 1.0 / 3.0);
            height = max(min((reached - base) / k, length), 0.0);
        } else {
            height = u * length;
        }
        radius = base + k * height;
    }
    let ring = ringRadius(radius, params.shapeExtra.x, unitFloat(words.x));
    if (radius > 0.0) {
        let lean = ring / radius * k;
        var n = vec3<f32>(lean * cosine, lean * sine, 1.0);
        _ = normalized(&n);
        *normal = n;
    } else {
        let capCosine = 1.0 / sqrt(1.0 + k * k);
        let z = 1.0 - unitFloat(words.y) * (1.0 - capCosine);
        let across = sqrt(max(0.0, 1.0 - z * z));
        *normal = vec3<f32>(across * cosine, across * sine, z);
    }
    return vec3<f32>(ring * cosine, ring * sine, height);
}

// A shape's sample for birth j from its shape draw: its place in the
// emitter's space before the offset (local), turned by the shape's
// rotation, and its normal.
fn sampleShape(j: u32, words: vec4<u32>, normal: ptr<function, vec3<f32>>) -> vec3<f32> {
    var position = vec3<f32>(0.0);
    var n = vec3<f32>(0.0, 0.0, 1.0);
    let kind = params.modes.y;
    if (kind == SHAPE_LINE) {
        position.x = (arcFraction(j, words) - 0.5) * params.shapeSize.x;
        n = vec3<f32>(0.0, 1.0, 0.0);
    } else if (kind == SHAPE_CIRCLE) {
        let angle = arcFraction(j, words) * params.shapeArc.x;
        let sine = sin(angle);
        let cosine = cos(angle);
        let ring = ringRadius(params.shapeOffset.w, params.shapeExtra.x, unitFloat(words.x));
        position = vec3<f32>(ring * cosine, ring * sine, 0.0);
        n = vec3<f32>(cosine, sine, 0.0);
    } else if (kind == SHAPE_SPHERE || kind == SHAPE_HEMISPHERE) {
        position = sampleSphere(words, kind == SHAPE_HEMISPHERE, &n);
    } else if (kind == SHAPE_BOX) {
        if (params.shapeExtra.x == 1.0) {
            position = sampleBoxVolume(words, &n);
        } else {
            position = sampleBoxShell(words, &n);
        }
    } else if (kind == SHAPE_CONE) {
        position = sampleCone(words, arcFraction(j, words), &n);
    } else {
        n = randomDirection(words.x, words.y);
    }
    *normal = turnByShape(n);
    return turnByShape(position);
}

fn turnByShape(v: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(dot(params.shapeRow0.xyz, v), dot(params.shapeRow1.xyz, v),
                     dot(params.shapeRow2.xyz, v));
}

// The initial velocity in the emitter's space from a sample's local place
// and normal: from the center, in the cone, fixed, or along the normal;
// blended toward a random direction by the randomize amount.
fn drawVelocity(words: vec4<u32>, local: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
    var direction = normal;
    let mode = params.modes.x;
    if (mode == DIRECTION_FROM_CENTER) {
        direction = local;
        if (!normalized(&direction)) {
            direction = randomDirection(words.x, words.y);
        }
    } else if (mode == DIRECTION_CONE) {
        let z = 1.0 - unitFloat(words.x) * (1.0 - params.direction.w);
        let across = sqrt(max(0.0, 1.0 - z * z));
        let angle = TWO_PI * unitFloat(words.y);
        direction = across * cos(angle) * params.tangentA.xyz +
                    across * sin(angle) * params.tangentB.xyz + z * params.direction.xyz;
    } else if (mode == DIRECTION_FIXED) {
        direction = params.direction.xyz;
    }
    let randomize = params.tangentA.w;
    if (randomize > 0.0) {
        let random = randomDirection(words.z, words.w);
        direction += (random - direction) * randomize;
        if (!normalized(&direction)) {
            direction = random;
        }
    }
    // The speed from the low byte of every word, apart from the bits the
    // direction's draws read.
    let speedWord = ((words.x & 0xFFu) << 24u) | ((words.y & 0xFFu) << 16u) |
                    ((words.z & 0xFFu) << 8u) | (words.w & 0xFFu);
    let speed = params.tangentB.w + (params.speed.x - params.tangentB.w) * unitFloat(speedWord);
    return direction * speed;
}
