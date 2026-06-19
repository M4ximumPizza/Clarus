#ifndef FIXED_ARITHMETIC_H
#define FIXED_ARITHMETIC_H

/*
 * fixed_arithmetic.h
 * ------------------
 * This header implements a small but complete set of Q-format types and
 * arithmetic operations with careful attention to:
 *  - defined/portable integer sizes (int16_t/int32_t/int64_t)
 *  - rounding-to-nearest with sign-awareness (no bias)
 *  - saturation (clamping) on overflow instead of undefined behavior
 *  - safe pre-checks to avoid undefined behavior when shifting signed
 *    negative values
 *  - simple division interfaces returning success/failure rather than
 *    raising exceptions or invoking undefined behavior on divide-by-zero
 *
 * Q-format summary (type == integer bitwidth; fractional == number of
 * fractional bits):
 *  - q16_16_t : 32-bit signed, 16 integer bits, 16 fractional bits
 *  - q8_24_t  : 32-bit signed,  8 integer bits, 24 fractional bits
 *  - q24_8_t  : 32-bit signed, 24 integer bits,  8 fractional bits
 *  - q0_31_t  : 32-bit signed,  0 integer bits, 31 fractional bits
 *  - q1_15_t  : 16-bit signed,  1 integer bit, 15 fractional bits
 *  - q4_12_t  : 16-bit signed,  4 integer bits, 12 fractional bits
 *  - q32_32_t : 64-bit signed, 32 integer bits, 32 fractional bits
 *
 * Shifting constants
 * ------------------
 * Each format has a corresponding SHIFT constant (number of fractional
 * bits). Example: to convert a Q16.16 product back into Q16.16, the
 * code shifts right by Q16_SHIFT (16).
 *
 * Conversion functions (float/double <-> fixed)
 * ---------------------------------------------
 *  - flt_to_q16/8/24/0_31, flt_to_q1/4: Convert 32-bit float to
 *    various fixed-point formats. They:
 *      * Pre-check and clamp to the representable range to avoid
 *        overflow during rounding and casting.
 *      * Use double for intermediate multiplication to reduce rounding
 *        error when converting from float.
 *      * Use sign-aware rounding-to-nearest (add/subtract 0.5
 *        depending on sign) before casting to integer.
 *    Contract:
 *      Input: IEEE-754 float32 (caller responsibility to provide a
 *             finite value). Output: clamped fixed-point integer.
 *      Errors: NaN/Inf will be handled by usual floating operations and
 *              end up clamped if large; no separate error code is
 *              returned.
 *
 *  - dbl_to_q32: Convert double to q32_32_t using long double for
 *    intermediate precision and clamping. Same semantics as above.
 *
 *  - q*_to_flt / q32_to_dbl: Convert fixed back to float/double by
 *    division by 2^fractional_bits. These are exact up to the target
 *    float/double precision limits.
 *
 * Arithmetic functions
 * --------------------
 * For each Q-format we provide add/sub/mul/div. General semantics:
 *  - add/sub: Performed in a wider signed integer (to detect and
 *    saturate on overflow) then clamped to the narrow target type.
 *
 *  - mul: Multiply in a wider integer (int64 or __int128 for q32_32)
 *    to preserve the full product. Before shifting the product down by
 *    fractional bits the implementation checks safe "pre-shift"
 *    bounds to decide saturation without shifting negative values
 *    (avoid undefined behavior). Rounding-to-nearest is applied in a
 *    sign-aware way.
 *
 *  - div: Division returns a boolean success flag and writes the result
 *    via an out parameter. On divide-by-zero the function returns false
 *    and does not write to the result. The function uses a scaled
 *    numerator (left-shifted by fractional bits) and applies sign-aware
 *    rounding before dividing by the denominator. Results are clamped
 *    to the target range when necessary.
 *
 * Rounding behavior
 * ------------------
 * Rounding-to-nearest is implemented consistently for both positive
 * and negative values. The implementation either:
 *  - shifts a product after adding/subtracting half (sign-aware) or
 *  - computes absolute value, adds half, then reapplies the original
 *    sign (used to avoid asymmetry with two's complement when adding
 *    half directly to a negative number).
 *
 * Saturation
 * ----------
 * On overflow the functions saturate to INT*_MAX or INT*_MIN for the
 * underlying storage width. The header also defines "pre-shift" bound
 * constants (e.g. Q16_MAX_PRE_SHIFT) used to quickly detect cases where
 * a product must be saturated before shifting.
 *
 * Safety and portability notes
 * ----------------------------
 *  - The header uses only standard integer types (stdint.h) and should
 *    work on any platform where these types match the expected widths.
 *  - q32_32_t multiplication/division uses GCC/Clang's __int128 for
 *    128-bit intermediates; compilers that don't provide __int128 will
 *    fail to compile these functions. If you need strict portability to
 *    compilers without __int128, replace the q32_32 functions with a
 *    fallback implementation or guard them with feature-detection
 *    macros.
 *  - The header avoids undefined behavior by never left-shifting
 *    negative values directly and by working with larger signed
 *    intermediates when necessary.
 *
 * @author M4ximumpizza
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

/* ==========================================
 * MISRA-C Float Type Aliases
 * ========================================== */
