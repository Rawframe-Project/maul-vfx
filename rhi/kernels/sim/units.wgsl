// The sim kernel's units (spliced into sim.wgsl), as the core's
// mvfxStepUnits (src/unit.c): a workgroup a live unit (striding over
// them), each of its substeps (for units that collide with themselves, a
// dispatch for each half of each, the self pass between: self.wgsl)
//  - blowing a sheet's points in the air (its triangles' normal and
//    tangent drag: sim/aero.wgsl);
//  - moving its points as particles over the substep (forces, drag, the
//    speed limit, the velocity modules, the colliders and the life their
//    contacts take, kills and turns), the predicted places kept in the
//    other buffer, free until the scatter;
//  - pinning its pinned points through their frames;
//  - projecting its six families of distance constraints in sixteen sets
//    in the core's order, a barrier between sets: within a set no two
//    constraints share a point, so solving one at once is the core's
//    loop over it, skipping those a torn edge breaks;
//  - tearing each point's edges across and down stretched past their
//    strain (its tears' facts held for their events after the step);
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
const ATTACH_HOST: u32 = 2u;
const ATTACH_PARTICLES: u32 = 3u;
// A unit's flags (the core's MVFX_ATTACH_*): born of an event; held where
// it is, its frame never moving; let go, its points free.
const UNIT_EVENT: u32 = 2u;
const UNIT_HELD: u32 = 4u;
const UNIT_LET_GO: u32 = 8u;
// The words before a source's copied arrays (its live count, padded).
const SOURCE_HEAD: u32 = 4u;

var<workgroup> unitEnded: u32;
// For units held to frames, the unit's frame at the plan's start and its
// target at the end (each an origin, then a rotation).
var<workgroup> unitFrom: array<vec4<f32>, 2>;
var<workgroup> unitTo: array<vec4<f32>, 2>;
// Whether the unit is let go, its pinned points free.
var<workgroup> unitLetGo: u32;
var<workgroup> unitKilled: atomic<u32>;
// The unit's tears over its substeps.
var<workgroup> unitTore: atomic<u32>;

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

fn setVectorIn(buffer: u32, first: u32, i: u32, v: vec3<f32>) {
    let c = params.counts.x;
    for (var k = 0u; k < 3u; k++) {
        setWordIn(buffer, (first + k) * c + i, bitcast<u32>(v[k]));
    }
}

// Whether a 64-bit tick (two words) is past another.
fn isAfter(tick: vec2<u32>, end: vec2<u32>) -> bool {
    return tick.y > end.y || (tick.y == end.y && tick.x > end.x);
}

// What a span's move reads and changes of a particle, held apart from its
// buffer: its place, velocity and turn, its birth and death ticks, its
// inverse life, its drag scale (1 where it stores none), its contacts'
// count (where it stores one, else 0) and whether a kill took it.
struct Moving {
    place: vec3<f32>,
    velocity: vec3<f32>,
    turning: Turning,
    birth: vec2<u32>,
    death: vec2<u32>,
    inverseLife: f32,
    dragScale: f32,
    contacts: u32,
    killed: bool,
}

fn loadMoving(buffer: u32, i: u32) -> Moving {
    let c = params.counts.x;
    let w = params.words;
    var m = Moving();
    m.place = readVector(buffer, i, w.place);
    m.velocity = readVector(buffer, i, w.velocity);
    m.turning = loadTurning(buffer, i);
    m.birth = vec2<u32>(wordIn(buffer, w.birth * c + i), wordIn(buffer, (w.birth + 1u) * c + i));
    m.death = vec2<u32>(wordIn(buffer, w.death * c + i), wordIn(buffer, (w.death + 1u) * c + i));
    m.inverseLife = bitcast<f32>(wordIn(buffer, w.inverseLife * c + i));
    m.dragScale = 1.0;
    if (w.drag != NONE) {
        m.dragScale = bitcast<f32>(wordIn(buffer, w.drag * c + i));
    }
    if (w.collisions != NONE) {
        m.contacts = wordIn(buffer, w.collisions * c + i);
    }
    return m;
}

