// The sim kernel's rigid bodies' free turning (spliced into sim.wgsl): the
// principal frame by cyclic Jacobi rotations, as the core's
// mvfxPrincipalOf (src/rigid_flow.c), and the flow of a body's spin and
// orientation by a Casimir splitting of its kinetic energy, as maul3d's
// docs/internals/rotation.md takes it in binary32 (the core turns in
// binary64): each operation in its order, its residual turns held to the
// length they keep, composed before the orientation takes them, and the
// spin corrected by what the next substep would miss reading it back,
// component by component. Its sine and cosine are sinCos's (the shader
// has no remainderf), made a unit pair as the doc's cossin.

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
// its w not negative, as the core's QuaternionOf.
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
    return select(1.0, -1.0, q.w < 0.0) * q / sqrt(dot(q, q));
}

// The cosine and sine of a, as rotation.md's cossin: sinCos's pair
// scaled to a unit one.
fn cosSin(a: f32) -> vec2<f32> {
    let sc = sinCos(a);
    let m = sqrt(sc.x * sc.x + sc.y * sc.y);
    let i = select(0.0, 1.0 / m, m > 0.0);
    return vec2<f32>(sc.y * i, sc.x * i);
}

// A unit quaternion, or the identity for one of length below binary32's
// epsilon, as rotation.md's normalize.
fn unitOf(q: vec4<f32>) -> vec4<f32> {
    let n = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1.19209290e-7) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let i = 1.0 / n;
    return vec4<f32>(i * q.x, i * q.y, i * q.z, i * q.w);
}

// v turned by a unit quaternion, as rotation.md's rotate.
fn turnVector(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let u = q.xyz;
    let c = cross(u, v);
    let t = vec3<f32>(2.0 * c.x, 2.0 * c.y, 2.0 * c.z);
    return (v + q.w * t) + cross(u, t);
}

// A turn by angle about a unit axis e, as rotation.md's turn.
fn turnOn(e: vec3<f32>, angle: f32) -> vec4<f32> {
    let cs = cosSin(0.5 * angle);
    return vec4<f32>(cs.y * e.x, cs.y * e.y, cs.y * e.z, cs.x);
}

// How many equal pieces a substep's residual turns take, as rotation.md's
// count: the first n of 1 to 63 whose quarter cubed reaches m, else 64.
fn flowPieces(m: f32) -> f32 {
    for (var n = 1u; n < 64u; n++) {
        let r = f32(n) * PIECE_TURN;
        if (!(m > r * r * r)) {
            return f32(n);
        }
    }
    return MOST_PIECES;
}

// rotation.md's residual(i, inv, tau): T turned by the residual's angle
// about principal axis i, and the momentum's pair across i turned back,
// held to its length.
fn residualTurn(i: u32, inv: f32, invMid: f32, tau: f32, t: ptr<function, vec4<f32>>,
                l: ptr<function, vec3<f32>>) {
    let angle = tau * (*l)[i] * (inv - invMid);
    let half = cosSin(0.5 * angle);
    *t = multiplyRotations(*t, vec4<f32>(select(0.0, half.y, i == 0u), 0.0,
                                         select(0.0, half.y, i == 2u), half.x));
    let c = half.x * half.x - half.y * half.y;
    let s = 2.0 * half.y * half.x;
    let j = (i + 1u) % 3u;
    let k = (i + 2u) % 3u;
    let lj = (*l)[j];
    let lk = (*l)[k];
    var nj = c * lj + s * lk;
    var nk = c * lk - s * lj;
    let before = lj * lj + lk * lk;
    let after = nj * nj + nk * nk;
    let d = 0.5 * (before - after) / after;
    if (d - d == 0.0) {
        nj = nj + d * nj;
        nk = nk + d * nk;
    }
    (*l)[j] = nj;
    (*l)[k] = nk;
}

// A body's orientation and angular velocity turned over t from its
// orientation q at its substep's start, as the core's Flow: by
// flowTurned, or about its angular velocity (turnBy) for a body of one
// moment about every axis, or one the flow does not apply to (one of no
// volume).
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

// The free flow of a body's orientation q and angular velocity w over h
// into out, as rotation.md's flow from its scale on; false for a body of
// one moment about every axis and one it does not apply to (its least
// moment, or its least over its largest, below binary32's least normal).
fn flowTurned(p: Principal, h: f32, q: vec4<f32>, w: vec3<f32>, out: ptr<function, Turning>) -> bool {
    let I = p.moments;
    if (!(I.x - I.x == 0.0 && I.z >= 1.17549435e-38 && I.z / I.x >= 1.17549435e-38)) {
        return false;
    }
    let S = vec3<f32>(1.0, I.y / I.x, I.z / I.x);
    if (S.y == 1.0 && S.z == 1.0) {
        return false;
    }
    let F = p.rotation;
    let invMid = 1.0 / S.y;
    let invMin = 1.0 / S.z;
    let invMax = 1.0;
    let start = unitOf(multiplyRotations(q, F));
    let pw = turnVector(vec4<f32>(-start.xyz, start.w), w);
    var L = vec3<f32>(pw.x, S.y * pw.y, S.z * pw.z);
    var len = sqrt(dot(L, L));
    let a = h * len * (invMin - invMid);
    let b = h * len * (invMid - invMax);
    let m = select(a * b * b, a * a * b, a * a * b > a * b * b);
    let n = flowPieces(m);
    let t = h / n;
    var T = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    for (var k = 0.0; k < n; k += 1.0) {
        residualTurn(2u, invMin, invMid, 0.5 * t, &T, &L);
        residualTurn(0u, invMax, invMid, t, &T, &L);
        residualTurn(2u, invMin, invMid, 0.5 * t, &T, &L);
    }
    len = sqrt(dot(L, L));
    if (len > 0.0) {
        T = multiplyRotations(T, turnOn((1.0 / len) * L, h * len * invMid));
    }
    let back = vec4<f32>(-F.xyz, F.w);
    let orientation = unitOf(multiplyRotations(q, multiplyRotations(multiplyRotations(F, T), back)));
    let P = unitOf(multiplyRotations(orientation, F));
    let spin = vec3<f32>(L.x, L.y / S.y, L.z / S.z);
    let o = turnVector(P, spin);
    let miss = spin - turnVector(vec4<f32>(-P.xyz, P.w), o);
    let angular = o + turnVector(P, miss);
    if (!all(abs(angular) <= vec3<f32>(3.40282347e38))) {
        return false;
    }
    (*out).orientation = orientation;
    (*out).angular = angular;
    return true;
}