typedef float  float32_t;
typedef double float64_t;

/* ==========================================
 * Type Definitions
 * ========================================== */
typedef int32_t q16_16_t;
typedef int32_t q8_24_t;
typedef int32_t q24_8_t;
typedef int32_t q0_31_t;

typedef int16_t q1_15_t;
typedef int16_t q4_12_t;

typedef int64_t q32_32_t;

/* ==========================================
 * Shifting Constants (Defined as Unsigned)
 * ========================================== */
#define Q16_SHIFT   16U
#define Q8_SHIFT    24U
#define Q24_SHIFT   8U
#define Q0_31_SHIFT 31U

#define Q1_SHIFT    15U
#define Q4_SHIFT    12U

#define Q32_SHIFT   32U

/* ==========================================
 * Type Safe Conversion Functions (with clamping)
 * ========================================== */
static inline q16_16_t flt_to_q16(float32_t f) {
    /* Pre-check bounds to avoid overflow when rounding */
    const double max_allowed = (double)INT32_MAX / 65536.0;
    const double min_allowed = (double)INT32_MIN / 65536.0;
    if (f >= max_allowed) return INT32_MAX;
    if (f <= min_allowed) return INT32_MIN;
    double tmp = (double)f * 65536.0; /* use double to reduce rounding error */
    /* sign-aware rounding to nearest integer */
    if (tmp >= 0.0) tmp += 0.5; else tmp -= 0.5;
    return (q16_16_t)tmp;
}
static inline q8_24_t  flt_to_q8(float32_t f)  {
    const double max_allowed = (double)INT32_MAX / 16777216.0;
    const double min_allowed = (double)INT32_MIN / 16777216.0;
    if (f >= max_allowed) return INT32_MAX;
    if (f <= min_allowed) return INT32_MIN;
    double tmp = (double)f * 16777216.0;
    if (tmp >= 0.0) tmp += 0.5; else tmp -= 0.5;
    return (q8_24_t)tmp;
}
static inline q24_8_t  flt_to_q24(float32_t f) {
    const double max_allowed = (double)INT32_MAX / 256.0;
    const double min_allowed = (double)INT32_MIN / 256.0;
    if (f >= max_allowed) return INT32_MAX;
    if (f <= min_allowed) return INT32_MIN;
    double tmp = (double)f * 256.0;
    if (tmp >= 0.0) tmp += 0.5; else tmp -= 0.5;
    return (q24_8_t)tmp;
}
static inline q0_31_t  flt_to_q0_31(float32_t f) {
    const double max_allowed = (double)INT32_MAX / 2147483648.0; /* 2^31 */
    const double min_allowed = (double)INT32_MIN / 2147483648.0;
    if (f >= max_allowed) return INT32_MAX;
    if (f <= min_allowed) return INT32_MIN;
    double tmp = (double)f * 2147483648.0; /* 2^31 */
    if (tmp >= 0.0) tmp += 0.5; else tmp -= 0.5;
    return (q0_31_t)tmp;
}
static inline q1_15_t  flt_to_q1(float32_t f)  {
    const double max_allowed = (double)INT16_MAX / 32768.0;
    const double min_allowed = (double)INT16_MIN / 32768.0;
    if (f >= max_allowed) return INT16_MAX;
    if (f <= min_allowed) return INT16_MIN;
    double tmp = (double)f * 32768.0;
    if (tmp >= 0.0) tmp += 0.5; else tmp -= 0.5;
    return (q1_15_t)tmp;
}
static inline q4_12_t  flt_to_q4(float32_t f)  {
    const double max_allowed = (double)INT16_MAX / 4096.0;
    const double min_allowed = (double)INT16_MIN / 4096.0;
    if (f >= max_allowed) return INT16_MAX;
    if (f <= min_allowed) return INT16_MIN;
    double tmp = (double)f * 4096.0;
    if (tmp >= 0.0) tmp += 0.5; else tmp -= 0.5;
    return (q4_12_t)tmp;
}
static inline q32_32_t dbl_to_q32(float64_t d) {
    const long double max_allowed = (long double)INT64_MAX / 4294967296.0L;
    const long double min_allowed = (long double)INT64_MIN / 4294967296.0L;
    if (d >= (double)max_allowed) return INT64_MAX;
    if (d <= (double)min_allowed) return INT64_MIN;
    long double tmp = (long double)d * 4294967296.0L; /* 2^32 */
    if (tmp >= 0.0L) tmp += 0.5L; else tmp -= 0.5L;
    return (q32_32_t)tmp;
}

