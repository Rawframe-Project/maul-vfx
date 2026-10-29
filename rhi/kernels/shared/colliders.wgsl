// Colliders' fields and gaps as the core resolves them (src/collide.c):
// spliced where a kernel names it, after shared/finite.wgsl,
// shared/forces.wgsl (rotate, sampleGrid) and shared/collider_numbers.wgsl,
// beside a storage buffer of
// words named grids that grid colliders read.

const LARGEST: f32 = 3.40282347e38;

fn dot3(a: vec3<f32>, b: vec3<f32>) -> f32 {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

fn conjugate(q: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(-q.xyz, q.w);
}

// Whether a field's value is within its box: below LARGEST, its value
// outside, and not a NaN, which a grid's NaN value makes and the core's
// compare with infinity reads as outside. The NaN is tested on the bits,
// which a device cannot fold.
fn inBox(f: f32) -> bool {
    return f < LARGEST && !isNan(f);
}

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
fn gridSpan(c: Collider) -> f32 {
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
    let cell = gridSpan(c);
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
        let aIn = inBox(fa);
        let bIn = inBox(fb);
        // At the box's edge one side is outside: a one-sided difference.
        if (aIn && bIn) {
            g[k] = (fa - fb) / (2.0 * h);
        } else if (aIn || bIn) {
            lost = lost || !inBox(f0);
            g[k] = select((f0 - fb) / h, (fa - f0) / h, aIn);
        }
    }
    let square = dot3(g, g);
    if (lost || !(square > 0.0 && isFinite(square))) {
        return vec3<f32>(0.0, 1.0, 0.0);
    }
    return g * (1.0 / sqrt(square));
}

// A grid collider's distance at a place and its unit normal there, as
// the core's mvfxFieldDistance: LARGEST outside its box; a heightfield's
// gap along its Y times the slope's cosine.
fn fieldDistance(c: Collider, world: vec3<f32>, n: ptr<function, vec3<f32>>) -> f32 {
    let local = rotate(conjugate(c.rotation), world - c.center.xyz);
    let f = gridField(c, local);
    if (!inBox(f)) {
        return LARGEST;
    }
    let g = gradientAt(c, local);
    *n = rotate(c.rotation, g);
    return select(f, f * abs(g.y), c.modes.x == COLLIDER_HEIGHTFIELD);
}

// A box's signed distance at a place in its frame, as the core's
// BoxDistance: the distance outside, the nearest face's (negative) within.
fn boxGap(local: vec3<f32>, half: vec3<f32>) -> f32 {
    var outside = 0.0;
    var inside = -LARGEST;
    for (var k = 0u; k < 3u; k++) {
        let d = abs(local[k]) - half[k];
        if (d > 0.0) {
            outside += d * d;
        }
        inside = max(inside, d);
    }
    return select(inside, sqrt(outside), inside > 0.0);
}

// A collider's gap at a place, as the core's mvfxColliderGap (not its
// bounds'): below 0 on its wrong side.
fn colliderGap(c: Collider, point: vec3<f32>) -> f32 {
    let d = point - c.center.xyz;
    let kind = c.modes.x;
    if (kind == COLLIDER_PLANE) {
        return d.x * c.normal.x + d.y * c.normal.y + d.z * c.normal.z;
    }
    if (kind == COLLIDER_SPHERE) {
        let gap = sqrt(d.x * d.x + d.y * d.y + d.z * d.z) - c.center.w;
        return select(gap, -gap, c.modes.y != 0u);
    }
    if (kind == COLLIDER_BOX) {
        let gap = boxGap(rotate(conjugate(c.rotation), d), c.halfExtents.xyz);
        return select(gap, -gap, c.modes.y != 0u);
    }
    var normal: vec3<f32>;
    return fieldDistance(c, point, &normal);
}
