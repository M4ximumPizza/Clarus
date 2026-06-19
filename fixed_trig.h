#ifndef FIXED_TRIG_H
#define FIXED_TRIG_H

#include "fixed_arithmetic.h"
#include <stdbool.h>

/**
 * @file fixed_trig.h
 * @brief Fixed-point Q16.16 Trigonometric Functions using the CORDIC Algorithm.
 * * This header provides fast, deterministic trigonometric operations (sin, cos, tan)
 * for systems lacking a hardware Floating Point Unit (FPU). All calculations use
 * signed 32-bit integers in Q16.16 format (16 bits for integer, 16 bits for fraction).

 @author M4ximumpizza
 */

/* =========================================================================
 * PI Constants in Q16.16 Format
 * Formulated as: (Value * 65536) rounded to nearest integer.
 * ========================================================================= */

/** @brief Q16.16 representation of $\pi$ (3.14159265) */
#define Q16_PI         205887L

/** @brief Q16.16 representation of $\pi/2$ (1.57079633) */
#define Q16_HALF_PI    102944L

/** @brief Q16.16 representation of $2\pi$ (6.28318531) */
#define Q16_TWO_PI     411775L


/**
 * @brief Computes both the sine and cosine of an angle simultaneously using CORDIC.
 * * Validates inputs, normalizes the angle $\theta$ to the fundamental domain $(-\pi, \pi]$,
 * transforms it to the CORDIC convergence range $[-\pi/2, \pi/2]$, and executes 15 pseudo-rotation
 * iterations.
 * * @param[in]  theta    The input angle in radians, represented as a signed q16_16_t.
 * @param[out] sin_res  Pointer to store the resulting sine value (q16_16_t).
 * @param[out] cos_res  Pointer to store the resulting cosine value (q16_16_t).
 * * @note Out-of-bounds inputs (e.g., highly large or small angles) will trigger repeated
 * subtractions/additions for normalization, impacting performance linearly.
 * * @return true if calculations were successful; false if either result pointer is NULL.
 */
static inline bool q16_sin_cos(q16_16_t theta, q16_16_t *sin_res, q16_16_t *cos_res) {
    if (sin_res == NULL || cos_res == NULL) {
        return false;
    }

    /* 1. Normalize angle to (-PI, PI] using primitive modulo-emulation loops */
    while (theta > Q16_PI)  theta -= Q16_TWO_PI;
    while (theta <= -Q16_PI) theta += Q16_TWO_PI;

    /* 2. Handle quadrant adjustments for CORDIC convergence range (-PI/2 to PI/2)
     * If the angle is in Quadrant II or III, reflect it to Quadrant I or IV */
    bool flip_sign = false;
    if (theta > Q16_HALF_PI) {
        theta = Q16_PI - theta;
        flip_sign = true;
    } else if (theta < -Q16_HALF_PI) {
        theta = -Q16_PI - theta;
        flip_sign = true;
    }

    /* Pre-calculated atan(2^-i) table in Q16.16 for 15 iterations.
     * cordic_angles[i] = atan(1 / (2^i)) * 65536 */
    static const q16_16_t cordic_angles[] = {
        51472, 30386, 16055, 8150, 4090, 2047, 1024, 512, 256, 128, 64, 32, 16, 8, 4
    };

    /* Initial CORDIC vector scaled by the inverse CORDIC growth factor 1/A_n.
     * For 15 iterations, A_n approaches ~1.64676, meaning 1/A_n is ~0.607252935.
     * 0.607252935 * 65536 = 39797 */
    q16_16_t x = 39797;
    q16_16_t y = 0;
    q16_16_t z = theta;

    /* 3. CORDIC Iterations (Pseudo-rotations via bit-shifting and additions) */
    for (unsigned i = 0; i < 15; i++) {
        q16_16_t x_shift = x >> i;
        q16_16_t y_shift = y >> i;

        if (z >= 0) {
            x = q16_sub(x, y_shift);
            y = q16_add(y, x_shift);
            z = q16_sub(z, cordic_angles[i]);
        } else {
            x = q16_add(x, y_shift);
            y = q16_sub(y, x_shift);
            z = q16_add(z, cordic_angles[i]);
        }
    }

    /* 4. Map back to original quadrant if flipped during step 2 */
    if (flip_sign) {
        x = -x;
    }

    *cos_res = x;
    *sin_res = y;
    return true;
}

/**
 * @brief Computes the sine of an angle in Q16.16 format.
 * * Wrapper around q16_sin_cos(). Discards the cosine result.
 * * @param theta The input angle in radians (q16_16_t).
 * @return The computed sine value as a q16_16_t in the range [-1.0, 1.0] ([-65536, 65536]).
 */
static inline q16_16_t q16_sin(q16_16_t theta) {
    q16_16_t s, c;
    q16_sin_cos(theta, &s, &c);
    return s;
}

/**
 * @brief Computes the cosine of an angle in Q16.16 format.
 * * Wrapper around q16_sin_cos(). Discards the sine result.
 * * @param theta The input angle in radians (q16_16_t).
 * @return The computed cosine value as a q16_16_t in the range [-1.0, 1.0] ([-65536, 65536]).
 */
static inline q16_16_t q16_cos(q16_16_t theta) {
    q16_16_t s, c;
    q16_sin_cos(theta, &s, &c);
    return c;
}

/**
 * @brief Computes the tangent of an angle ($\tan(\theta) = \sin(\theta)/\cos(\theta)$).
 * * Leverages the unified CORDIC calculation step followed by a fixed-point division.
 * * @param[in]  theta   The input angle in radians (q16_16_t).
 * @param[out] result  Pointer to store the resulting tangent value (q16_16_t).
 * * @return true if successful; false if a division-by-zero or saturation error occurred
 * via `q16_div` (e.g., when $\theta \approx \pm\pi/2$).
 */
static inline bool q16_tan(q16_16_t theta, q16_16_t *result) {
    q16_16_t s, c;
    if (!q16_sin_cos(theta, &s, &c)) return false;
    return q16_div(s, c, result);
}

#endif /* FIXED_TRIG_H */