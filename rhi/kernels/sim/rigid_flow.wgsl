// The sim kernel's rigid bodies' free turning (spliced into sim.wgsl), as
// the core's mvfxPrincipalOf and mvfxFlowBody (src/rigid_flow.c): the
// principal frame by cyclic Jacobi rotations, and the flow of a body's
// spin and orientation by a Casimir splitting of its kinetic energy. The
// core turns in binary64; here in binary32, so each turn is held to the
// length it keeps by a first-order correction, the turns are composed
// into one before the orientation takes them, and the spin's momentum is
// held to its size as the next substep reads it back.

// The most sweeps the Jacobi rotations take, as the core's MOST_SWEEPS.
const MOST_SWEEPS: u32 = 16u;
// The residual turns' pieces, as the core's MOST_PIECES and PIECE_TURN.
const MOST_PIECES: f32 = 64.0;
const PIECE_TURN: f32 = 0.25;
// The largest half turn sinCos takes.
const MOST_HALF_TURN: f32 = 16777216.0;

// A body's principal frame, as the core's mvfxPrincipal: its moments,
// largest first, and the quaternion turning the frame into the body's.
struct Principal {
    moments: vec3<f32>,
    rotation: vec4<f32>,
}

// The sine and cosine of x, as the core's mvfxSinCos within binary32: x
// less the nearest multiple of pi/2 in parts of 12 bits (exact while that
// multiple is below 2^12, near 6400 radians), the remainder's series; past
// 2^24 or NaN, as of 0.
fn sinCos(x: f32) -> vec2<f32> {
    let value = select(0.0, x, abs(x) <= MOST_HALF_TURN);
    let k = floor(value * 0x1.45f306p-1f + 0.5);
    let r = ((value - k * 0x1.922p+0f) + k * 0x1.2aep-18f + k * 0x1.deap-31f) - k * 0x1.18p-44f;
    let r2 = r * r;
    let sine = r * (1.0 + r2 * (-1.0 / 6.0 + r2 * (1.0 / 120.0 + r2 * (-1.0 / 5040.0 +
                                                                     r2 * (1.0 / 362880.0)))));
    let cosine = 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 + r2 * (-1.0 / 720.0 +
                                                             r2 * (1.0 / 40320.0 +
                                                                   r2 * (-1.0 / 3628800.0)))));
    switch (i32(k) & 3) {
        case 0: {
            return vec2<f32>(sine, cosine);
        }
        case 1: {
            return vec2<f32>(cosine, -sine);
        }
        case 2: {
            return vec2<f32>(-sine, -cosine);
        }
        default: {
            return vec2<f32>(-cosine, sine);
        }
    }
}

