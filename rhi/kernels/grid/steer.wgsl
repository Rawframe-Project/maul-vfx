// The grid kernel's steering (spliced into grid.wgsl), as the core's
// src/steer.c and the flocking and pair sums of src/neighbor.c, each
// operation in the core's order: flocking from the neighbors the query
// counts (separation, alignment and cohesion within their radii and the
// view cone), seek, flee and path following toward targets the plan
// resolves, the sum held to the most force, and the pair force's pull,
// not held, added over the plan's seconds. WGSL's sqrt and division are
// not correctly rounded, so steered velocities differ from the core's
// within its rounding.

// Whether steering flocks (rhi/src/numbers.h's MVFX_STEER_FLOCK).
const STEER_FLOCK: u32 = 2u;

// A vector's length, its squares summed in order.
fn lengthOf(v: vec3<f32>) -> f32 {
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Whether a neighbor an offset d away (its squared length square) lies in
// the view about a heading; a particle at rest sees all.
fn sees(d: vec3<f32>, square: f32, h: vec3<f32>) -> bool {
    let heading = h.x * h.x + h.y * h.y + h.z * h.z;
    let cosine = params.steerScale.w;
    if (cosine <= -1.0) {
        return true;
    }
    let along = d.x * h.x + d.y * h.y + d.z * h.z;
    return along >= cosine * sqrt(square * heading);
}

// Adds a counted neighbor (its place at, velocity moving, offset d from
// the particle, squared length square) to the flocking sums of the rules
// whose radii and view take it.
fn addFlock(at: vec3<f32>, moving: vec3<f32>, d: vec3<f32>, square: f32, heading: vec3<f32>,
            sums: ptr<function, Sums>) {
    if (!sees(d, square, heading)) {
        return;
    }
    let squares = params.ruleSquares;
    let apart = square < squares.x && square > 0.0;
    let aligned = square < squares.y;
    let cohered = square < squares.z;
    (*sums).aligned += select(0u, 1u, aligned);
    (*sums).cohered += select(0u, 1u, cohered);
    for (var k = 0u; k < 3u; k++) {
        (*sums).apart[k] -= select(0.0, d[k] / square, apart);
        (*sums).alignedVelocity[k] += select(0.0, moving[k], aligned);
        (*sums).coheredPlace[k] += select(0.0, at[k], cohered);
    }
}

// Adds the pair force's pull of a neighbor an offset d away: toward it by
// the strength at the distance over the radius.
fn addPull(d: vec3<f32>, square: f32, sums: ptr<function, Sums>) {
    let distance = sqrt(square);
    let t = distance * params.path.z;
    var strength = params.rules.w;
    if (params.steer.w != NONE) {
        strength = params.rules.w * curveAt(params.steer.w, t);
    }
    for (var k = 0u; k < 3u; k++) {
        (*sums).pull[k] += strength * d[k] / distance;
    }
}

// Adds weight times the velocity of the given speed along d less v; a d
// of length 0 asks to stand still.
fn toward(d: vec3<f32>, speed: f32, v: vec3<f32>, weight: f32, acc: ptr<function, vec3<f32>>) {
    let length = lengthOf(d);
    let scale = select(0.0, speed / length, length > 0.0);
    for (var k = 0u; k < 3u; k++) {
        (*acc)[k] += weight * (d[k] * scale - v[k]);
    }
}

fn seekFor(p: vec3<f32>, v: vec3<f32>, acc: ptr<function, vec3<f32>>) {
    let d = params.seek.xyz - p;
    let distance = lengthOf(d);
    var speed = params.steerScale.x;
    let radius = params.targets.x;
    if (radius > 0.0 && distance < radius) {
        // Arrival: slower as it nears.
        speed *= distance / radius;
    }
    toward(d, speed, v, params.seek.w, acc);
}

fn fleeFor(p: vec3<f32>, v: vec3<f32>, acc: ptr<function, vec3<f32>>) {
    let d = p - params.flee.xyz;
    let distance = lengthOf(d);
    let radius = params.targets.y;
    if (distance > 0.0 && (radius == 0.0 || distance < radius)) {
        toward(d, params.steerScale.x, v, params.flee.w, acc);
    }
}

// The path's point after segment g's start, around a closed path.
fn pathPoint(g: u32) -> vec3<f32> {
    return params.points[g % params.steer.y].xyz;
}

// The path's point nearest x into q: its segment, the first nearest.
fn nearest(x: vec3<f32>, q: ptr<function, vec3<f32>>) -> u32 {
    var best = bitcast<f32>(0x7F800000u);
    var at = 0u;
    for (var g = 0u; g < params.steer.z; g++) {
        let a = pathPoint(g);
        let b = pathPoint(g + 1u);
        let ab = b - a;
        let square = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
        let along = (x.x - a.x) * ab.x + (x.y - a.y) * ab.y + (x.z - a.z) * ab.z;
        var u = select(0.0, along / square, square > 0.0);
        u = select(select(u, 1.0, u > 1.0), 0.0, u < 0.0);
        let point = vec3<f32>(a.x + ab.x * u, a.y + ab.y * u, a.z + ab.z * u);
        let d = x - point;
        let distance = d.x * d.x + d.y * d.y + d.z * d.z;
        if (distance < best) {
            best = distance;
            at = g;
            *q = point;
        }
    }
    return at;
}

// Moves q on along the path from segment g by a distance: around a closed
// path, to the last point of an open one.
fn advance(closed: bool, start: u32, length: f32, q: ptr<function, vec3<f32>>) {
    var g = start;
    var distance = length;
    let perimeter = params.path.y;
    let segments = params.steer.z;
    if (closed && perimeter > 0.0) {
        distance -= floor(distance / perimeter) * perimeter;
    }
    for (var n = 0u; n <= segments; n++) {
        let b = pathPoint(g + 1u);
        let d = b - *q;
        let left = lengthOf(d);
        if (distance <= left) {
            let u = select(0.0, distance / left, left > 0.0);
            for (var k = 0u; k < 3u; k++) {
                (*q)[k] += d[k] * u;
            }
            return;
        }
        distance -= left;
        *q = b;
        g += 1u;
        if (g == segments) {
            if (!closed) {
                return;
            }
            g = 0u;
        }
    }
}

fn follow(p: vec3<f32>, v: vec3<f32>, acc: ptr<function, vec3<f32>>) {
    let lookahead = params.path.x;
    let predicted = vec3<f32>(p.x + v.x * lookahead, p.y + v.y * lookahead,
                              p.z + v.z * lookahead);
    var q = vec3<f32>(0.0);
    let g = nearest(predicted, &q);
    if (!(lengthOf(predicted - q) > params.targets.w)) {
        return;
    }
    advance(params.path.w != 0.0, g, lengthOf(v) * lookahead, &q);
    toward(q - p, params.steerScale.x, v, params.targets.z, acc);
}

fn flock(sums: Sums, p: vec3<f32>, v: vec3<f32>, acc: ptr<function, vec3<f32>>) {
    let rules = params.rules;
    let aligned = select(0.0, 1.0 / f32(sums.aligned), sums.aligned > 0u);
    let cohered = select(0.0, 1.0 / f32(sums.cohered), sums.cohered > 0u);
    for (var k = 0u; k < 3u; k++) {
        (*acc)[k] += rules.x * sums.apart[k];
        (*acc)[k] += select(0.0, rules.y * (sums.alignedVelocity[k] * aligned - v[k]),
                            sums.aligned > 0u);
        (*acc)[k] += select(0.0, rules.z * (sums.coheredPlace[k] * cohered - p[k]),
                            sums.cohered > 0u);
    }
}

// Holds an acceleration to the most force, 0 for no limit.
fn limit(most: f32, acc: ptr<function, vec3<f32>>) {
    let length = lengthOf(*acc);
    if (most > 0.0 && length > most) {
        let scale = most / length;
        for (var k = 0u; k < 3u; k++) {
            (*acc)[k] *= scale;
        }
    }
}

// Particle i's steering and pull over the plan added to its velocity,
// from its place and velocity at the step's start and its query's sums.
fn steerParticle(i: u32, place: vec3<f32>, velocity: vec3<f32>, sums: Sums) {
    var acc = vec3<f32>(0.0);
    if ((params.grid.y & GRID_FINDS) != 0u) {
        flock(sums, place, velocity, &acc);
    }
    if (params.seek.w > 0.0) {
        seekFor(place, velocity, &acc);
    }
    if (params.flee.w > 0.0) {
        fleeFor(place, velocity, &acc);
    }
    if (params.steer.z > 0u) {
        follow(place, velocity, &acc);
    }
    limit(params.steerScale.y, &acc);
    let c = params.head.x;
    let seconds = params.steerScale.z;
    for (var k = 0u; k < 3u; k++) {
        // The pair force is a force, not steering: not held.
        let moved = velocity[k] + (acc[k] + sums.pull[k]) * seconds;
        particles[(params.words.y + k) * c + i] = bitcast<u32>(moved);
    }
}
