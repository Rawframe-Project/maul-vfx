// Exact arithmetic the kernels share (tools/gen_kernels.py splices it
// where a kernel names it): what WGSL leaves inexact, done in integers so
// that every device gives the core's binary32 words. Its division is
// within 2.5 units in the last place and an integer it cannot hold
// converts to either neighbour, so a particle's inverse life, its age and
// its death tick come from these. Counts of ticks are 64-bit, two words.

// The seconds of a tick, 2^-20: a product by it is exact.
const TICK_SECONDS: f32 = 1.0 / 1048576.0;

// A 64-bit number over 2^s, floored, s below 64.
fn shiftRight(x: vec2<u32>, s: u32) -> vec2<u32> {
    if (s == 0u) {
        return x;
    }
    if (s >= 32u) {
        return vec2<u32>(x.y >> (s - 32u), 0u);
    }
    return vec2<u32>((x.x >> s) | (x.y << (32u - s)), x.y >> s);
}

// A 64-bit number times 2^s, s below 32, its top bits beyond 64 dropped.
fn shiftLeft(x: vec2<u32>, s: u32) -> vec2<u32> {
    if (s == 0u) {
        return x;
    }
    return vec2<u32>(x.x << s, (x.y << s) | (x.x >> (32u - s)));
}

// Whether a 64-bit number has a bit set below bit b.
fn anyBelow(x: vec2<u32>, b: u32) -> bool {
    if (b == 0u) {
        return false;
    }
    if (b >= 32u) {
        return x.x != 0u || (b > 32u && (x.y & ((1u << (b - 32u)) - 1u)) != 0u);
    }
    return (x.x & ((1u << b) - 1u)) != 0u;
}

fn add64(x: vec2<u32>, y: vec2<u32>) -> vec2<u32> {
    let low = x.x + y.x;
    return vec2<u32>(low, x.y + y.y + select(0u, 1u, low < x.x));
}

fn subtract64(x: vec2<u32>, y: vec2<u32>) -> vec2<u32> {
    return vec2<u32>(x.x - y.x, x.y - y.y - select(0u, 1u, x.x < y.x));
}

fn less64(x: vec2<u32>, y: vec2<u32>) -> bool {
    return x.y < y.y || (x.y == y.y && x.x < y.x);
}

// The remainder of a 64-bit number over a divisor below 2^63, by long
// division a bit at a time.
fn remainder64(x: vec2<u32>, d: vec2<u32>) -> vec2<u32> {
    var r = vec2<u32>(0u);
    for (var b = 63; b >= 0; b--) {
        let word = select(x.x, x.y, b >= 32);
        r = shiftLeft(r, 1u) | vec2<u32>((word >> (u32(b) % 32u)) & 1u, 0u);
        if (!less64(r, d)) {
            r = subtract64(r, d);
        }
    }
    return r;
}

// n / d correctly rounded to binary32, ties to even, for n below d below
// 2^63 (64-bit both), as C's (float)((double)n / (double)d) gives it for
// numbers below 2^53 (rounding to binary64 first changes nothing: 53 bits
// are at least twice 24 and 2): n shifted up to d's binade, the quotient
// by long division to 25 bits and the remainder deciding a tie.
fn ratio(n: vec2<u32>, d: vec2<u32>) -> f32 {
    if (n.x == 0u && n.y == 0u) {
        return 0.0;
    }
    var r = n;
    var exponent = 127;
    for (var k = 0; k < 64 && less64(r, d); k++) {
        r = shiftLeft(r, 1u);
        exponent -= 1;
    }
    var q = 0u;
    for (var k = 0u; k < 25u; k++) {
        q <<= 1u;
        if (!less64(r, d)) {
            r = subtract64(r, d);
            q |= 1u;
        }
        r = shiftLeft(r, 1u);
    }
    var kept = q >> 1u;
    if ((q & 1u) == 1u && (r.x != 0u || r.y != 0u || (kept & 1u) == 1u)) {
        kept += 1u;
    }
    if (kept == 0x01000000u) {
        kept = 0x00800000u;
        exponent += 1;
    }
    return bitcast<f32>((u32(exponent) << 23u) | (kept & 0x007FFFFFu));
}