// Writes what a span's move changes back; a kill holds as the kept word 0
// for the rest of the step.
fn storeMoving(buffer: u32, i: u32, m: Moving) {
    let c = params.counts.x;
    let w = params.words;
    setVectorIn(buffer, w.place, i, m.place);
    setVectorIn(buffer, w.velocity, i, m.velocity);
    storeTurning(buffer, i, m.turning);
    setWordIn(buffer, w.inverseLife * c + i, bitcast<u32>(m.inverseLife));
    setWordIn(buffer, w.death * c + i, m.death.x);
    setWordIn(buffer, (w.death + 1u) * c + i, m.death.y);
    if (w.collisions != NONE) {
        setWordIn(buffer, w.collisions * c + i, m.contacts);
    }
    if (m.killed) {
        setWordIn(buffer, w.alive * c + i, 0u);
    }
}

// A particle moved as the core's mvfxIntegrateRange moves it over a
// substep since a tick of the plan for ticks: its contacts with the
// colliders (the step's fate, its first contact for its event), the life
// they take, a kill (by a contact, the life lost, a kill rule or an
// attractor's kill radius), and its turn over the substep.
fn moveState(m: ptr<function, Moving>, since: u32, ticks: u32) -> Fate {
    let start = (*m).place;
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(start, secondsOf(vec2<u32>(since, 0u)), &a, &wind);
    let seconds = secondsOf(vec2<u32>(ticks, 0u));
    let f = factorsFor(wind, (*m).dragScale, seconds, false);
    let birth = (*m).birth;
    let inverseLife = (*m).inverseLife;
    let tick = planTick(since);
    let v0 = limitSpeed((*m).velocity, spanAge(tick, birth, inverseLife),
                        factors(bitcast<f32>(params.turns.w), seconds).x);
    var p = moveByModules(start, start + v0 * f.y + a * f.z, spanAge(tick, birth, inverseLife),
                          seconds);
    var v = v0 * f.x + a * f.y;
    // A rigid body meets the colliders at its shape, in its own solve.
    var fate = Fate();
    fate.inverseLife = inverseLife;
    fate.death = (*m).death;
    if (params.rigid.y == 0u) {
        fate = meet(start, &p, &v, birth, inverseLife, (*m).death);
    }
    (*m).place = p;
    (*m).velocity = v;
    if (params.words.collisions != NONE) {
        let count = (*m).contacts;
        (*m).contacts =
            select(count + fate.contacts, 0xFFFFFFFFu, count > 0xFFFFFFFFu - fate.contacts);
    }
    (*m).inverseLife = fate.inverseLife;
    (*m).death = fate.death;
    let end = planTick(since + ticks);
    // The life a contact takes kills only at the contact, as the core's
    // Collide: a later point's own death is past its unit's (liveDeath).
    (*m).killed = (*m).killed || fate.killed || (fate.contacts > 0u && !isAfter(fate.death, end)) ||
                  killed(start, p, v, subtract64(end, birth), (*m).contacts);
    // Turned over the substep at its age then, with the life its contacts
    // left.
    let angular = factors(params.unitAcross.w, seconds);
    (*m).turning = turnState((*m).turning, angular.xy,
                             lifeAt(LIFE_ROTATION, spanAge(tick, birth, fate.inverseLife)));
    return fate;
}

