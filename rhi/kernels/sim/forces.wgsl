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
        if (weight > 0.0) {
            forceAt(n, f, x, seconds, weight, a, drag);
        }
    }
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
