// The sim kernel's sheets in the air (spliced into sim.wgsl): each
// triangle's normal (pressure) and tangent (skin) drag on its mean
// velocity less the air's, as the core's src/aero.c.

// The velocity change the air gives each point of the triangle of points
// a, b and c over a substep of h seconds from seconds after the plan's
// start, as the core's Triangle: its normal (pressure) and tangent (skin)
// drag on its mean velocity less the air's at its middle.
fn triangleChange(a: u32, b: u32, c: u32, seconds: f32, h: f32) -> vec3<f32> {
    let xa = pointPlace(a);
    let xb = pointPlace(b);
    let xc = pointPlace(c);
    let va = readVector(0u, a, params.words.velocity);
    let vb = readVector(0u, b, params.words.velocity);
    let vc = readVector(0u, c, params.words.velocity);
    var e1 = vec3<f32>(0.0);
    var e2 = vec3<f32>(0.0);
    var middle = vec3<f32>(0.0);
    var v = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        e1[k] = xb[k] - xa[k];
        e2[k] = xc[k] - xa[k];
        middle[k] = quotient(xa[k] + xb[k] + xc[k], 3.0);
        v[k] = quotient(va[k] + vb[k] + vc[k], 3.0);
    }
    // The area is the scaled one's times 2^128, as the core's Triangle.
    let crossed = crossEdges(e1, e2);
    let scaled = crossed.w != 0.0;
    var n = crossed.xyz;
    let twice = lengthOf(n);
    if (!(twice > 0.0)) {
        return vec3<f32>(0.0);
    }
    let wind = airAt(middle, seconds);
    var along = 0.0;
    for (var k = 0u; k < 3u; k++) {
        n[k] = quotient(n[k], twice);
        v[k] -= wind[k];
        along += v[k] * n[k];
    }
    var tangent = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        tangent[k] = v[k] - along * n[k];
    }
    let half = 0.5 * twice;
    let normalShare = airShare(params.unitAero.x, half, scaled, abs(along), h);
    let tangentShare = airShare(params.unitAero.y, half, scaled, lengthOf(tangent), h);
    var d = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        d[k] = -normalShare * along * n[k] - tangentShare * tangent[k];
    }
    return d;
}

// The air's change to point q of the unit from its first point over a
// substep, as the core's BlowSheet sums it: the triangles of the whole
// quads it is a corner of, in the quads' row-major order (the quad up and left,
// its second triangle; the quad up, both; the quad left, both; its own,
// its first).
fn airChange(first: u32, q: u32, seconds: f32, h: f32) -> vec3<f32> {
    let w = params.units.y;
    let rows = unitPoints() / w;
    let c = q % w;
    let r = q / w;
    var d = vec3<f32>(0.0);
    if (c >= 1u && r >= 1u && isQuadWhole(first + q - w - 1u, w)) {
        let p = first + q - w - 1u;
        d += triangleChange(p + w, p + 1u, p + w + 1u, seconds, h);
    }
    if (c + 1u < w && r >= 1u && isQuadWhole(first + q - w, w)) {
        let p = first + q - w;
        d += triangleChange(p, p + 1u, p + w, seconds, h);
        d += triangleChange(p + w, p + 1u, p + w + 1u, seconds, h);
    }
    if (c >= 1u && r + 1u < rows && isQuadWhole(first + q - 1u, w)) {
        let p = first + q - 1u;
        d += triangleChange(p, p + 1u, p + w, seconds, h);
        d += triangleChange(p + w, p + 1u, p + w + 1u, seconds, h);
    }
    if (c + 1u < w && r + 1u < rows && isQuadWhole(first + q, w)) {
        let p = first + q;
        d += triangleChange(p, p + 1u, p + w, seconds, h);
    }
    return d;
}

// Blows a sheet from its first point over a substep, as the core's
// mvfxBlowSheets: each point's change kept in its other buffer's place
// (free until the move), then added to its velocity, so every triangle
// reads the velocities from before.
fn blowSheet(first: u32, local: u32, seconds: f32, h: f32) {
    let c = params.counts.x;
    let points = unitPoints();
    for (var q = local; q < points; q += CHUNK) {
        let d = airChange(first, q, seconds, h);
        for (var k = 0u; k < 3u; k++) {
            moved[(params.words.place + k) * c + first + q] = bitcast<u32>(d[k]);
        }
    }
    storageBarrier();
    workgroupBarrier();
    for (var q = local; q < points; q += CHUNK) {
        let i = first + q;
        let v = readVector(0u, i, params.words.velocity);
        setVector(params.words.velocity, i, v + readVector(1u, i, params.words.place));
    }
    storageBarrier();
    workgroupBarrier();
}
