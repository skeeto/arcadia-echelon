/*
 * prng.h — splitmix64 used as a stateless hash.
 *
 * The key property: stream(seed, tick, salt) jumps directly to an independent
 * pseudo-random value for a given (seed, spawn-tick, purpose) without iterating,
 * so world generation is a pure, order-independent function of integer inputs
 * and every client computes byte-identical results.
 */
#ifndef ECHELON_PRNG_H
#define ECHELON_PRNG_H

#include <stdint.h>

static inline uint64_t sm64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/* An independent 64-bit value keyed by (seed, tick, salt). */
static inline uint64_t stream(uint32_t seed, int32_t tick, uint32_t salt)
{
    return sm64(((uint64_t)seed << 32) ^ (uint32_t)tick ^ sm64(salt + 0x1000));
}

/* Uniform integer in [0, n) from a 64-bit draw. */
static inline uint32_t rng_below(uint64_t r, uint32_t n)
{
    return n ? (uint32_t)((r >> 33) % n) : 0;
}

/* Uniform float in [0,1). */
static inline float rng_unit(uint64_t r)
{
    return (float)((r >> 40) & 0xFFFFFF) / (float)0x1000000;
}

/* Uniform float in [lo,hi). */
static inline float rng_range(uint64_t r, float lo, float hi)
{
    return lo + (hi - lo) * rng_unit(r);
}

/* percent chance in [0,100): true with probability pct/100 using the low bits. */
static inline int rng_chance(uint64_t r, uint32_t pct)
{
    return (uint32_t)((r >> 12) % 100) < pct;
}

#endif /* ECHELON_PRNG_H */