// The principal frame of an inertia (xx, yy, zz, xy, xz, yz), as the
// core's mvfxPrincipalOf in binary32.
fn principalOf(inertia: array<f32, 6>) -> Principal {
    var a = array<vec3<f32>, 3>(vec3<f32>(inertia[0], inertia[3], inertia[4]),
                                vec3<f32>(inertia[3], inertia[1], inertia[5]),
                                vec3<f32>(inertia[4], inertia[5], inertia[2]));
    var v = array<vec3<f32>, 3>(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0),
                                vec3<f32>(0.0, 0.0, 1.0));
    for (var sweep = 0u; sweep < MOST_SWEEPS; sweep++) {
        if (a[0][1] == 0.0 && a[0][2] == 0.0 && a[1][2] == 0.0) {
            break;
        }
        for (var pair = 0u; pair < 3u; pair++) {
            let p = select(0u, 1u, pair == 2u);
            let q = select(2u, 1u, pair == 0u);
            if (a[p][q] == 0.0) {
                continue;
            }
            let theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
            let t = select(1.0, -1.0, theta < 0.0) / (abs(theta) + sqrt(theta * theta + 1.0));
            let c = 1.0 / sqrt(t * t + 1.0);
            let s = t * c;
            let apq = a[p][q];
            a[p][p] -= t * apq;
            a[q][q] += t * apq;
            a[p][q] = 0.0;
            a[q][p] = 0.0;
            let r = 3u - p - q;
            let arp = a[r][p];
            let arq = a[r][q];
            a[r][p] = c * arp - s * arq;
            a[p][r] = a[r][p];
            a[r][q] = s * arp + c * arq;
            a[q][r] = a[r][q];
            for (var k = 0u; k < 3u; k++) {
                let vkp = v[k][p];
                let vkq = v[k][q];
                v[k][p] = c * vkp - s * vkq;
                v[k][q] = s * vkp + c * vkq;
            }
        }
    }
    // Largest first, a tie keeping the earlier axis first.
    var order = vec3<u32>(0u, 1u, 2u);
    for (var i = 1u; i < 3u; i++) {
        for (var j = i; j > 0u && a[order[j]][order[j]] > a[order[j - 1u]][order[j - 1u]]; j--) {
            let swap = order[j];
            order[j] = order[j - 1u];
            order[j - 1u] = swap;
        }
    }
    var out = Principal();
    var axes = array<vec3<f32>, 3>();
    for (var c = 0u; c < 3u; c++) {
        out.moments[c] = a[order[c]][order[c]];
        var largest = 0u;
        for (var k = 1u; k < 3u; k++) {
            largest = select(largest, k, abs(v[k][order[c]]) > abs(v[largest][order[c]]));
        }
        let sign = select(1.0, -1.0, v[largest][order[c]] < 0.0);
        for (var k = 0u; k < 3u; k++) {
            axes[k][c] = sign * v[k][order[c]];
        }
    }
    let det = dot(vec3<f32>(axes[0][0], axes[1][0], axes[2][0]),
                  cross(vec3<f32>(axes[0][1], axes[1][1], axes[2][1]),
                        vec3<f32>(axes[0][2], axes[1][2], axes[2][2])));
    if (det < 0.0) {
        for (var k = 0u; k < 3u; k++) {
            axes[k][2] = -axes[k][2];
        }
    }
    out.rotation = quaternionOf(axes);
    return out;
}

