/* Game rumble: the console's haptics capture stream.
 *
 * libSceAvcap2 offers OpenHaptic / ReadHaptic besides the audio and video
 * capture Remote Play uses: the haptic signal the console sends to a
 * DualSense, which is also what a game's rumble becomes on this console.
 * Opened with the block below (found on a console) it gives interleaved
 * float32 pairs, exactly zero until a game rumbles. A thread reads it
 * and haptic_level.c turns it into motor levels for AnyPad's pads.
 */
#ifndef PH_PS5_HAPTIC_H
#define PH_PS5_HAPTIC_H

#include <stdint.h>

/* The capture thread. haptic_want(1) opens the stream (when any of our pads
 * is connected and game rumble is on), haptic_want(0) closes it; the thread
 * recovers by itself when the stream ends. */
int  haptic_start(void);
void haptic_want(int on);
void haptic_quit(void);

/* Records the raw stream for `seconds` (1..30) to /data/anypad/haptic-<ms>.raw.
 * Returns 0 if the stream is not open. The last file's path once done ("" while recording). */
int  haptic_record(int seconds);
const char *haptic_record_path(void);

/* The motor levels now (0..255). Returns 1 if the stream is open. */
int  haptic_levels(uint8_t *strong, uint8_t *weak);

/* Experiment kept for the page: opens the stream once with `param`, reads for
 * `ms` and writes a JSON report. Refused while the thread holds the stream. */
int  haptic_probe(const unsigned char *param, int plen, int open, int flag, int ms, int auth,
                  char *out, int n);

#endif
