#include "haptic_level.h"

#include <string.h>

void haptic_env_reset(haptic_env *e)
{
    memset(e, 0, sizeof *e);
}

static unsigned isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;

    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (unsigned)r;
}

/* x^0.6 for x in 0..1 at steps of 1/32 (no libm on the console), interpolated. */
static const float k_pow06[33] = {
    0.0000f, 0.1250f, 0.1895f, 0.2416f, 0.2872f, 0.3283f, 0.3663f, 0.4018f,
    0.4353f, 0.4671f, 0.4976f, 0.5269f, 0.5552f, 0.5825f, 0.6090f, 0.6347f,
    0.6598f, 0.6842f, 0.7081f, 0.7314f, 0.7543f, 0.7767f, 0.7987f, 0.8203f,
    0.8415f, 0.8623f, 0.8829f, 0.9031f, 0.9230f, 0.9426f, 0.9620f, 0.9811f,
    1.0000f,
};

static float pow06(float x)
{
    float f;
    int i;

    if (x <= 0) return 0;
    if (x >= 1) return 1;
    f = x * 32.0f;
    i = (int)f;
    if (i >= 32) return 1;
    return k_pow06[i] + (k_pow06[i + 1] - k_pow06[i]) * (f - (float)i);
}

uint8_t haptic_level_from_rms(float rms)
{
    float x = rms / HAPTIC_FULL_RMS, out;

    if (!(x > HAPTIC_FLOOR)) return 0;          /* also catches NaN */
    if (x > 1) x = 1;
    out = 0.15f + 0.85f * pow06(x);
    return (uint8_t)(out * 255.0f + 0.5f);
}

void haptic_env_feed(haptic_env *e, const uint8_t *data, int len, long now)
{
    double sq[2] = { 0, 0 };
    unsigned cnt[2] = { 0, 0 };
    int i, side;

    for (i = 0; i + 3 < len; i += 4) {
        union { uint32_t u; float f; } v;
        v.u = (uint32_t)data[i] | (uint32_t)data[i + 1] << 8 | (uint32_t)data[i + 2] << 16 |
              (uint32_t)data[i + 3] << 24;
        side = (i >> 2) & 1;
        if (!(v.f > -4.0f && v.f < 4.0f)) continue;     /* not a sample (NaN, inf, garbage) */
        sq[side] += (double)v.f * v.f;
        cnt[side]++;
    }
    for (side = 0; side < 2; side++) {
        float rms = 0;
        uint8_t lvl;
        if (cnt[side]) {
            /* sqrt without libm: integer square root of the mean square in 1e-12 units */
            double ms = sq[side] / cnt[side];
            rms = (float)isqrt64((uint64_t)(ms * 1e12)) / 1e6f;
        }
        lvl = haptic_level_from_rms(rms);
        e->cur[side] = lvl;
        if (lvl >= e->held[side] || now - e->t_held[side] > HAPTIC_HOLD_MS) {
            e->held[side] = lvl;
            e->t_held[side] = now;
        }
    }
    e->t_feed = now;
    e->fed = 1;
}

void haptic_env_levels(const haptic_env *e, long now, uint8_t *strong, uint8_t *weak)
{
    uint8_t out[2];
    int side;

    for (side = 0; side < 2; side++) {
        uint8_t v = e->cur[side];
        if (now - e->t_held[side] <= HAPTIC_HOLD_MS && e->held[side] > v) v = e->held[side];
        out[side] = e->fed && now - e->t_feed <= HAPTIC_STALE_MS ? v : 0;
    }
    *strong = out[0];
    *weak = out[1];
}
