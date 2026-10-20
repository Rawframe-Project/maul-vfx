// The sim kernel's draws and forces (spliced into sim.wgsl): the draws at
// a serial (shared/pcg4d_hash.wgsl), the modules' forces and fields, the
// kill rules.

// include: shared/pcg4d_hash.wgsl

const SOURCE_SIZE: u32 = 0u;
const SOURCE_COLOR: u32 = 1u;
const SOURCE_ANGLE: u32 = 2u;
const SOURCE_SPIN: u32 = 3u;
const SOURCE_ORIENTATION: u32 = 4u;
const SOURCE_ANGULAR: u32 = 5u;
const SOURCE_DRAG: u32 = 6u;
const SOURCE_CUSTOM: u32 = 7u;
const SOURCE_LIFE: u32 = 15u;
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

// include: shared/curve.wgsl

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

// The key birth j draws its values by: an event birth's, set in its
// record's last words (the top bit of its high word set), else its serial.
fn birthKey(j: u32) -> vec2<u32> {
    let b2 = birthWord(j, 2u);
    return select(birthWord(j, 0u).xy, b2.zw, b2.w != 0u);
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

// Force n's field among the step's numbers (shared/forces.wgsl).
fn fieldOf(n: u32) -> Field {
    return params.fields[n];
}

// include: shared/forces.wgsl

// The forces at x, seconds after the plan's start: gravity and their
// accelerations in order, and the drag the winds and velocity fields add
// (the emitter's own added after, as the core sums them).
fn forcesAt(x: vec3<f32>, seconds: f32, a: ptr<function, vec3<f32>>, drag: ptr<function, f32>) {
    *a = params.gravity.xyz;
    *drag = 0.0;
    for (var n = 0u; n < params.forceHead.x; n++) {
        let f = params.forces[n];
        let weight = falloffAt(f, x);
        if (weight > 0.0) {
            forceAt(n, f, x, seconds, weight, a, drag);
        }
    }
}

// The air's velocity at x, seconds after the plan's start, as the core's
// AirAt: the pull of the forces that drag over their drag, 0 where none
// reaches.
fn airAt(x: vec3<f32>, seconds: f32) -> vec3<f32> {
    var pull = vec3<f32>(0.0);
    var drag = 0.0;
    for (var n = 0u; n < params.forceHead.x; n++) {
        let f = params.forces[n];
        if (!forceDrags(f)) {
            continue;
        }
        let weight = falloffAt(f, x);
        if (weight > 0.0) {
            forceAt(n, f, x, seconds, weight, &pull, &drag);
        }
    }
    var out = vec3<f32>(0.0);
    for (var k = 0u; drag > 0.0 && k < 3u; k++) {
        out[k] = quotient(pull[k], drag);
    }
    return out;
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

const MAX_CONTACTS: u32 = 4u;

// include: shared/colliders.wgsl