static inline float32_t q16_to_flt(q16_16_t q) { return (float32_t)q / 65536.0f; }
static inline float32_t q8_to_flt(q8_24_t q)   { return (float32_t)q / 16777216.0f; }
static inline float32_t q24_to_flt(q24_8_t q)  { return (float32_t)q / 256.0f; }
static inline float32_t q0_31_to_flt(q0_31_t q) { return (float32_t)q / 2147483648.0f; }
static inline float32_t q1_to_flt(q1_15_t q)   { return (float32_t)q / 32768.0f; }
static inline float32_t q4_to_flt(q4_12_t q)   { return (float32_t)q / 4096.0f; }
static inline float64_t q32_to_dbl(q32_32_t q) { return (float64_t)q / 4294967296.0; }

/* ==========================================
 * Pre-computed Overflow Bounds (No Undefined Behavior)
 * ========================================== */
#define Q16_MAX_PRE_SHIFT  (2147483647LL / 65536LL)      /* 32767 */
#define Q16_MIN_PRE_SHIFT  (-2147483648LL / 65536LL)     /* -32768 */

#define Q8_MAX_PRE_SHIFT   (2147483647LL / 16777216LL)   /* 127 */
#define Q8_MIN_PRE_SHIFT   (-2147483648LL / 16777216LL)  /* -128 */

#define Q24_MAX_PRE_SHIFT  (2147483647LL / 256LL)        /* 8388607 */
#define Q24_MIN_PRE_SHIFT  (-2147483648LL / 256LL)       /* -8388608 */

#define Q0_31_MAX_PRE_SHIFT  1LL                         /* normalized */
#define Q0_31_MIN_PRE_SHIFT  -1LL                        /* normalized */

#define Q1_MAX_PRE_SHIFT   (32767LL / 32768LL)           /* 0 (almost 1.0) */
#define Q1_MIN_PRE_SHIFT   (-32768LL / 32768LL)          /* -1 */

#define Q4_MAX_PRE_SHIFT   (32767LL / 4096LL)            /* 7 */
#define Q4_MIN_PRE_SHIFT   (-32768LL / 4096LL)           /* -8 */

/* Helper: sign-aware rounding and safe clamp after shifting */
static inline int64_t shift_right_with_round_clamp_32_from_64(int64_t product, unsigned shift) {
    /* Rounding-to-nearest implemented via absolute-value to avoid
       asymmetry when adding +/- half to negative numbers and then
       performing an arithmetic right shift. This produces consistent
       nearest rounding for both signs. */
    int64_t half = 1LL << (shift - 1U);
    int64_t abs_prod = (product >= 0) ? product : -product;
    int64_t rounded = (abs_prod + half) >> shift;
    int64_t result = (product >= 0) ? rounded : -rounded;
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return result;
}