// A 64-bit count as the nearest binary32, ties to even, as C converts it.
fn nearestFloat(low: u32, high: u32) -> f32 {
    if (high == 0u && low < 0x01000000u) {
        return f32(low);
    }
    let x = vec2<u32>(low, high);
    let lead = select(31u - countLeadingZeros(low), 63u - countLeadingZeros(high), high != 0u);
    // The top 24 bits kept; the first dropped is the half, those below it
    // decide a tie.
    let drop = lead - 23u;
    var kept = shiftRight(x, drop).x;
    let half = (shiftRight(x, drop - 1u).x & 1u) == 1u;
    if (half && (anyBelow(x, drop - 1u) || (kept & 1u) == 1u)) {
        kept += 1u;
    }
    // A carry past 24 bits doubles the value.
    var exponent = lead;
    if (kept == 0x01000000u) {
        kept = 0x00800000u;
        exponent += 1u;
    }
    return bitcast<f32>(((exponent + 127u) << 23u) | (kept & 0x007FFFFFu));
}

// a / b correctly rounded for positive normal a and b whose quotient is
// below binary32's largest: the significands' quotient by long division to
// 25 bits, rounded up when the 25th is set. A quotient of two binary32
// numbers is never halfway between two normal ones, so no tie needs the
// remainder; and the significands' quotient is at most 2 - 2^-23, below
// the midpoint under 2, so the rounding never carries into the exponent.
// A quotient below binary32's least normal is rounded to 2^-149's, ties to
// even: there the remainder decides a tie.
fn divide(a: f32, b: f32) -> f32 {
    let x = bitcast<u32>(a);
    let y = bitcast<u32>(b);
    var exponent = i32(x >> 23u) - i32(y >> 23u) + 127;
    var r = (x & 0x007FFFFFu) | 0x00800000u;
    let d = (y & 0x007FFFFFu) | 0x00800000u;
    if (r < d) {
        r <<= 1u;
        exponent -= 1;
    }
    // r stays below 2 d, so below 2^25.
    var q = 0u;
    for (var k = 0u; k < 25u; k++) {
        q <<= 1u;
        if (r >= d) {
            r -= d;
            q |= 1u;
        }
        r <<= 1u;
    }
    if (exponent <= 0) {
        // q 2^(exponent - 151), in 2^-149's.
        let shift = u32(2 - exponent);
        if (shift >= 26u) {
            return 0.0;
        }
        let units = q >> shift;
        let rest = q & ((1u << shift) - 1u);
        let half = 1u << (shift - 1u);
        let up = rest > half || (rest == half && (r != 0u || (units & 1u) == 1u));
        return bitcast<f32>(units + select(0u, 1u, up));
    }
    let kept = (q >> 1u) + (q & 1u);
    return bitcast<f32>((u32(exponent) << 23u) | (kept & 0x007FFFFFu));
}

// a b correctly rounded, ties to even, for positive normal a and b whose
// product is normal: the significands' 48-bit product from 12-bit halves,
// rounded to 24 bits. In integers, so no compiler reorders a chain of
// such products (DXC's fast math reassociates float ones).
fn product(a: f32, b: f32) -> f32 {
    let x = bitcast<u32>(a);
    let y = bitcast<u32>(b);
    let p = (x & 0x007FFFFFu) | 0x00800000u;
    let q = (y & 0x007FFFFFu) | 0x00800000u;
    let middle = (p >> 12u) * (q & 0xFFFu) + (p & 0xFFFu) * (q >> 12u);
    var low = (p & 0xFFFu) * (q & 0xFFFu) + ((middle & 0xFFFu) << 12u);
    let high = (p >> 12u) * (q >> 12u) + (middle >> 12u) + (low >> 24u);
    low &= 0x00FFFFFFu;
    // The product is high 2^24 + low, from 2^46 to below 2^48.
    var exponent = i32(x >> 23u) + i32(y >> 23u) - 127;
    var kept = (high << 1u) | (low >> 23u);
    var rest = low & 0x007FFFFFu;
    var half = 0x00400000u;
    if (high >= 0x00800000u) {
        kept = high;
        rest = low;
        half = 0x00800000u;
        exponent += 1;
    }
    if (rest > half || (rest == half && (kept & 1u) == 1u)) {
        kept += 1u;
    }
    if (kept == 0x01000000u) {
        kept = 0x00800000u;
        exponent += 1;
    }
    return bitcast<f32>((u32(exponent) << 23u) | (kept & 0x007FFFFFu));
}

