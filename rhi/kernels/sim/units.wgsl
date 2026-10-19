// The sim kernel's units (spliced into sim.wgsl), as the core's
// mvfxStepUnits (src/unit.c), for units without pins, aerodynamics,
// self-collision or tearing: a workgroup a live unit (striding over them),
// each of its substeps
//  - moving its points as particles over the substep (forces, drag, the
//    speed limit, the velocity modules, the colliders and the life their
//    contacts take, kills and turns), the predicted places kept in the
//    other buffer, free until the scatter;
//  - projecting its six families of distance constraints in sixteen sets
//    in the core's order, a barrier between sets: within a set no two
//    constraints share a point, so solving one at once is the core's
//    loop over it;
//  - adding the correction over the substep to the velocities, then
//    damping the relative velocities along the same sets;
// then its points killed together when one is, each with the first
// point's death.
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

// The points of a unit pinned (params.unitPins.x), and what they are
// held to (params.unitPins.y, mvfxAttachKind).
const UNIT_PIN_FIRST: u32 = 1u;
const UNIT_PIN_ROW: u32 = 2u;
const ATTACH_INSTANCE: u32 = 1u;

var<workgroup> unitEnded: u32;
var<workgroup> unitKilled: atomic<u32>;

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

// Whether a 64-bit tick (two words) is past another.
fn isAfter(tick: vec2<u32>, end: vec2<u32>) -> bool {
    return tick.y > end.y || (tick.y == end.y && tick.x > end.x);
}

// Particle i moved as the core's mvfxIntegrateRange moves it over a
// substep since a tick of the plan for ticks: its predicted place, also
// kept in the other buffer; its contacts with the colliders (the first of
// the step noted for its event), the life they take, a kill (by a contact, the life lost, a kill rule or an
// attractor's kill radius) held as its kept word 0 for the rest of the
// step, and its turn over the substep.
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
    let tick = planTick(since);
    let v0 = limitSpeed(readVector(0u, i, params.words.velocity), spanAge(tick, birth, inverseLife),
                        factors(bitcast<f32>(params.turns.w), seconds).x);
    var p = moveByModules(start, start + v0 * f.y + a * f.z, spanAge(tick, birth, inverseLife),
                          seconds);
    var v = v0 * f.x + a * f.y;
    let death = vec2<u32>(source[params.words.death * c + i],
                          source[(params.words.death + 1u) * c + i]);
    let fate = meet(start, &p, &v, birth, inverseLife, death);
    if (fate.contacted) {
        noteUnitContact(i, contactTick(since, ticks, fate.first.t), fate.first, v, inverseLife);
    }
    setVector(params.words.place, i, p);
    setVector(params.words.velocity, i, v);
    for (var k = 0u; k < 3u; k++) {
        moved[(params.words.place + k) * c + i] = bitcast<u32>(p[k]);
    }
    var contacts = 0u;
    if (params.words.collisions != NONE) {
        let count = source[params.words.collisions * c + i];
        contacts = select(count + fate.contacts, 0xFFFFFFFFu, count > 0xFFFFFFFFu - fate.contacts);
        source[params.words.collisions * c + i] = contacts;
    }
    source[params.words.inverseLife * c + i] = bitcast<u32>(fate.inverseLife);
    source[params.words.death * c + i] = fate.death.x;
    source[(params.words.death + 1u) * c + i] = fate.death.y;
    let end = planTick(since + ticks);
    // The life a contact takes kills only at the contact, as the core's
    // Collide: a later point's own death is past its unit's (liveDeath).
    if (fate.killed || (fate.contacts > 0u && !isAfter(fate.death, end)) ||
        killed(start, p, v, subtract64(end, birth), contacts)) {
        source[params.words.alive * c + i] = 0u;
    }
    // Turned over the substep at its age then, with the life its contacts
    // left.
    let angular = factors(params.unitAcross.w, seconds);
    turn(0u, i, angular.xy, lifeAt(LIFE_ROTATION, spanAge(tick, birth, fate.inverseLife)));
}

// A particle's normalized age at a tick, held to [0, 1].
fn spanAge(tick: vec2<u32>, birth: vec2<u32>, inverseLife: f32) -> f32 {
    let age = secondsOf(subtract64(tick, birth)) * inverseLife;
    return select(min(age, 1.0), 0.0, !(age > 0.0));
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

// Whether point q of a unit is pinned, as the core's mvfxIsPinnedPoint
// for an emitter with anchors; its weight (0 pinned, 1 free).
fn isPinned(q: u32) -> bool {
    let pins = params.unitPins.x;
    return params.unitArrays.x != NONE &&
           (((pins & UNIT_PIN_FIRST) != 0u && q == 0u) || ((pins & UNIT_PIN_ROW) != 0u && q < params.units.y));
}

fn pointWeight(i: u32, first: u32) -> f32 {
    return select(1.0, 0.0, isPinned(i - first));
}

// A pinned point's place at a tick of the plan, as the core's
// mvfxPinPlace: its anchor, held in the particles' space, or for units
// held to the instance in its space, mapped by the instance then.
fn pinPlace(i: u32, tick: u32) -> vec3<f32> {
    let anchor = readVector(0u, i, params.unitArrays.x);
    if (params.unitPins.y != ATTACH_INSTANCE) {
        return anchor;
    }
    var origin = vec3<f32>(0.0);
    var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    let scale = placeAt(tick, &origin, &rotation);
    return origin + rotate(rotation, anchor * scale);
}

// Pins point i over a substep from a tick of the plan to another of h
// seconds, as the core's Pin: at its place at the end, moving from its
// place at the start.
fn pinPoint(i: u32, since: u32, to: u32, h: f32) {
    let before = pinPlace(i, since);
    let after = pinPlace(i, to);
    var v = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        v[k] = quotient(after[k] - before[k], h);
    }
    setVector(params.words.place, i, after);
    setVector(params.words.velocity, i, v);
}