static inline int32_t shift_right_with_round_clamp_16_from_32(int32_t product, unsigned shift) {
    /* Use absolute-value based rounding to nearest to avoid bias. */
    int32_t half = 1U << (shift - 1U);
    int32_t abs_prod = (product >= 0) ? product : -product;
    int32_t rounded = (abs_prod + half) >> shift;
    int32_t result = (product >= 0) ? rounded : -rounded;
    if (result > INT16_MAX) return INT16_MAX;
    if (result < INT16_MIN) return INT16_MIN;
    return result;
}

/* ==========================================
 * Q16.16 Functions (With Bounds Saturation)
 * ========================================== */
static inline q16_16_t q16_add(q16_16_t a, q16_16_t b) {
    int64_t sum = (int64_t)a + (int64_t)b;
    if (sum > INT32_MAX)  { sum = INT32_MAX; }
    if (sum < INT32_MIN) { sum = INT32_MIN; }
    return (q16_16_t)sum;
}

static inline q16_16_t q16_sub(q16_16_t a, q16_16_t b) {
    int64_t diff = (int64_t)a - (int64_t)b;
    if (diff > INT32_MAX)  { diff = INT32_MAX; }
    if (diff < INT32_MIN) { diff = INT32_MIN; }
    return (q16_16_t)diff;
}

static inline q16_16_t q16_mul(q16_16_t a, q16_16_t b) {
    int64_t product = (int64_t)a * (int64_t)b;

    /* Compute bounds without left-shifting negative values */
    const int64_t MAX_UNSHIFTED = ((int64_t)INT32_MAX) << Q16_SHIFT; /* safe: INT32_MAX is positive */
    const int64_t MIN_UNSHIFTED = -(((int64_t)INT32_MAX + 1LL) << Q16_SHIFT); /* avoid shifting negative */

    if (product > MAX_UNSHIFTED) {
        return INT32_MAX;
    } else if (product < MIN_UNSHIFTED) {
        return INT32_MIN;
    }

    int64_t shifted = shift_right_with_round_clamp_32_from_64(product, Q16_SHIFT);
    return (q16_16_t)shifted;
}

static inline bool q16_div(q16_16_t a, q16_16_t b, q16_16_t *result) {
    bool success = false;
    if ((b != 0) && (result != NULL)) {
        int64_t temp = (int64_t)a << Q16_SHIFT;
        int64_t abs_b = (b >= 0) ? (int64_t)b : -(int64_t)b;
        /* rounding: add/subtract half of |b| based on sign of numerator */
        if (temp >= 0) temp += abs_b / 2LL; else temp -= abs_b / 2LL;
        int64_t final_div = temp / (int64_t)b;
        if (final_div > INT32_MAX) {
            *result = INT32_MAX;
        } else if (final_div < INT32_MIN) {
            *result = INT32_MIN;
        } else {
            *result = (q16_16_t)final_div;
            success = true;
        }
    }
    return success;
}

/* ==========================================
 * Q8.24 Functions
 * ========================================== */
static inline q8_24_t q8_add(q8_24_t a, q8_24_t b) {
    int64_t sum = (int64_t)a + (int64_t)b;
    if (sum > INT32_MAX)  { sum = INT32_MAX; }
    if (sum < INT32_MIN) { sum = INT32_MIN; }
    return (q8_24_t)sum;
}

static inline q8_24_t q8_sub(q8_24_t a, q8_24_t b) {
    int64_t diff = (int64_t)a - (int64_t)b;
    if (diff > INT32_MAX)  { diff = INT32_MAX; }
    if (diff < INT32_MIN) { diff = INT32_MIN; }
    return (q8_24_t)diff;
}

static inline q8_24_t q8_mul(q8_24_t a, q8_24_t b) {
    int64_t product = (int64_t)a * (int64_t)b;
    const int64_t MAX_UNSHIFTED = ((int64_t)INT32_MAX) << Q8_SHIFT;
    const int64_t MIN_UNSHIFTED = -(((int64_t)INT32_MAX + 1LL) << Q8_SHIFT);

    if (product > MAX_UNSHIFTED) {
        return INT32_MAX;
    } else if (product < MIN_UNSHIFTED) {
        return INT32_MIN;
    }

    int64_t rounding = (product >= 0) ? (1LL << (Q8_SHIFT - 1U)) : -(1LL << (Q8_SHIFT - 1U));
    int64_t shifted = (product + rounding) >> Q8_SHIFT;
    if (shifted > INT32_MAX) return INT32_MAX;
    if (shifted < INT32_MIN) return INT32_MIN;
    return (q8_24_t)shifted;
}

