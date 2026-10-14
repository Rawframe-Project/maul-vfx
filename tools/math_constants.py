#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The constants of src/elementary.c, derived exactly: pi by Machin's formula and
# ln 2 by its atanh series, to 60 digits in exact rationals, then split
# for Cody and Waite's argument reduction (each high part with enough
# trailing zero bits that its product with any reduction multiple the
# library's domain allows is exact in binary64), and the reciprocal
# factorials of the Taylor series as correctly rounded binary64.
#
# usage: math_constants.py

from fractions import Fraction


def arctan_inverse(n, terms=80):
    # atan(1/n) as an exact rational partial sum.
    total = Fraction(0)
    for k in range(terms):
        total += Fraction((-1) ** k, (2 * k + 1) * n ** (2 * k + 1))
    return total


PI = 16 * arctan_inverse(5) - 4 * arctan_inverse(239)
# ln 2 = 2 atanh(1/3).
LN2 = 2 * sum(Fraction(1, (2 * k + 1) * 3 ** (2 * k + 1)) for k in range(120))


def truncated(value, bits):
    # value cut to its leading `bits` significant bits.
    exponent = 0
    scaled = value
    while scaled >= 2:
        scaled /= 2
        exponent += 1
    while scaled < 1:
        scaled *= 2
        exponent -= 1
    mantissa = int(scaled * 2 ** (bits - 1))
    return Fraction(mantissa, 1) * Fraction(2) ** (exponent - bits + 1)


def split(value, bits):
    # Three parts: two of `bits` bits, the rest to binary64.
    high = truncated(value, bits)
    middle = truncated(value - high, bits)
    low = value - high - middle
    return high, middle, low


def show(name, value):
    print("#define %s %s" % (name, float(value).hex()))


# Reduction by pi/2 of floats up to 2^24 in size: multiples below 2^24,
# so 29-bit parts keep each product exact.
c1, c2, c3 = split(PI / 2, 29)
show("HALF_PI_1", c1)
show("HALF_PI_2", c2)
show("HALF_PI_3", c3)
show("TWO_OVER_PI", 2 / PI)
# Reduction by ln 2 of exponents up to 2^11 in size: 42-bit high part.
l1, l2, _ = split(LN2, 42)
show("LN2_1", l1)
show("LN2_2", LN2 - l1)
show("LOG2_E", 1 / LN2)
show("LN2", LN2)
for n in range(2, 18):
    factorial = 1
    for i in range(2, n + 1):
        factorial *= i
    show("INVERSE_FACTORIAL_%d" % n, Fraction(1, factorial))
