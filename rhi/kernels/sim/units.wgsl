// The sim kernel's units (spliced into sim.wgsl), as the core's
// mvfxStepUnits (src/unit.c), for units without pins, aerodynamics,
// self-collision or tearing: a workgroup a live unit (striding over them),
// each of its substeps
//  - moving its points as particles over the substep (forces, drag, the
//    speed limit and the velocity modules), the predicted places kept in
//    the other buffer, free until the scatter;
//  - projecting its six families of distance constraints in sixteen sets
//    in the core's order, a barrier between sets: within a set no two
//    constraints share a point, so solving one at once is the core's
//    loop over it;
//  - adding the correction over the substep to the velocities, then
//    damping the relative velocities along the same sets.
// The square root, the divisions in the projections and the damping's
// exponential differ from the core's within their rounding.

// A family of constraints: from the point at (c + c0, r + r0) to that at
// (c + c1, r + r1), in sets of the place's (byColumn) or the row's
// remainder by sets; its rest length and compliance over h^2.
struct Family {
    ends: vec4<u32>,
    sets: u32,
    byColumn: bool,
    rest: f32,
    compliance: f32,
}

var<workgroup> unitEnded: u32;

// Family f of the core's FamiliesOf: stretch across and down, shear along
// both diagonals, bending across and down two points apart.
fn familyOf(f: u32, scale: f32, inverseSquare: f32) -> Family {
    let r = params.unitRests;
    let s = params.unitStiffness;
    let stretch = s.x * inverseSquare;
    let shear = s.y * inverseSquare;
    let bend = s.z * inverseSquare;
    switch (f) {
        case 0u: {
            return Family(vec4<u32>(0u, 0u, 1u, 0u), 2u, true, r.x * scale, stretch);
        }
        case 1u: {
            return Family(vec4<u32>(0u, 0u, 0u, 1u), 2u, false, r.y * scale, stretch);
        }
        case 2u: {
            return Family(vec4<u32>(0u, 0u, 1u, 1u), 2u, false, r.z * scale, shear);
        }
        case 3u: {
            return Family(vec4<u32>(1u, 0u, 0u, 1u), 2u, false, r.w * scale, shear);
        }
        case 4u: {
            return Family(vec4<u32>(0u, 0u, 2u, 0u), 4u, true, 2.0 * r.x * scale, bend);
        }
        default: {
            return Family(vec4<u32>(0u, 0u, 0u, 2u), 4u, false, 2.0 * r.y * scale, bend);
        }
    }
}

fn pointPlace(i: u32) -> vec3<f32> {
    return readVector(0u, i, params.words.place);
}

fn setVector(first: u32, i: u32, v: vec3<f32>) {
    let c = params.counts.x;
    for (var k = 0u; k < 3u; k++) {
        source[(first + k) * c + i] = bitcast<u32>(v[k]);
    }
}

// Particle i moved as the core's mvfxIntegrateRange moves it over a
// substep since a tick of the plan for ticks: its predicted place, also
// kept in the other buffer.
fn moveSpan(i: u32, since: u32, ticks: u32) {
    let c = params.counts.x;
    let start = pointPlace(i);
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(start, secondsOf(vec2<u32>(since, 0u)), &a, &wind);
    var scale = 1.0;
    if (params.words.drag != NONE) {
        scale = bitcast<f32>(source[params.words.drag * c + i]);
    }
    let seconds = secondsOf(vec2<u32>(ticks, 0u));
    let f = factorsFor(wind, scale, seconds, false);
    let birth = vec2<u32>(source[params.words.birth * c + i],
                          source[(params.words.birth + 1u) * c + i]);
    let inverseLife = bitcast<f32>(source[params.words.inverseLife * c + i]);
    let tickLow = params.start.x + since;
    let tick = vec2<u32>(tickLow, params.start.y + select(0u, 1u, tickLow < params.start.x));
    var age = secondsOf(subtract64(tick, birth)) * inverseLife;
    age = select(min(age, 1.0), 0.0, !(age > 0.0));
    let v0 = limitSpeed(readVector(0u, i, params.words.velocity), age,
                        factors(bitcast<f32>(params.turns.w), seconds).x);
    let p = moveByModules(start, start + v0 * f.y + a * f.z, age, seconds);
    setVector(params.words.place, i, p);
    setVector(params.words.velocity, i, v0 * f.x + a * f.y);
    for (var k = 0u; k < 3u; k++) {
        moved[(params.words.place + k) * c + i] = bitcast<u32>(p[k]);
    }
}

// The constraints of set s of family f in a unit of width and rows, and
// constraint n of them: its first and second point from the unit's first.
fn setCount(f: Family, s: u32, width: u32, rows: u32) -> u32 {
    // None where the unit is too small for the family (a chain's rows).
    if (width <= max(f.ends.x, f.ends.z) || rows <= max(f.ends.y, f.ends.w)) {
        return 0u;
    }
    let columns = width - max(f.ends.x, f.ends.z);
    let lines = rows - max(f.ends.y, f.ends.w);
    if (f.byColumn) {
        return select(0u, (columns - s + f.sets - 1u) / f.sets, columns > s) * lines;
    }
    return select(0u, (lines - s + f.sets - 1u) / f.sets, lines > s) * columns;
}

