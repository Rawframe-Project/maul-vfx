// Drag factors as the kernels share them (tools/gen_kernels.py splices
// them where a kernel names them): over seconds, for drag k a second and
// x = k seconds, the decay e^-x, the carry seconds phi1(x) and the push
// seconds^2 phi2(x), the core's mvfxDragFactorsOf in binary32.

// Below this x the factors come from their series: past it neither
// 1 - e^-x (at least 0.63) nor 1 - phi1 (at least 0.37) cancels in
// binary32, while below it both would.
const DRAG_SERIES_LIMIT: f32 = 1.0;

// Drag factors over seconds: decay, carry, push (the integrator's exact
// form). Below DRAG_SERIES_LIMIT, phi_n(x) is the sum over j of
// (-x)^j / (j + n)!, eleven terms by Horner's rule (the next is below
// 2^-26 of the sum at x = 1), and the decay 1 - x phi1, as the core's
// series.
fn factors(drag: f32, seconds: f32) -> vec3<f32> {
    let x = drag * seconds;
    if (x < DRAG_SERIES_LIMIT) {
        let n = -x;
        var phi1 = 1.0 / 39916800.0;
        var phi2 = 1.0 / 479001600.0;
        phi1 = phi1 * n + 1.0 / 3628800.0;
        phi2 = phi2 * n + 1.0 / 39916800.0;
        phi1 = phi1 * n + 1.0 / 362880.0;
        phi2 = phi2 * n + 1.0 / 3628800.0;
        phi1 = phi1 * n + 1.0 / 40320.0;
        phi2 = phi2 * n + 1.0 / 362880.0;
        phi1 = phi1 * n + 1.0 / 5040.0;
        phi2 = phi2 * n + 1.0 / 40320.0;
        phi1 = phi1 * n + 1.0 / 720.0;
        phi2 = phi2 * n + 1.0 / 5040.0;
        phi1 = phi1 * n + 1.0 / 120.0;
        phi2 = phi2 * n + 1.0 / 720.0;
        phi1 = phi1 * n + 1.0 / 24.0;
        phi2 = phi2 * n + 1.0 / 120.0;
        phi1 = phi1 * n + 1.0 / 6.0;
        phi2 = phi2 * n + 1.0 / 24.0;
        phi1 = phi1 * n + 0.5;
        phi2 = phi2 * n + 1.0 / 6.0;
        phi1 = phi1 * n + 1.0;
        phi2 = phi2 * n + 0.5;
        return vec3<f32>(1.0 - x * phi1, seconds * phi1, seconds * seconds * phi2);
    }
    let decay = exp(-x);
    let phi1 = (1.0 - decay) / x;
    let phi2 = (1.0 - phi1) / x;
    return vec3<f32>(decay, seconds * phi1, seconds * seconds * phi2);
}
