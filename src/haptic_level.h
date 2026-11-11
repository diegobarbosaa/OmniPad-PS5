/* Game rumble from the console's haptics: the signal a DualSense would play
 * on its two actuators, turned into the two motor levels other pads have.
 *
 * The capture stream (ps5_haptic.c) gives chunks of interleaved 32-bit float
 * samples, two channels (left and right actuator), -1..1, about 12 000 frames
 * a second; silence is exactly 0. Measured on a console: light effects have
 * an RMS of about 0.01..0.03 (peaks 0.2..0.35), heavy ones about 0.10..0.12
 * (peaks about 0.9).
 *
 * Each chunk's RMS per side becomes a level: HAPTIC_FULL_RMS is full
 * strength, anything under HAPTIC_FLOOR of that is silence, and a curve
 * (0.15 + 0.85 * x^0.6) keeps light effects felt without matching heavy ones.
 * The highest level is held a short while so short knocks are still felt.
 * Left drives the strong (low-frequency) motor, right the weak one.
 */
#ifndef PH_HAPTIC_LEVEL_H
#define PH_HAPTIC_LEVEL_H

#include <stdint.h>

#define HAPTIC_HOLD_MS   80     /* the highest level is kept this long */
#define HAPTIC_STALE_MS  250    /* no chunk for this long: silence */
#define HAPTIC_FULL_RMS  0.11f  /* RMS that gives full strength */
#define HAPTIC_FLOOR     0.04f  /* fraction of full under which it is silence */

typedef struct {
    uint8_t cur[2];             /* the last chunk's level, per side */
    uint8_t held[2];            /* the highest recent level */
    long t_held[2];             /* when it was set */
    long t_feed;                /* when the last chunk came */
    int fed;
} haptic_env;

void haptic_env_reset(haptic_env *e);

/* One chunk of the stream: len bytes of interleaved little-endian float32 pairs. */
void haptic_env_feed(haptic_env *e, const uint8_t *data, int len, long now);

/* The motor levels now, 0..255: strong from the left actuator, weak from the right. */
void haptic_env_levels(const haptic_env *e, long now, uint8_t *strong, uint8_t *weak);

/* A level 0..255 from an RMS (exposed for tests). */
uint8_t haptic_level_from_rms(float rms);

#endif