fn constraintEnds(f: Family, s: u32, n: u32, width: u32, rows: u32) -> vec2<u32> {
    let columns = width - max(f.ends.x, f.ends.z);
    var c = 0u;
    var r = 0u;
    if (f.byColumn) {
        let across = (columns - s + f.sets - 1u) / f.sets;
        r = n / across;
        c = s + (n % across) * f.sets;
    } else {
        r = s + (n / columns) * f.sets;
        c = n % columns;
    }
    return vec2<u32>((r + f.ends.y) * width + c + f.ends.x, (r + f.ends.w) * width + c + f.ends.z);
}

// Projects the distance between points a and b toward rest, as the core's
// Project with both points free.
fn projectPair(a: u32, b: u32, rest: f32, compliance: f32) {
    let pa = pointPlace(a);
    let pb = pointPlace(b);
    var d = vec3<f32>(0.0);
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        d[k] = pa[k] - pb[k];
        square += d[k] * d[k];
    }
    let length = sqrt(square);
    let sum = 1.0 + 1.0 + compliance;
    if (!(length > 0.0)) {
        return;
    }
    let step = quotient(-(length - rest), sum);
    var na = pa;
    var nb = pb;
    for (var k = 0u; k < 3u; k++) {
        let n = quotient(d[k], length);
        na[k] += 1.0 * step * n;
        nb[k] -= 1.0 * step * n;
    }
    setVector(params.words.place, a, na);
    setVector(params.words.place, b, nb);
}

// Damps the relative velocity of points a and b along the line between
// them by a share, as the core's Damp with both points free.
fn dampPair(a: u32, b: u32, share: f32) {
    let pa = pointPlace(a);
    let pb = pointPlace(b);
    var d = vec3<f32>(0.0);
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        d[k] = pa[k] - pb[k];
        square += d[k] * d[k];
    }
    let length = sqrt(square);
    if (!(length > 0.0)) {
        return;
    }
    let va = readVector(0u, a, params.words.velocity);
    let vb = readVector(0u, b, params.words.velocity);
    var along = 0.0;
    for (var k = 0u; k < 3u; k++) {
        along += quotient((vb[k] - va[k]) * d[k], length);
    }
    let change = quotient(share * along, 1.0 + 1.0);
    var wa = va;
    var wb = vb;
    for (var k = 0u; k < 3u; k++) {
        let n = quotient(d[k], length);
        wa[k] += 1.0 * change * n;
        wb[k] -= 1.0 * change * n;
    }
    setVector(params.words.velocity, a, wa);
    setVector(params.words.velocity, b, wb);
}

// Every set of every family over a unit from its first point, projected
// or damped (share above 0), a barrier after each set.
fn solveSets(first: u32, local: u32, scale: f32, inverseSquare: f32, share: f32) {
    let width = params.units.y;
    let rows = unitPoints() / width;
    for (var f = 0u; f < 6u; f++) {
        let family = familyOf(f, scale, inverseSquare);
        for (var s = 0u; s < family.sets; s++) {
            let count = setCount(family, s, width, rows);
            for (var n = local; n < count; n += CHUNK) {
                let ends = constraintEnds(family, s, n, width, rows) + vec2<u32>(first);
                if (share > 0.0) {
                    dampPair(ends.x, ends.y, share);
                } else {
                    projectPair(ends.x, ends.y, family.rest, family.compliance);
                }
            }
            storageBarrier();
            workgroupBarrier();
        }
    }
}

// Units: each live unit's substeps, a workgroup a unit; the ended (marked
// by update) are skipped.
@compute @workgroup_size(256, 1, 1)
fn units(@builtin(local_invocation_index) local: u32,
         @builtin(workgroup_id) group: vec3<u32>) {
    let c = params.counts.x;
    let points = unitPoints();
    if (local == 0u) {
        liveCount = counts[0];
    }
    let live = workgroupUniformLoad(&liveCount) / points;
    let ticks = params.counts.z;
    let substeps = params.units.z;
    // Rest lengths in the particles' space: a world-space emitter's scale
    // with its instance at the plan's end.
    let scale = select(1.0, params.toOrigin.w, params.start.w == 0u);
    for (var u = group.x; u < live; u += params.units.w) {
        let first = u * points;
        if (local == 0u) {
            unitEnded = select(0u, 1u, source[params.words.alive * c + first] == LIVE_ENDED);
        }
        if (workgroupUniformLoad(&unitEnded) != 0u) {
            continue;
        }
        for (var s = 0u; s < substeps; s++) {
            let since = ticks * s / substeps;
            let to = ticks * (s + 1u) / substeps;
            if (to == since) {
                continue;
            }
            let h = secondsOf(vec2<u32>(to - since, 0u));
            for (var q = local; q < points; q += CHUNK) {
                moveSpan(first + q, since, to - since);
            }
            storageBarrier();
            workgroupBarrier();
            solveSets(first, local, scale, divide(1.0, h * h), 0.0);
            // The integrator's velocity and the constraints' correction over
            // the substep.
            for (var q = local; q < points; q += CHUNK) {
                let i = first + q;
                let p = pointPlace(i);
                let predicted = readVector(1u, i, params.words.place);
                var v = readVector(0u, i, params.words.velocity);
                for (var k = 0u; k < 3u; k++) {
                    v[k] += quotient(p[k] - predicted[k], h);
                }
                setVector(params.words.velocity, i, v);
            }
            storageBarrier();
            workgroupBarrier();
            let share = 1.0 - exp(-params.unitStiffness.w * h);
            if (share > 0.0) {
                solveSets(first, local, scale, 0.0, share);
            }
        }
    }
}
