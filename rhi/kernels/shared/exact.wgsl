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

// a / b correctly rounded, ties to even, for positive normal a and b whose
// quotient is normal: the significands' quotient by long division, 25
// bits and the remainder deciding the rounding.
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
    var kept = q >> 1u;
    if ((q & 1u) == 1u && (r != 0u || (kept & 1u) == 1u)) {
        kept += 1u;
    }
    if (kept == 0x01000000u) {
        kept = 0x00800000u;
        exponent += 1;
    }
    return bitcast<f32>((u32(exponent) << 23u) | (kept & 0x007FFFFFu));
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
// does, so the search walks those values from an estimate (a few steps),
// then takes the least count rounding to the value found.
fn lifeTicks(inverse: f32) -> vec2<u32> {
    var f = max(ceil(1048576.0 / inverse), 1.0);
    for (var n = 0; n < 64 && f > 1.0 && reaches(previousCount(f), inverse); n++) {
        f = previousCount(f);
    }
    for (var n = 0; n < 64 && !reaches(f, inverse); n++) {
        f = nextCount(f);
    }
    return leastCount(f);
}