static inline bool q8_div(q8_24_t a, q8_24_t b, q8_24_t *result) {
    bool success = false;
    if ((b != 0) && (result != NULL)) {
        int64_t temp = (int64_t)a << Q8_SHIFT;
        int64_t abs_b = (b >= 0) ? (int64_t)b : -(int64_t)b;
        if (temp >= 0) temp += abs_b / 2LL; else temp -= abs_b / 2LL;
        int64_t final_div = temp / (int64_t)b;
        if (final_div > INT32_MAX) {
            *result = INT32_MAX;
        } else if (final_div < INT32_MIN) {
            *result = INT32_MIN;
        } else {
            *result = (q8_24_t)final_div;
            success = true;
        }
    }
    return success;
}

/* ==========================================
 * Q24.8 Functions
 * ========================================== */
static inline q24_8_t q24_add(q24_8_t a, q24_8_t b) {
    int64_t sum = (int64_t)a + (int64_t)b;
    if (sum > INT32_MAX)  { sum = INT32_MAX; }
    if (sum < INT32_MIN) { sum = INT32_MIN; }
    return (q24_8_t)sum;
}

static inline q24_8_t q24_sub(q24_8_t a, q24_8_t b) {
    int64_t diff = (int64_t)a - (int64_t)b;
    if (diff > INT32_MAX)  { diff = INT32_MAX; }
    if (diff < INT32_MIN) { diff = INT32_MIN; }
    return (q24_8_t)diff;
}

static inline q24_8_t q24_mul(q24_8_t a, q24_8_t b) {
    int64_t product = (int64_t)a * (int64_t)b;
    const int64_t MAX_UNSHIFTED = ((int64_t)INT32_MAX) << Q24_SHIFT;
    const int64_t MIN_UNSHIFTED = -(((int64_t)INT32_MAX + 1LL) << Q24_SHIFT);

    if (product > MAX_UNSHIFTED) {
        return INT32_MAX;
    } else if (product < MIN_UNSHIFTED) {
        return INT32_MIN;
    }

    int64_t rounding = (product >= 0) ? (1LL << (Q24_SHIFT - 1U)) : -(1LL << (Q24_SHIFT - 1U));
    int64_t shifted = (product + rounding) >> Q24_SHIFT;
    if (shifted > INT32_MAX) return INT32_MAX;
    if (shifted < INT32_MIN) return INT32_MIN;
    return (q24_8_t)shifted;
}

static inline bool q24_div(q24_8_t a, q24_8_t b, q24_8_t *result) {
    bool success = false;
    if ((b != 0) && (result != NULL)) {
        int64_t temp = (int64_t)a << Q24_SHIFT;
        int64_t abs_b = (b >= 0) ? (int64_t)b : -(int64_t)b;
        if (temp >= 0) temp += abs_b / 2LL; else temp -= abs_b / 2LL;
        int64_t final_div = temp / (int64_t)b;
        if (final_div > INT32_MAX) {
            *result = INT32_MAX;
        } else if (final_div < INT32_MIN) {
            *result = INT32_MIN;
        } else {
            *result = (q24_8_t)final_div;
            success = true;
        }
    }
    return success;
}

/* ==========================================
 * Q0.31 Functions
 * ========================================== */
static inline q0_31_t q0_31_add(q0_31_t a, q0_31_t b) {
    int64_t sum = (int64_t)a + (int64_t)b;
    if (sum > INT32_MAX)  { sum = INT32_MAX; }
    if (sum < INT32_MIN) { sum = INT32_MIN; }
    return (q0_31_t)sum;
}

static inline q0_31_t q0_31_sub(q0_31_t a, q0_31_t b) {
    int64_t diff = (int64_t)a - (int64_t)b;
    if (diff > INT32_MAX)  { diff = INT32_MAX; }
    if (diff < INT32_MIN) { diff = INT32_MIN; }
    return (q0_31_t)diff;
}