// The unit quaternion of a rotation matrix (its columns the turned axes),
// as the core's QuaternionOf.
fn quaternionOf(m: array<vec3<f32>, 3>) -> vec4<f32> {
    let trace = m[0][0] + m[1][1] + m[2][2];
    var q = vec4<f32>(0.0);
    if (trace >= m[0][0] && trace >= m[1][1] && trace >= m[2][2]) {
        let s = 2.0 * sqrt(1.0 + trace);
        q = vec4<f32>((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s,
                      0.25 * s);
    } else if (m[0][0] >= m[1][1] && m[0][0] >= m[2][2]) {
        let s = 2.0 * sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
        q = vec4<f32>(0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s,
                      (m[2][1] - m[1][2]) / s);
    } else if (m[1][1] >= m[2][2]) {
        let s = 2.0 * sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]);
        q = vec4<f32>((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s,
                      (m[0][2] - m[2][0]) / s);
    } else {
        let s = 2.0 * sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
        q = vec4<f32>((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s,
                      (m[1][0] - m[0][1]) / s);
    }
    return q / sqrt(dot(q, q));
}

// How many equal pieces a substep's residual turns take, as the core's
// Pieces.
fn flowPieces(turn: f32, least: f32, most: f32) -> f32 {
    let a = turn * least;
    let b = turn * most;
    let reach = max(a * a * b, a * b * b) / (PIECE_TURN * PIECE_TURN * PIECE_TURN);
    var n = 1.0;
    while (n < MOST_PIECES && n * n * n < reach) {
        n += 1.0;
    }
    return n;
}

// A first-order correction of x toward the squared length kept, as maul3d
// takes it: x (1 + (kept - got) / (2 got)).
fn keepLength(x: vec2<f32>, kept: f32) -> vec2<f32> {
    let got = dot(x, x);
    return select(x, x + x * ((kept - got) / (2.0 * got)), got > 0.0);
}

// Turns T by theta about principal axis i, and the momentum L in the
// frame back by theta, its pair across i held to its length: the
// residual's exact flow. False past sinCos's range.
fn residualTurn(i: u32, theta: f32, t: ptr<function, vec4<f32>>, l: ptr<function, vec3<f32>>) -> bool {
    if (!(abs(0.5 * theta) <= MOST_HALF_TURN)) {
        return false;
    }
    let half = sinCos(0.5 * theta);
    var r = vec4<f32>(0.0, 0.0, 0.0, half.y);
    r[i] = half.x;
    *t = multiplyRotations(*t, r);
    let c = half.y * half.y - half.x * half.x;
    let s = 2.0 * half.x * half.y;
    let a = (i + 1u) % 3u;
    let b = (i + 2u) % 3u;
    let pair = vec2<f32>((*l)[a], (*l)[b]);
    let turned = keepLength(vec2<f32>(c * pair.x + s * pair.y, -s * pair.x + c * pair.y),
                            dot(pair, pair));
    (*l)[a] = turned.x;
    (*l)[b] = turned.y;
    return true;
}

// A body's orientation and angular velocity turned over t from its
// orientation q at its substep's start, as the core's Flow: by
// flowTurned, or about its angular velocity (turnBy) for a body of one
// moment about every axis, one of no volume or a turn flowTurned refuses.
fn flowBody(b: ptr<function, Body>, inertia: array<f32, 6>, q: vec4<f32>, t: f32) {
    let w = (*b).moving.turning.angular;
    var turned = Turning();
    if (flowTurned(principalOf(inertia), t, q, w, &turned)) {
        (*b).moving.turning.orientation = turned.orientation;
        (*b).moving.turning.angular = turned.angular;
    } else {
        (*b).moving.turning.orientation = turnBy(q, w * t);
    }
}

// The free flow of a body's orientation q and angular velocity w over t
// into out, as the core's mvfxFlowBody, its moments taken over the
// largest so only their ratios are used; false for a body of one moment
// about every axis or one of none, and past sinCos's or binary32's range.
fn flowTurned(p: Principal, t: f32, q: vec4<f32>, w: vec3<f32>, out: ptr<function, Turning>) -> bool {
    if (p.moments.x == p.moments.z || !(p.moments.z > 0.0)) {
        return false;
    }
    let m = p.moments / p.moments.x;
    let frame = p.rotation;
    let back = vec4<f32>(-frame.xyz, frame.w);
    let start = normalize(multiplyRotations(q, frame));
    var l = m * rotate(vec4<f32>(-start.xyz, start.w), w);
    let kept = dot(l, l);
    let least = 1.0 / m.z - 1.0 / m.y;
    let most = 1.0 / m.x - 1.0 / m.y;
    let pieces = flowPieces(t * sqrt(kept), least, -most);
    let piece = t / pieces;
    var turn = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    var done = true;
    for (var k = 0.0; done && k < pieces; k += 1.0) {
        done = residualTurn(2u, 0.5 * piece * l.z * least, &turn, &l) &&
               residualTurn(0u, piece * l.x * most, &turn, &l) &&
               residualTurn(2u, 0.5 * piece * l.z * least, &turn, &l);
    }
    let size = sqrt(dot(l, l));
    let casimir = 0.5 * t * size / m.y;
    if (!done || !(size > 0.0) || !(size <= 3.40282347e38) || !(abs(casimir) <= MOST_HALF_TURN)) {
        return false;
    }
    let half = sinCos(casimir);
    turn = multiplyRotations(turn, vec4<f32>(l / size * half.x, half.y));
    let orientation =
        normalize(multiplyRotations(q, multiplyRotations(frame, multiplyRotations(turn, back))));
    let now = normalize(multiplyRotations(orientation, frame));
    var angular = rotate(now, l / m);
    // The momentum as the next substep reads it back, held to its size.
    let read = m * rotate(vec4<f32>(-now.xyz, now.w), angular);
    let got = dot(read, read);
    angular = select(angular, angular + angular * ((kept - got) / (2.0 * got)), got > 0.0);
    if (!all(abs(angular) <= vec3<f32>(3.40282347e38)) ||
        !all(abs(orientation) <= vec4<f32>(3.40282347e38))) {
        return false;
    }
    (*out).orientation = orientation;
    (*out).angular = angular;
    return true;
}
