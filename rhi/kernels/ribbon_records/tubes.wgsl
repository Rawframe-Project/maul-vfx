// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbons drawn round (ribbon_records.wgsl): a tube's frame carried point
// to point along its chain, its rings, its caps and their indices.

// A tube's frame at its last point: the place in the particles' space
// (where near places subtract exactly), the unit tangent and the unit
// normal its rings start from; started once it has one (the core's
// mvfxTubeFrame).
struct Frame {
    place: vec3<f32>,
    tangent: vec3<f32>,
    normal: vec3<f32>,
    started: bool,
}

// v less its part along a unit t, normalized; zero when nothing is left.
fn acrossOf(v: vec3<f32>, t: vec3<f32>) -> vec3<f32> {
    return unitOf(v - dot(v, t) * t);
}

// The normal across a unit tangent from the axis least along it.
fn firstNormal(t: vec3<f32>) -> vec3<f32> {
    var least = 0u;
    for (var k = 1u; k < 3u; k++) {
        if (abs(t[k]) < abs(t[least])) {
            least = k;
        }
    }
    var axis = vec3<f32>(0.0);
    axis[least] = 1.0;
    return acrossOf(axis, t);
}

// v reflected in the plane across d (of squared length c).
fn reflectIn(d: vec3<f32>, c: f32, v: vec3<f32>) -> vec3<f32> {
    return v - (2.0 * dot(d, v) / c) * d;
}

// Carries a frame to the next point (here, in the particles' space) by
// double reflection (Wang, Juttler, Zheng and Liu 2008, as the core's
// mvfxAdvanceTubeFrame), the way between them in the world: the first
// point's normal across its tangent from the axis least along it; a turn
// back on itself starts afresh.
fn advanceFrame(frame: ptr<function, Frame>, here: vec3<f32>, tangent: vec3<f32>) {
    if (!(*frame).started) {
        (*frame).normal = firstNormal(tangent);
    } else {
        let d = rotate(params.turn, here - (*frame).place) / params.scales.x;
        let c1 = dot(d, d);
        var normal = (*frame).normal;
        var bent = (*frame).tangent;
        if (c1 > 0.0) {
            normal = reflectIn(d, c1, normal);
            bent = reflectIn(d, c1, bent);
        }
        let e = tangent - bent;
        let c2 = dot(e, e);
        var second = normal;
        if (c2 > 0.0) {
            second = reflectIn(e, c2, normal);
        }
        let held = acrossOf(second, tangent);
        (*frame).normal = select(firstNormal(tangent), held, dot(held, held) > 0.0);
    }
    (*frame).place = here;
    (*frame).tangent = tangent;
    (*frame).started = true;
}

// The unit way out of a frame at angle 2 pi k / sides, and the way around
// there.
fn wayAt(normal: vec3<f32>, tangent: vec3<f32>, sides: u32, k: u32) -> array<vec3<f32>, 2> {
    let turn = sinCos(6.2831853 * f32(k % sides) / f32(sides));
    let sine = turn.x;
    let cosine = turn.y;
    let binormal = cross(tangent, normal);
    return array<vec3<f32>, 2>(cosine * normal + sine * binormal,
                               -sine * normal + cosine * binormal);
}

// A tube's point g of a chain of n, its vertices from chainVertex and its
// indices from chainIndex: the frame carried to it, its ring at half its
// width about it, but for the last the quads to the next ring, at either
// end its cap after the chain's rings (the core's WriteTubePoint).
fn writeTube(chainVertex: u32, chainIndex: u32, n: u32, g: u32, a: u32, b: u32, fraction: f32,
             place: vec3<f32>, inParticles: vec3<f32>, half: f32, point: Point,
             frame: ptr<function, Frame>) {
    var vertex = point;
    let sides = ribbon.sink.y;
    advanceFrame(frame, inParticles, point.tangent);
    let normal = (*frame).normal;
    let tangent = (*frame).tangent;
    let ring = chainVertex + g * (sides + 1u);
    for (var k = 0u; k <= sides; k++) {
        let way = wayAt(normal, tangent, sides, k);
        vertex.side = way[1];
        vertex.face = way[0];
        vertex.corner = vec2<f32>(point.along, f32(k) / f32(sides));
        writePoint(ring + k, a, b, fraction, place + half * way[0], vertex);
    }
    if (g + 1u < n) {
        let at = chainIndex + g * sides * 6u;
        for (var k = 0u; k < sides; k++) {
            let low = ring + k;
            let high = ring + sides + 1u + k;
            ribbonList[at + 6u * k] = low;
            ribbonList[at + 6u * k + 1u] = low + 1u;
            ribbonList[at + 6u * k + 2u] = high;
            ribbonList[at + 6u * k + 3u] = high;
            ribbonList[at + 6u * k + 4u] = low + 1u;
            ribbonList[at + 6u * k + 5u] = high + 1u;
        }
    }
    let end = g + 1u == n;
    if (ribbon.sink.z == 0u || (g != 0u && !end)) {
        return;
    }
    // The cap: its center facing back along the tangent at the start or
    // along it at the end, then a ring, its corners a disc's.
    let cap = chainVertex + n * (sides + 1u) + select(0u, sides + 2u, end);
    vertex.face = select(-tangent, tangent, end);
    vertex.corner = vec2<f32>(0.5);
    writePoint(cap, a, b, fraction, place, vertex);
    let binormal = cross(tangent, normal);
    for (var k = 0u; k <= sides; k++) {
        let way = wayAt(normal, tangent, sides, k);
        vertex.side = way[1];
        vertex.corner = vec2<f32>(0.5 + 0.5 * dot(way[0], normal), 0.5 + 0.5 * dot(way[0], binormal));
        writePoint(cap + 1u + k, a, b, fraction, place + half * way[0], vertex);
    }
    let at = chainIndex + (n - 1u) * sides * 6u + select(0u, sides * 3u, end);
    for (var k = 0u; k < sides; k++) {
        let edge = cap + 1u + k;
        ribbonList[at + 3u * k] = cap;
        ribbonList[at + 3u * k + 1u] = select(edge + 1u, edge, end);
        ribbonList[at + 3u * k + 2u] = select(edge, edge + 1u, end);
    }
}