// Particle i moved by moveState, its predicted place also kept in the
// other buffer, free until the scatter, and the first contact of the step
// noted for its event.
fn moveSpan(i: u32, since: u32, ticks: u32) {
    var m = loadMoving(0u, i);
    let inverseLife = m.inverseLife;
    let fate = moveState(&m, since, ticks);
    if (fate.contacted) {
        noteUnitContact(i, contactTick(since, ticks, fate.first.t), fate.first, m.velocity,
                        inverseLife);
    }
    storeMoving(0u, i, m);
    setVectorIn(1u, params.words.place, i, m.place);
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

// Whether point q of the unit a workgroup runs is held, pinned and its
// unit not let go, as the core's mvfxPointWeight; its weight (0 held, 1
// free).
fn isHeld(q: u32) -> bool {
    return isPinned(q) && unitLetGo == 0u;
}

fn pointWeight(i: u32, first: u32) -> f32 {
    return select(1.0, 0.0, isHeld(i - first));
}

// Whether the units are held to frames (the host's or a source's
// particles'), each unit keeping one.
fn isFramed() -> bool {
    return params.unitPins.y >= ATTACH_HOST;
}

// Unit point i's stored frame (at its unit's first point): its origin and
// its rotation.
fn storedFrame(first: u32) -> array<vec4<f32>, 2> {
    let c = params.counts.x;
    var q = vec4<f32>(0.0);
    for (var k = 0u; k < 4u; k++) {
        q[k] = bitcast<f32>(source[(params.unitArrays.z + k) * c + first]);
    }
    return array<vec4<f32>, 2>(vec4<f32>(readVector(0u, first, params.unitArrays.y), 0.0), q);
}

// The host's frame f at the plan's end in the particles' space.
fn hostFrame(f: u32) -> array<vec4<f32>, 2> {
    let at = params.unitFrames.x + 2u * f;
    return array<vec4<f32>, 2>(bitcast<vec4<f32>>(births[at]), bitcast<vec4<f32>>(births[at + 1u]));
}

// Word w of the source's particles copied after its step: its live
// count, then its serials, places and orientations, an array of its
// capacity each.
fn copiedWord(w: u32) -> u32 {
    let at = 4u * params.unitFrames.x + w;
    return births[at >> 2u][at & 3u];
}

// The source's particle with a serial, or NONE, as the core's FindSource:
// its live particles are in the order of their births, so their serials
// ascend modulo 2^32 from the oldest's.
fn findSource(serial: u32) -> u32 {
    let count = copiedWord(0u);
    if (count == 0u) {
        return NONE;
    }
    let base = copiedWord(SOURCE_HEAD);
    let wanted = serial - base;
    var low = 0u;
    var high = count;
    while (low < high) {
        let middle = low + (high - low) / 2u;
        if (copiedWord(SOURCE_HEAD + middle) - base < wanted) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return select(NONE, low, low < count && copiedWord(SOURCE_HEAD + low) == serial);
}

// The frame of the source's particle i, as the core's SourceFrame: its
// place and, where its emitter stores one, its orientation.
fn sourceFrame(i: u32) -> array<vec4<f32>, 2> {
    let c = params.unitFrames.y;
    var frame = array<vec4<f32>, 2>(vec4<f32>(0.0), vec4<f32>(0.0, 0.0, 0.0, 1.0));
    for (var k = 0u; k < 3u; k++) {
        frame[0][k] = bitcast<f32>(copiedWord(SOURCE_HEAD + (1u + k) * c + i));
    }
    for (var k = 0u; params.unitFrames.z != 0u && k < 4u; k++) {
        frame[1][k] = bitcast<f32>(copiedWord(SOURCE_HEAD + (4u + k) * c + i));
    }
    return frame;
}

// A unit's frame's target at the plan's end, as the core's TargetOf: its
// stored frame when held or let go; for the host's frames the one it
// names, its stored frame when that is not set; for a source's particles
// the frame of the one it names, and a unit whose particle is gone let go
// (its flags marked), as the core's BeginAttachments.
fn frameTarget(first: u32, stored: array<vec4<f32>, 2>) -> array<vec4<f32>, 2> {
    let c = params.counts.x;
    let flags = source[params.unitArrays.w * c + first];
    let f = source[params.unitPins.z * c + first];
    if ((flags & (UNIT_HELD | UNIT_LET_GO)) != 0u) {
        return stored;
    }
    if (params.unitPins.y == ATTACH_PARTICLES) {
        let i = findSource(f);
        if (i == NONE) {
            source[params.unitArrays.w * c + first] = flags | UNIT_LET_GO;
            return stored;
        }
        return sourceFrame(i);
    }
    if (f >= params.unitFrames.y) {
        return stored;
    }
    return hostFrame(f);
}

// The unit's frame a share f of the way to its target, as the core's
// EasedFrame: its origin linearly, its rotation by a normalized blend
// along the shorter way.
fn easedFrame(f: f32) -> array<vec4<f32>, 2> {
    let start = unitFrom;
    let to = unitTo;
    var out = array<vec4<f32>, 2>(vec4<f32>(0.0), vec4<f32>(0.0));
    for (var k = 0u; k < 3u; k++) {
        out[0][k] = start[0][k] + (to[0][k] - start[0][k]) * f;
    }
    var along = 0.0;
    for (var k = 0u; k < 4u; k++) {
        along += start[1][k] * to[1][k];
    }
    let sign = select(1.0, -1.0, along < 0.0);
    var square = 0.0;
    for (var k = 0u; k < 4u; k++) {
        out[1][k] = start[1][k] + (sign * to[1][k] - start[1][k]) * f;
        square += out[1][k] * out[1][k];
    }
    let length = sqrt(square);
    for (var k = 0u; length > 0.0 && k < 4u; k++) {
        out[1][k] = quotient(out[1][k], length);
    }
    return out;
}

// A pinned point's place at a tick of the plan, as the core's
// mvfxPinPlace: its anchor, held in the particles' space; for units held
// to the instance, in its space mapped by the instance then; for units
// held to frames, in the unit's frame eased that far.
fn pinPlace(i: u32, tick: u32) -> vec3<f32> {
    let anchor = readVector(0u, i, params.unitArrays.x);
    if (isFramed()) {
        let frame = easedFrame(select(1.0, f32(tick) / f32(params.counts.z), params.counts.z > 0u));
        return rotate(frame[1], anchor) + frame[0].xyz;
    }
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

// Point i's torn edges (1 across, 2 down), 0 for units that do not tear.
fn tornOf(i: u32) -> u32 {
    if (params.unitTears.x == NONE) {
        return 0u;
    }
    return source[params.unitTears.x * params.counts.x + i];
}

// Whether a sheet's quad from point i (w across) is whole, none of its
// four edges torn, as the core's mvfxIsQuadWhole.
fn isQuadWhole(i: u32, w: u32) -> bool {
    let t = tornOf(i);
    return ((t | tornOf(i + w)) & 1u) == 0u && ((t | tornOf(i + 1u)) & 2u) == 0u;
}

// Whether family f's constraint from point i (w across) is broken by a
// torn edge, as the core's IsBroken: a stretch constraint torn; a quad's
// diagonals with any of its four edges; a bending pair with either edge
// it spans.
fn isBroken(f: u32, i: u32, w: u32) -> bool {
    if (params.unitTears.x == NONE) {
        return false;
    }
    switch f {
        case 0u: {
            return (tornOf(i) & 1u) != 0u;
        }
        case 1u: {
            return (tornOf(i) & 2u) != 0u;
        }
        case 2u, 3u: {
            return !isQuadWhole(i, w);
        }
        case 4u: {
            return ((tornOf(i) | tornOf(i + 1u)) & 1u) != 0u;
        }
        default: {
            return ((tornOf(i) | tornOf(i + w)) & 2u) != 0u;
        }
    }
}

// Notes point a's tear of an edge (0 across, 1 down) at a tick of the
// plan for its event, when the emitter writes tear events: its flag, the
// tick, the edge's middle and direction, and the point's velocity and
// inverse life then (MVFX_TEAR_FACTS a point, after the spawn region).
fn noteTear(a: u32, edge: u32, tick: u32, middle: vec3<f32>, along: vec3<f32>) {
    if (params.unitTears.y == 0u) {
        return;
    }
    let c = params.counts.x;
    events[EVENT_FACTS * a] |= EVENT_TEAR_ACROSS << edge;
    let at = params.unitTears.y + 22u * a + 11u * edge;
    let v = readVector(0u, a, params.words.velocity);
    events[at] = tick;
    for (var k = 0u; k < 3u; k++) {
        events[at + 1u + k] = bitcast<u32>(middle[k]);
        events[at + 4u + k] = bitcast<u32>(along[k]);
        events[at + 7u + k] = bitcast<u32>(v[k]);
    }
    events[at + 10u] = source[params.words.inverseLife * c + a];
}

// Tears the edge from point a to point b (bit of a's torn edges) longer
// than longest at a tick of the plan, as the core's TearEdge: a's bit
// set, the unit's tally counted, its event's facts noted.
fn tearEdge(a: u32, b: u32, bit: u32, longest: f32, tick: u32) {
    let pa = pointPlace(a);
    let pb = pointPlace(b);
    var d = vec3<f32>(0.0);
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        d[k] = pb[k] - pa[k];
        square += d[k] * d[k];
    }
    let torn = tornOf(a);
    if ((torn & bit) != 0u || !(square > longest * longest)) {
        return;
    }
    source[params.unitTears.x * params.counts.x + a] = torn | bit;
    atomicAdd(&unitTore, 1u);
    let length = lengthOf(d);
    var middle = vec3<f32>(0.0);
    var along = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        middle[k] = pa[k] + 0.5 * d[k];
        along[k] = quotient(d[k], length);
    }
    noteTear(a, bit - 1u, tick, middle, along);
}

// Tears point q's edges across and down of the unit from its first point
// stretched past their rests times 1 + tearStrain at a substep's end (a
// tick of the plan), as the core's Tear: each point its own edges, the
// places no one writes then.
fn tearPoint(first: u32, q: u32, scale: f32, tick: u32) {
    let w = params.units.y;
    let rows = unitPoints() / w;
    let stretch = 1.0 + params.unitAero.z;
    if (q % w + 1u < w) {
        tearEdge(first + q, first + q + 1u, 1u, familyOf(0u, scale, 0.0).rest * stretch, tick);
    }
    if (q / w + 1u < rows) {
        tearEdge(first + q, first + q + w, 2u, familyOf(1u, scale, 0.0).rest * stretch, tick);
    }
}

// A torn chain's ribbon at point q of the unit from its first point, as
// the core's SplitRibbons walks it: the serial of the point after the
// nearest torn edge across before it, else its first point's id.
fn splitRibbon(first: u32, q: u32) {
    let c = params.counts.x;
    var id = source[params.words.ribbon * c + first];
    for (var k = q; k >= 1u; k--) {
        if ((tornOf(first + k - 1u) & 1u) != 0u) {
            id = source[params.words.serial * c + first + k];
            break;
        }
    }
    source[params.words.ribbon * c + first + q] = id;
}

// Projects the distance between points a and b of weights wa and wb
// toward rest, as the core's Project.
fn projectPair(a: u32, b: u32, rest: f32, compliance: f32, wa: f32, wb: f32) {
    let pa = pointPlace(a);
    let pb = pointPlace(b);
    let d = pa - pb;
    let length = lengthOf(d);
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
    let d = pa - pb;
    let length = lengthOf(d);
    if (!(length > 0.0) || !(wa + wb > 0.0)) {
        return;
    }
    let va = readVector(0u, a, params.words.velocity);
    let vb = readVector(0u, b, params.words.velocity);
    var along = 0.0;
    for (var k = 0u; k < 3u; k++) {
        along += quotient((vb[k] - va[k]) * d[k], length);
    }
    if (!isFinite(along)) {
        // A relative velocity times a distance overflowed where each fits:
        // along the unit direction instead, as the core's Damp.
        along = 0.0;
        for (var k = 0u; k < 3u; k++) {
            along += (vb[k] - va[k]) * quotient(d[k], length);
        }
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
                if (isBroken(f, ends.x - family.ends.y * width - family.ends.x, width)) {
                    continue;
                }
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

// What a dispatch of the unit pass runs (root.phase): every substep of
// each unit (UNITS_WHOLE); or, for an emitter whose units collide with
// themselves (their pass between), one substep's first part (the air,
// the move, the pins and the constraints) or its rest (the tears, the
// velocities and the damping, the unit's end after the last).
const UNITS_WHOLE: u32 = 0u;
const UNITS_MOVE: u32 = 1u;
const UNITS_REST: u32 = 2u;

// Starts a unit from its first point in a dispatch, by its first
// invocation: whether it ended, its kills and tears cleared, its frame
// and target (each dispatch finds the same: a unit whose particle is gone
// is let go by the first), whether it is let go.
fn unitBegin(first: u32, local: u32) {
    let c = params.counts.x;
    if (local == 0u) {
        unitEnded = select(0u, 1u, source[params.words.alive * c + first] == LIVE_ENDED);
        atomicExchange(&unitKilled, 0u);
        atomicExchange(&unitTore, 0u);
        unitLetGo = 0u;
        if (isFramed()) {
            unitFrom = storedFrame(first);
            unitTo = frameTarget(first, unitFrom);
            unitLetGo = source[params.unitArrays.w * c + first] & UNIT_LET_GO;
        }
    }
}

// Substep s's span of the plan: its first tick and its ticks.
fn substepSpan(s: u32) -> vec2<u32> {
    let ticks = params.counts.z;
    let substeps = params.units.z;
    let since = ticks * s / substeps;
    return vec2<u32>(since, ticks * (s + 1u) / substeps - since);
}

// Substep s's first part over a unit from its first point: the air, each
// point's move and its pin, the constraints.
fn substepMove(first: u32, local: u32, s: u32, scale: f32) {
    let span = substepSpan(s);
    let h = secondsOf(vec2<u32>(span.y, 0u));
    if (params.unitAero.x != 0.0 || params.unitAero.y != 0.0) {
        blowSheet(first, local, secondsOf(vec2<u32>(span.x, 0u)), h);
    }
    for (var q = local; q < unitPoints(); q += CHUNK) {
        moveSpan(first + q, span.x, span.y);
        if (isHeld(q)) {
            pinPoint(first + q, span.x, span.x + span.y, h);
        }
    }
    storageBarrier();
    workgroupBarrier();
    solveSets(first, local, scale, divide(1.0, h * h), 0.0);
}

// Substep s's rest over a unit from its first point: its tears (and a
// torn chain's ribbons split), the velocities' correction, the damping;
// its tears added to its tally.
fn substepRest(first: u32, local: u32, s: u32, scale: f32) {
    let c = params.counts.x;
    let points = unitPoints();
    let span = substepSpan(s);
    let h = secondsOf(vec2<u32>(span.y, 0u));
    if (params.unitTears.x != NONE) {
        for (var q = local; q < points; q += CHUNK) {
            tearPoint(first, q, scale, span.x + span.y);
        }
    }
    if (params.unitTears.x != NONE && params.words.ribbon != NONE) {
        // A torn unit drawn as ribbons splits them where it tore; its
        // first point's id never changes.
        storageBarrier();
        workgroupBarrier();
        for (var q = max(local, 1u); atomicLoad(&unitTore) != 0u && q < points; q += CHUNK) {
            splitRibbon(first, q);
        }
    }
    // The integrator's velocity and the constraints' correction over the
    // substep; a held point's the pin's.
    for (var q = local; q < points; q += CHUNK) {
        let i = first + q;
        if (isHeld(q)) {
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
    if (local == 0u && params.unitTears.x != NONE) {
        // Its tears' tally, which offsets sums (MVFX_GPU_TEAR_COUNT), in its
        // first point's alive word of the other buffer, free until the
        // scatter.
        moved[params.words.alive * c + first] += atomicExchange(&unitTore, 0u);
    }
}

// Ends a unit from its first point after its last substep: its frame at
// its target, as the core's EndAttachments; its points killed together.
fn unitEnd(first: u32, local: u32) {
    if (local == 0u && isFramed()) {
        storeFrame(first, unitTo);
    }
    wholeUnit(first, local);
}

// Units: each live unit's substeps (all, or one's part: root.phase), a
// workgroup a unit, the ended (marked by update) skipped; after the last,
// the plan's births of units, killed whole.
@compute @workgroup_size(256, 1, 1)
fn units(@builtin(local_invocation_index) local: u32,
         @builtin(workgroup_id) group: vec3<u32>) {
    let c = params.counts.x;
    let points = unitPoints();
    if (local == 0u) {
        liveCount = counts[0];
    }
    let live = workgroupUniformLoad(&liveCount) / points;
    let substeps = params.units.z;
    let phase = root.phase;
    let s = root.substep;
    // Rest lengths in the particles' space: a world-space emitter's scale
    // with its instance at the plan's end.
    let scale = select(1.0, params.toOrigin.w, params.start.w == 0u);
    for (var u = group.x; u < live; u += params.units.w) {
        let first = u * points;
        unitBegin(first, local);
        if (local == 0u && params.unitTears.x != NONE && phase != UNITS_REST && s == 0u) {
            moved[params.words.alive * c + first] = 0u;
        }
        if (workgroupUniformLoad(&unitEnded) != 0u) {
            continue;
        }
        if (phase == UNITS_WHOLE) {
            for (var k = 0u; k < substeps; k++) {
                if (substepSpan(k).y > 0u) {
                    substepMove(first, local, k, scale);
                    substepRest(first, local, k, scale);
                }
            }
            unitEnd(first, local);
            continue;
        }
        if (substepSpan(s).y > 0u) {
            if (phase == UNITS_MOVE) {
                substepMove(first, local, s, scale);
            } else {
                substepRest(first, local, s, scale);
            }
        }
        if (phase == UNITS_REST && s + 1u == substeps) {
            unitEnd(first, local);
        }
    }
    if (phase == UNITS_WHOLE || (phase == UNITS_REST && s + 1u == substeps)) {
        wholeBirths(group.x, local);
    }
}

// Stores a unit's frame at its first point.
fn storeFrame(first: u32, frame: array<vec4<f32>, 2>) {
    let c = params.counts.x;
    for (var k = 0u; k < 3u; k++) {
        source[(params.unitArrays.y + k) * c + first] = bitcast<u32>(frame[0][k]);
    }
    for (var k = 0u; k < 4u; k++) {
        source[(params.unitArrays.z + k) * c + first] = bitcast<u32>(frame[1][k]);
    }
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
