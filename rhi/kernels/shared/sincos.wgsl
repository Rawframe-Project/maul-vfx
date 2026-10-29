// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The sine and cosine as the core's mvfxSinCos (src/elementary.c), without
// bindings, spliced where a kernel names it, after shared/finite.wgsl.
// WGSL bounds its own sin and cos only on [-pi, pi], and loosely there;
// these reduce every finite angle exactly, in integers, and keep within a
// few units in the last place of the core's.

// 2/pi's fraction in 32-bit words, as the core's TWO_OVER_PI_WORDS: bits 1
// to 256, bit 1 weighing 1/2.
const TWO_OVER_PI_WORDS = array<u32, 8>(0xA2F9836Eu, 0x4E441529u, 0xFC2757D1u, 0xF534DDC0u,
                                        0xDB629599u, 0x3C439041u, 0xFE5163ABu, 0xDEBBC561u);
// pi/2, and the angle below which an angle is its own remainder.
const HALF_PI: f32 = 1.57079632679489662;
const QUARTER_PI: f32 = 0.785398163397448310;

// An angle less its nearest multiple of pi/2, and that multiple's low two
// bits.
struct Reduced {
    r: f32,
    quadrant: u32,
}

// 32 bits of 2/pi's fraction from bit first on, first from -31 (the bits
// at 0 and above, its integer part, are 0), as the core's TwoOverPiBits.
fn twoOverPiBits(first: i32) -> u32 {
    let place = u32(first + 31);
    let word = place / 32u;
    let shift = place % 32u;
    var words = TWO_OVER_PI_WORDS;
    let high = select(words[max(word, 1u) - 1u], 0u, word == 0u);
    let low = words[min(word, 7u)];
    if (shift == 0u) {
        return high;
    }
    return (high << shift) | (low >> (32u - shift));
}

// a times b in full, its low word and its high one, from 16-bit halves:
// WGSL has no 64-bit integers.
fn productWide(a: u32, b: u32) -> vec2<u32> {
    let a0 = a & 0xFFFFu;
    let a1 = a >> 16u;
    let b0 = b & 0xFFFFu;
    let b1 = b >> 16u;
    let p00 = a0 * b0;
    let p01 = a0 * b1;
    let p10 = a1 * b0;
    let middle = (p00 >> 16u) + (p01 & 0xFFFFu) + (p10 & 0xFFFFu);
    let low = (middle << 16u) | (p00 & 0xFFFFu);
    let high = a1 * b1 + (p01 >> 16u) + (p10 >> 16u) + (middle >> 16u);
    return vec2<u32>(low, high);
}

// x, finite and at least pi/4, an integer m 2^e (m below 2^24), less its
// nearest multiple of pi/2: Payne and Hanek's reduction, as the core's
// ReducedPast. 96 of 2/pi's bits from bit e - 1 on (those above give
// multiples of 4) times m in 32-bit limbs put x 2/pi within 2^-71 quarter
// turns, where the core's 128 serve binary64; the remainder's leading 64
// bits keep at least 34 significant, as no float's is below 2^-29.9 of a
// quarter turn. The core takes Cody and Waite's reduction below 2^24,
// exact there too; binary32 has no room for it, so this one serves every
// angle.
fn reducedPast(x: f32) -> Reduced {
    let bits = bitcast<u32>(x);
    let m = (bits & 0x7FFFFFu) | 0x800000u;
    let e = i32((bits >> 23u) & 0xFFu) - 150;
    // The product's limbs, most significant first: 2^-94 of x 2/pi less a
    // multiple of 4.
    var limbs: array<u32, 4>;
    var carry = 0u;
    for (var k = 2; k >= 0; k--) {
        let product = productWide(m, twoOverPiBits(e - 1 + 32 * k));
        let low = product.x + carry;
        limbs[k + 1] = low;
        carry = product.y + select(0u, 1u, low < carry);
    }
    limbs[0] = carry;
    // Bits 94 and 95 count quarter turns; below them the fraction's
    // leading 64 bits.
    let turns = limbs[1] >> 30u;
    var high = ((limbs[1] & 0x3FFFFFFFu) << 2u) | (limbs[2] >> 30u);
    var low = (limbs[2] << 2u) | (limbs[3] >> 30u);
    // Past half a quarter turn, the next multiple is nearer: the fraction
    // less one, negated.
    let next = high >= 0x80000000u;
    if (next) {
        low = ~low + 1u;
        high = ~high + select(0u, 1u, low == 0u);
    }
    let part = f32(high) * 0x1p-32f + f32(low) * 0x1p-64f;
    return Reduced(select(part, -part, next) * HALF_PI, (turns + select(0u, 1u, next)) & 3u);
}

// The sine and cosine of x, as the core's mvfxSinCos within a few units in
// the last place: the remainder's series, each term below 2^-24 of the
// result past the last. A NaN or an infinity gives those of 0.
fn sinCos(x: f32) -> vec2<f32> {
    var reduced = Reduced(0.0, 0u);
    if (isFinite(x)) {
        reduced = Reduced(x, 0u);
        if (abs(x) >= QUARTER_PI) {
            // By its size: -x turns back as x does.
            reduced = reducedPast(abs(x));
            if (x < 0.0) {
                reduced = Reduced(-reduced.r, (4u - reduced.quadrant) & 3u);
            }
        }
    }
    let r = reduced.r;
    let r2 = r * r;
    let sine = r * (1.0 + r2 * (-1.0 / 6.0 + r2 * (1.0 / 120.0 + r2 * (-1.0 / 5040.0 +
                                                                     r2 * (1.0 / 362880.0)))));
    let cosine = 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 + r2 * (-1.0 / 720.0 +
                                                             r2 * (1.0 / 40320.0 +
                                                                   r2 * (-1.0 / 3628800.0)))));
    switch (reduced.quadrant) {
        case 0u: {
            return vec2<f32>(sine, cosine);
        }
        case 1u: {
            return vec2<f32>(cosine, -sine);
        }
        case 2u: {
            return vec2<f32>(-sine, -cosine);
        }
        default: {
            return vec2<f32>(-cosine, sine);
        }
    }
}