// a / b as the core's division gives it, for positive normal b and a
// quotient below binary32's largest: the exact division on a's magnitude,
// the sign put back; zero and subnormal a left to the device.
fn quotient(a: f32, b: f32) -> f32 {
    let magnitude = abs(a);
    if (magnitude < 1.17549435e-38) {
        return a / b;
    }
    return select(divide(magnitude, b), -divide(magnitude, b), a < 0.0);
}

// The binary32 age of a count of ticks, as the core's: the count rounded
// once, times 2^-20.
fn secondsOf(ticks: vec2<u32>) -> f32 {
    return nearestFloat(ticks.x, ticks.y) * TICK_SECONDS;
}

// The values a count of ticks rounds to, in order: every count to 2^24,
// then each binary32.
fn nextCount(f: f32) -> f32 {
    if (f < 16777216.0) {
        return f + 1.0;
    }
    return bitcast<f32>(bitcast<u32>(f) + 1u);
}

fn previousCount(f: f32) -> f32 {
    if (f <= 16777216.0) {
        return f - 1.0;
    }
    return bitcast<f32>(bitcast<u32>(f) - 1u);
}

fn reaches(f: f32, inverse: f32) -> bool {
    return f * TICK_SECONDS * inverse >= 1.0;
}

// The least count rounding to f, a value a count rounds to: past 2^24 the
// midpoint below it when a tie goes to f (its significand even), the
// count after it otherwise.
fn leastCount(f: f32) -> vec2<u32> {
    if (f < 16777216.0) {
        return vec2<u32>(u32(f), 0u);
    }
    let bits = bitcast<u32>(f);
    let m = (bits & 0x007FFFFFu) | 0x00800000u;
    let shift = (bits >> 23u) - 150u;
    let value = shiftLeft(vec2<u32>(m, 0u), shift);
    // Half the gap below f: a quarter of the gap above at a power of 2.
    var half = shift - 1u;
    if (m == 0x00800000u) {
        if (shift == 1u) {
            return value;
        }
        half = shift - 2u;
    }
    let least = subtract64(value, shiftLeft(vec2<u32>(1u, 0u), half));
    return add64(least, vec2<u32>(bits & 1u, 0u));
}

// The ticks a particle at inverse life inverse lives, as the core's
// mvfxDeathTick: the least count of at least 1 whose age times the
// inverse reaches 1. The age only changes where the count's binary32
// does, so the search walks those values from an estimate (a few steps:
// WGSL's division is within 2.5 units in the last place), then takes the
// least count rounding to the value found.
fn lifeTicks(inverse: f32) -> vec2<u32> {
    return lifeTicksFrom(inverse, max(ceil(1048576.0 / inverse), 1.0));
}

// lifeTicks from a start f, a value a count rounds to.
fn lifeTicksFrom(inverse: f32, start: f32) -> vec2<u32> {
    var f = start;
    for (var n = 0; n < 64 && f > 1.0 && reaches(previousCount(f), inverse); n++) {
        f = previousCount(f);
    }
    for (var n = 0; n < 64 && !reaches(f, inverse); n++) {
        f = nextCount(f);
    }
    return leastCount(f);
}
