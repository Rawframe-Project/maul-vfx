// The sim kernel's beam (spliced into sim.wgsl): an emitter's beam placed
// at the step's end as the core's mvfxPlaceBeam places it, after scatter
// and ribbons and before measure, in one workgroup: point i of the
// emitter's order at i / segments along the curve (its points as the plan
// resolved them) plus the noise across the chord, faded by sin(pi t),
// into the particles' space, at rest. Each invocation places a run of the
// points, each gap to the point before kept in the distances' words,
// which the first invocation then sums in order as the core does (a
// thousand additions cost less than a scan's barriers). The blocks'
// boxes become the beam's for measure: the whole box in the first
// block's, the others empty, the growth by size as scatter left it.

const PI: f32 = 3.14159265;

// The beam's noise at a fraction t along it, before the chord's part is
// taken out: mvfxGradientNoise3's value along the beam and over time (the
// time's cell and fraction as the plan resolved them), summed over the
// corners in its operations and order.
fn beamNoise(t: f32, seed: u32) -> vec3<f32> {
    let x = t * params.beamNoise.y;
    let below = floor(x);
    let cell = vec3<u32>(u32(below), params.beam.z, 0u);
    let f = vec3<f32>(x - below, params.beamNoise.z, 0.0);
    let w1 = fade(f);
    var value = vec3<f32>(0.0);
    for (var corner = 0u; corner < 8u; corner++) {
        let i = corner & 1u;
        let j = (corner >> 1u) & 1u;
        let k = (corner >> 2u) & 1u;
        let h = pcg4d(vec4<u32>(cell.x + i, cell.y + j, cell.z + k, seed));
        let d = f - vec3<f32>(f32(i), f32(j), f32(k));
        let wx = select(1.0 - w1.x, w1.x, i != 0u);
        let wy = select(1.0 - w1.y, w1.y, j != 0u);
        let wz = select(1.0 - w1.z, w1.z, k != 0u);
        let weight = wx * wy * wz;
        let a = GRADIENTS[h.x >> 28u];
        let b = GRADIENTS[h.y >> 28u];
        let c = GRADIENTS[h.z >> 28u];
        value.x += weight * (a.x * d.x + a.y * d.y + a.z * d.z);
        value.y += weight * (b.x * d.x + b.y * d.y + b.z * d.z);
        value.z += weight * (c.x * d.x + c.y * d.y + c.z * d.z);
    }
    return value;
}

// The curve's place a fraction t along it: a line, or a quadratic or
// cubic Bezier curve through the controls.
fn beamCurveAt(t: f32) -> vec3<f32> {
    let u = 1.0 - t;
    let p0 = params.beamPoints[0].xyz;
    let p1 = params.beamPoints[1].xyz;
    let p2 = params.beamPoints[2].xyz;
    let p3 = params.beamPoints[3].xyz;
    switch (params.beam.y) {
        case 0u: {
            return u * p0 + t * p1;
        }
        case 1u: {
            return u * u * p0 + 2.0 * u * t * p1 + t * t * p2;
        }
        default: {
            return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
        }
    }
}

// The unit chord from the start to the end; 0 where they meet.
fn beamChord() -> vec3<f32> {
    let d = params.beamPoints[params.beam.y + 1u].xyz - params.beamPoints[0].xyz;
    let total = sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return select(vec3<f32>(0.0), d / total, total > 0.0);
}

// Point i's place in the particles' space: on the curve with the noise
// across the chord, as it is for an emitter in the instance's space,
// through the instance's transform at the plan's end less the anchor for
// one in the world.
fn beamPlace(i: u32, seed: u32, chord: vec3<f32>) -> vec3<f32> {
    let t = f32(i) / f32(params.beam.x);
    let value = beamNoise(t, seed);
    let along = value.x * chord.x + value.y * chord.y + value.z * chord.z;
    let point = beamCurveAt(t) + (value - along * chord) * params.beamNoise.x * sinCos(PI * t).x;
    if (params.start.w != 0u) {
        return point;
    }
    return params.toOrigin.xyz + rotate(params.toRotation, point * params.toOrigin.w);
}

// Beam, in one workgroup, for an emitter with a beam.
@compute @workgroup_size(256, 1, 1)
fn beam(@builtin(local_invocation_index) local: u32) {
    if (local == 0u) {
        liveCount = counts[0];
    }
    let n = workgroupUniformLoad(&liveCount);
    if (n == 0u) {
        return;
    }
    let c = params.counts.x;
    // The strike's noise: the seed of its first point.
    let seed = params.counts.w ^ (moved[params.words.serial * c] * 0x9E3779B9u);
    let chord = beamChord();
    let distances = params.words.ribbonDistance;
    let per = (n + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, n);
    let hi = min(lo + per, n);
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    var before = vec3<f32>(0.0);
    if (lo > 0u && lo < hi) {
        before = beamPlace(lo - 1u, seed, chord);
    }
    for (var i = lo; i < hi; i++) {
        let p = beamPlace(i, seed, chord);
        for (var k = 0u; k < 3u; k++) {
            moved[(params.words.place + k) * c + i] = bitcast<u32>(p[k]);
            moved[(params.words.velocity + k) * c + i] = 0u;
        }
        if (distances != NONE) {
            let d = select(vec3<f32>(0.0), p - before, i > 0u);
            moved[distances * c + i] = bitcast<u32>(sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
        }
        before = p;
        low = lowerOf(low, p);
        high = higherOf(high, p);
    }
    storageBarrier();
    if (local == 0u && distances != NONE) {
        var total = 0.0;
        for (var i = 1u; i < n; i++) {
            total += bitcast<f32>(moved[distances * c + i]);
            moved[distances * c + i] = bitcast<u32>(total);
        }
        moved[distances * c] = 0u;
    }
    reduceBox(local, low, high, 0.0);
    let blockTotal = blockCount();
    for (var g = local; g < blockTotal; g += CHUNK) {
        let box = blockTotal + 6u * g;
        for (var k = 0u; k < 3u; k++) {
            blocks[box + k] = bitcast<u32>(select(EMPTY_LOW, boxLow[0][k], g == 0u));
            blocks[box + 3u + k] = bitcast<u32>(select(-EMPTY_LOW, boxHigh[0][k], g == 0u));
        }
    }
}