static inline q0_31_t q0_31_mul(q0_31_t a, q0_31_t b) {
    int64_t product = (int64_t)a * (int64_t)b;
    const int64_t MAX_UNSHIFTED = ((int64_t)INT32_MAX) << Q0_31_SHIFT;
    const int64_t MIN_UNSHIFTED = -(((int64_t)INT32_MAX + 1LL) << Q0_31_SHIFT);

    if (product > MAX_UNSHIFTED) {
        return INT32_MAX;
    } else if (product < MIN_UNSHIFTED) {
        return INT32_MIN;
    }

    int64_t rounding = (product >= 0) ? (1LL << (Q0_31_SHIFT - 1U)) : -(1LL << (Q0_31_SHIFT - 1U));
    int64_t shifted = (product + rounding) >> Q0_31_SHIFT;
    if (shifted > INT32_MAX) return INT32_MAX;
    if (shifted < INT32_MIN) return INT32_MIN;
    return (q0_31_t)shifted;
}

static inline bool q0_31_div(q0_31_t a, q0_31_t b, q0_31_t *result) {
    bool success = false;
    if ((b != 0) && (result != NULL)) {
        int64_t temp = (int64_t)a << Q0_31_SHIFT;
        int64_t abs_b = (b >= 0) ? (int64_t)b : -(int64_t)b;
        if (temp >= 0) temp += abs_b / 2LL; else temp -= abs_b / 2LL;
        int64_t final_div = temp / (int64_t)b;
        if (final_div > INT32_MAX) {
            *result = INT32_MAX;
        } else if (final_div < INT32_MIN) {
            *result = INT32_MIN;
        } else {
            *result = (q0_31_t)final_div;
            success = true;
        }
    }
    return success;
}

/* ==========================================
 * Q1.15 Functions
 * ========================================== */
static inline q1_15_t q1_add(q1_15_t a, q1_15_t b) {
    int32_t sum = (int32_t)a + (int32_t)b;
    if (sum > INT16_MAX)  { sum = INT16_MAX; }
    if (sum < INT16_MIN) { sum = INT16_MIN; }
    return (q1_15_t)sum;
}

static inline q1_15_t q1_sub(q1_15_t a, q1_15_t b) {
    int32_t diff = (int32_t)a - (int32_t)b;
    if (diff > INT16_MAX)  { diff = INT16_MAX; }
    if (diff < INT16_MIN) { diff = INT16_MIN; }
    return (q1_15_t)diff;
}

static inline q1_15_t q1_mul(q1_15_t a, q1_15_t b) {
    int32_t product = (int32_t)a * (int32_t)b;
    const int32_t MAX_UNSHIFTED = (int32_t)INT16_MAX << Q1_SHIFT;
    const int32_t MIN_UNSHIFTED = -(((int32_t)INT16_MAX + 1) << Q1_SHIFT);

    if (product > MAX_UNSHIFTED) {
        return INT16_MAX;
    } else if (product < MIN_UNSHIFTED) {
        return INT16_MIN;
    }

    int32_t shifted = shift_right_with_round_clamp_16_from_32(product, Q1_SHIFT);
    return (q1_15_t)shifted;
}

static inline bool q1_div(q1_15_t a, q1_15_t b, q1_15_t *result) {
    bool success = false;
    if ((b != 0) && (result != NULL)) {
        int32_t temp = (int32_t)a << Q1_SHIFT;
        int32_t abs_b = (b >= 0) ? (int32_t)b : -(int32_t)b;
        if (temp >= 0) temp += abs_b / 2; else temp -= abs_b / 2;
        int32_t final_div = temp / (int32_t)b;
        if (final_div > INT16_MAX) {
            *result = INT16_MAX;
        } else if (final_div < INT16_MIN) {
            *result = (int16_t)0x8000;
        } else {
            *result = (q1_15_t)final_div;
            success = true;
        }
    }
    return success;
}

/* ==========================================
 * Q4.12 Functions
 * ========================================== */
static inline q4_12_t q4_add(q4_12_t a, q4_12_t b) {
    int32_t sum = (int32_t)a + (int32_t)b;
    if (sum > INT16_MAX)  { sum = INT16_MAX; }
    if (sum < INT16_MIN) { sum = INT16_MIN; }
    return (q4_12_t)sum;
}