// Projects the distance between points a and b of weights wa and wb
// toward rest, as the core's Project.
fn projectPair(a: u32, b: u32, rest: f32, compliance: f32, wa: f32, wb: f32) {
    let pa = pointPlace(a);
    let pb = pointPlace(b);
    var d = vec3<f32>(0.0);
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        d[k] = pa[k] - pb[k];
        square += d[k] * d[k];
    }
    let length = sqrt(square);
    let sum = wa + wb + compliance;
    if (!(length > 0.0) || !(sum > 0.0)) {
        return;
    }
    let step = quotient(-(length - rest), sum);
    var na = pa;
    var nb = pb;
    for (var k = 0u; k < 3u; k++) {
        let n = quotient(d[k], length);
        na[k] += wa * step * n;
        nb[k] -= wb * step * n;
    }
    setVector(params.words.place, a, na);
    setVector(params.words.place, b, nb);
}

// Damps the relative velocity of points a and b of weights wa and wb
// along the line between them by a share, as the core's Damp.
fn dampPair(a: u32, b: u32, share: f32, wa: f32, wb: f32) {
    let pa = pointPlace(a);
    let pb = pointPlace(b);
    var d = vec3<f32>(0.0);
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        d[k] = pa[k] - pb[k];
        square += d[k] * d[k];
    }
    let length = sqrt(square);
    if (!(length > 0.0) || !(wa + wb > 0.0)) {
        return;
    }
    let va = readVector(0u, a, params.words.velocity);
    let vb = readVector(0u, b, params.words.velocity);
    var along = 0.0;
    for (var k = 0u; k < 3u; k++) {
        along += quotient((vb[k] - va[k]) * d[k], length);
    }
    let change = quotient(share * along, wa + wb);
    var ua = va;
    var ub = vb;
    for (var k = 0u; k < 3u; k++) {
        let n = quotient(d[k], length);
        ua[k] += wa * change * n;
        ub[k] -= wb * change * n;
    }
    setVector(params.words.velocity, a, ua);
    setVector(params.words.velocity, b, ub);
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
                let wa = pointWeight(ends.x, first);
                let wb = pointWeight(ends.y, first);
                if (share > 0.0) {
                    dampPair(ends.x, ends.y, share, wa, wb);
                } else {
                    projectPair(ends.x, ends.y, family.rest, family.compliance, wa, wb);
                }
            }
            storageBarrier();
            workgroupBarrier();
        }
    }
}

// Units: each live unit's substeps, a workgroup a unit, the ended (marked
// by update) skipped; then the plan's births of units, killed whole.
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
            atomicExchange(&unitKilled, 0u);
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
                if (isPinned(q)) {
                    pinPoint(first + q, since, to, h);
                }
            }
            storageBarrier();
            workgroupBarrier();
            solveSets(first, local, scale, divide(1.0, h * h), 0.0);
            // The integrator's velocity and the constraints' correction over
            // the substep; a pinned point's the pin's.
            for (var q = local; q < points; q += CHUNK) {
                let i = first + q;
                if (isPinned(q)) {
                    continue;
                }
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
        wholeUnit(first, local);
    }
    wholeBirths(group.x, local);
}

// A unit's points die together: a kill of one kills all, as the core's
// RemoveKilled takes a unit (its death is its first point's, liveDeath).
fn wholeUnit(first: u32, local: u32) {
    let c = params.counts.x;
    let points = unitPoints();
    for (var q = local; q < points; q += CHUNK) {
        if (source[params.words.alive * c + first + q] == 0u) {
            atomicOr(&unitKilled, 1u);
        }
    }
    storageBarrier();
    workgroupBarrier();
    if (atomicLoad(&unitKilled) != 0u) {
        for (var q = local; q < points; q += CHUNK) {
            source[params.words.alive * c + first + q] = 0u;
        }
    }
    storageBarrier();
    workgroupBarrier();
}

// The plan's births of whole units: a birth killed on its way to the
// plan's end (killBirths, before this pass) kills its unit's every point,
// as the core's RemoveKilled takes the units it appended.
fn wholeBirths(group: u32, local: u32) {
    let points = unitPoints();
    for (var b = group; b < birthCount() / points; b += params.units.w) {
        let first = b * points;
        if (local == 0u) {
            atomicExchange(&unitKilled, 0u);
        }
        workgroupBarrier();
        for (var q = local; q < points; q += CHUNK) {
            if ((accepted[first + q] & 3u) == 2u) {
                atomicOr(&unitKilled, 1u);
            }
        }
        workgroupBarrier();
        let killed = atomicLoad(&unitKilled) != 0u;
        for (var q = local; q < points; q += CHUNK) {
            let mark = accepted[first + q];
            if (killed && (mark & 3u) == 1u) {
                accepted[first + q] = (mark & ~3u) | 2u;
            }
        }
        workgroupBarrier();
    }
}