static inline q4_12_t q4_sub(q4_12_t a, q4_12_t b) {
    int32_t diff = (int32_t)a - (int32_t)b;
    if (diff > INT16_MAX)  { diff = INT16_MAX; }
    if (diff < INT16_MIN) { diff = INT16_MIN; }
    return (q4_12_t)diff;
}

static inline q4_12_t q4_mul(q4_12_t a, q4_12_t b) {
    int32_t product = (int32_t)a * (int32_t)b;
    const int32_t MAX_UNSHIFTED = (int32_t)INT16_MAX << Q4_SHIFT;
    const int32_t MIN_UNSHIFTED = -(((int32_t)INT16_MAX + 1) << Q4_SHIFT);

    if (product > MAX_UNSHIFTED) {
        return INT16_MAX;
    } else if (product < MIN_UNSHIFTED) {
        return INT16_MIN;
    }

    int32_t shifted = shift_right_with_round_clamp_16_from_32(product, Q4_SHIFT);
    return (q4_12_t)shifted;
}

static inline bool q4_div(q4_12_t a, q4_12_t b, q4_12_t *result) {
    bool success = false;
    if ((b != 0) && (result != NULL)) {
        int32_t temp = (int32_t)a << Q4_SHIFT;
        int32_t abs_b = (b >= 0) ? (int32_t)b : -(int32_t)b;
        if (temp >= 0) temp += abs_b / 2; else temp -= abs_b / 2;
        int32_t final_div = temp / (int32_t)b;
        if (final_div > INT16_MAX) {
            *result = INT16_MAX;
        } else if (final_div < INT16_MIN) {
            *result = (int16_t)0x8000;
        } else {
            *result = (q4_12_t)final_div;
            success = true;
        }
    }
    return success;
}

/* ==========================================
 * Q32.32 Functions (Pure ISO C99 Compliant)
 * ========================================== */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

static inline q32_32_t q32_add(q32_32_t a, q32_32_t b) {
    q32_32_t sum = a + b;
    if ((b > 0) && (a > (INT64_MAX - b))) { sum = INT64_MAX; }
    if ((b < 0) && (a < (INT64_MIN - b))) { sum = INT64_MIN; }
    return sum;
}

static inline q32_32_t q32_sub(q32_32_t a, q32_32_t b) {
    q32_32_t diff = a - b;
    if ((b < 0) && (a > (INT64_MAX + b))) { diff = INT64_MAX; }
    if ((b > 0) && (a < (INT64_MIN + b))) { diff = INT64_MIN; }
    return diff;
}

static inline q32_32_t q32_mul(q32_32_t a, q32_32_t b) {
    __int128 product = (__int128)a * (__int128)b;

    /* Safe pre-computed bounds: compute MIN by shifting positive and negating */
    const __int128 MAX_UNSHIFTED = ((__int128)INT64_MAX) << Q32_SHIFT;
    const __int128 MIN_UNSHIFTED = -(((__int128)INT64_MAX + 1) << Q32_SHIFT);

    if (product > MAX_UNSHIFTED) {
        return INT64_MAX;
    } else if (product < MIN_UNSHIFTED) {
        return INT64_MIN;
    }

    /* sign-aware rounding */
    __int128 rounding = (product >= 0) ? ((__int128)1 << (Q32_SHIFT - 1U)) : -(((__int128)1) << (Q32_SHIFT - 1U));
    __int128 shifted = (product + rounding) >> Q32_SHIFT;
    if (shifted > INT64_MAX) return INT64_MAX;
    if (shifted < INT64_MIN) return INT64_MIN;
    return (q32_32_t)shifted;
}

static inline bool q32_div(q32_32_t a, q32_32_t b, q32_32_t *result) {
    bool success = false;
    if ((b != 0) && (result != NULL)) {
        __int128 temp = (__int128)a << Q32_SHIFT;
        __int128 abs_b = (b >= 0) ? (__int128)b : -(__int128)b;
        if (temp >= 0) temp += abs_b / 2; else temp -= abs_b / 2;
        __int128 final = temp / (__int128)b;
        if (final > INT64_MAX) {
            *result = INT64_MAX;
        } else if (final < INT64_MIN) {
            *result = INT64_MIN;
        } else {
            *result = (q32_32_t)final;
            success = true;
        }
    }
    return success;
}

#pragma GCC diagnostic pop

#endif /* FIXED_ARITHMETIC_H */
