/* Game rumble: the console's haptics capture stream (see ps5_haptic.h).
 *
 * What is known, from libSceAvcap2 itself and a console (firmware 10.01):
 *   - every Open* is an IPC stub to the capture service: OpenAudio method
 *     0xA70000 with 0x38 bytes, OpenVideo 0xA70001 with 0x2E8, OpenHaptic
 *     0xA70002 with 0x18, an unnamed open (NID ijHDdN-xBf4) 0xA70003 with 0x18;
 *   - OpenHaptic accepts { u64 0x18, u64 0, u32 2, u32 0 } (2: the Remote
 *     Play source, as for audio); other guesses answer 0x81950001;
 *   - ReadHaptic(handle, buf, meta, flag) reads 0x800 bytes into buf + 8 and
 *     a 0x18-byte meta block, about 140 chunks in 3 s: interleaved float32
 *     pairs (left and right actuator, -1..1, about 12 000 frames a second),
 *     exactly zero until a game rumbles (recordings: haptic_level.h). The
 *     meta holds the virtual pad's device id (0x...030d, as the console's
 *     log names it) in one of its slots, ff ff ff ff in the others;
 *   - after a while the service may answer 0x81950008 (ended) to every open:
 *     the session is then closed and the library terminated and initialised
 *     again, with a growing pause. A call that never returns only stalls
 *     this thread, never the controllers;
 *   - the library needs libSceIpmi loaded first (found by HearBridge-PS5,
 *     whose loader is followed here).
 */
#include "ps5_haptic.h"
#include "haptic_level.h"
#include "log.h"
#include "util.h"

#include <ps5/kernel.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define NID_LoadStartModule "wzvqT4UqKX8"
#define NID_Initialize      "svzPXluOz8U"
#define NID_Terminate       "gojqghDU+1Y"
#define NID_OpenHaptic      "Tzk7-W5mp0A"
#define NID_OpenOther       "ijHDdN-xBf4"
#define NID_Close           "tR7gPe1i8hw"
#define NID_Start           "8CGNCwBsItI"
#define NID_Stop            "Z5dKK0xnQ8g"
#define NID_ReadHaptic      "1Xp85wUaVTE"

#define AVCAP2_INVALID 0x81950001u
#define AVCAP2_EMPTY   0x81950002u
#define AVCAP2_OVERRUN 0x81950004u
#define AVCAP2_ENDED   0x81950008u

#define AUTHID_AVCAP2  0x4900000000000002ULL

#define DATA_LEN 0x800
#define META_LEN 0x18

#define BACKOFF_FIRST_MS 1000
#define BACKOFF_MAX_MS   15000

typedef int (*fn_LoadStart)(const char *, size_t, const void *, uint32_t, void *, int *);
typedef int (*fn_Init)(void);
typedef int (*fn_Term)(void);
typedef int (*fn_Open)(int64_t *outhandle, void *param, int flags);
typedef int (*fn_Close)(int64_t handle);
typedef int (*fn_Start)(int64_t handle);
typedef int (*fn_Stop)(int64_t handle);
typedef int (*fn_Read)(int64_t handle, void *buf, void *meta, int flag);

static struct {
    int loaded;
    fn_Init init;
    fn_Term term;
    fn_Open open_haptic, open_other;
    fn_Close close;
    fn_Start start;
    fn_Stop stop;
    fn_Read read;
} L;

static int g_inited;                    /* sceAvcap2Initialize done (shared by thread and probe) */

/* The thread's state; levels behind the mutex. */
static pthread_t g_thr;
static int g_thr_up;
static volatile int g_want, g_quit, g_open;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static haptic_env g_env;

/* Recording the raw stream for analysis: requested by the page, written by the thread. */
#define REC_DIR "/data/anypad"
static volatile long g_rec_until;       /* record chunks until then (ms); 0: not asked */
static FILE *g_rec;
static char g_rec_path[96];

static const unsigned char k_block[0x18] = {
    0x18, 0, 0, 0, 0, 0, 0, 0,          /* size */
    0, 0, 0, 0, 0, 0, 0, 0,
    2, 0, 0, 0,                         /* source: Remote Play */
    0, 0, 0, 0,
};

/* ---- the library ----------------------------------------------------------- */

static uint32_t lib_handle(const char *name)
{
    uint32_t h = 0;

    return kernel_dynlib_handle(-1, name, &h) == 0 ? h : 0;
}

static int load(fn_LoadStart ls, const char *base)
{
    static const char *const roots[] = { "/system/common/lib/", "/system/priv/lib/", "" };
    char path[128];
    size_t i;
    int res;

    if (lib_handle(base)) return 1;
    for (i = 0; ls && i < sizeof roots / sizeof roots[0]; i++) {
        snprintf(path, sizeof path, "%s%s", roots[i], base);
        res = -1;
        ls(path, 0, NULL, 0, NULL, &res);
        if (lib_handle(base)) return 1;
    }
    log_line("haptic: could not load %s", base);
    return 0;
}

static int lib_load(void)
{
    uint32_t kh, ah;
    fn_LoadStart ls;

    if (L.loaded) return 1;
    kh = lib_handle("libkernel_web.sprx");
    if (!kh) kh = lib_handle("libkernel.sprx");
    if (!kh) kh = lib_handle("libkernel_sys.sprx");
    ls = kh ? (fn_LoadStart)kernel_dynlib_resolve(-1, kh, NID_LoadStartModule) : NULL;
    if (!load(ls, "libSceIpmi.sprx") || !load(ls, "libSceAvcap2.sprx") || !(ah = lib_handle("libSceAvcap2.sprx")))
        return 0;
    L.init = (fn_Init)kernel_dynlib_resolve(-1, ah, NID_Initialize);
    L.term = (fn_Term)kernel_dynlib_resolve(-1, ah, NID_Terminate);
    L.open_haptic = (fn_Open)kernel_dynlib_resolve(-1, ah, NID_OpenHaptic);
    L.open_other = (fn_Open)kernel_dynlib_resolve(-1, ah, NID_OpenOther);
    L.close = (fn_Close)kernel_dynlib_resolve(-1, ah, NID_Close);
    L.start = (fn_Start)kernel_dynlib_resolve(-1, ah, NID_Start);
    L.stop = (fn_Stop)kernel_dynlib_resolve(-1, ah, NID_Stop);
    L.read = (fn_Read)kernel_dynlib_resolve(-1, ah, NID_ReadHaptic);
    if (!L.init || !L.term || !L.open_haptic || !L.close || !L.start || !L.stop || !L.read) {
        log_line("haptic: a libSceAvcap2 function is missing");
        return 0;
    }
    L.loaded = 1;
    return 1;
}

/* ---- the thread ------------------------------------------------------------- */

static int session_open(int64_t *h)
{
    unsigned char block[sizeof k_block];
    int r;

    if (!g_inited) {
        r = L.init();
        if (r != 0) {
            log_line("haptic: initialise %#x", (unsigned)r);
            return 0;
        }
        g_inited = 1;
    }
    memcpy(block, k_block, sizeof block);
    *h = 0;
    r = L.open_haptic(h, block, 0);
    if (r != 0 || !*h) {
        log_line("haptic: open %#x", (unsigned)r);
        return 0;
    }
    r = L.start(*h);
    if (r != 0) {
        log_line("haptic: start %#x", (unsigned)r);
        L.close(*h);
        *h = 0;
        return 0;
    }
    return 1;
}

static void session_close(int64_t *h, int reset)
{
    if (*h) {
        L.stop(*h);
        L.close(*h);
        *h = 0;
    }
    if (reset && g_inited) {
        L.term();
        g_inited = 0;
    }
    pthread_mutex_lock(&g_mx);
    haptic_env_reset(&g_env);
    pthread_mutex_unlock(&g_mx);
    g_open = 0;
    if (g_rec) {                        /* a recording cut short by the stream */
        fclose(g_rec);
        g_rec = NULL;
        g_rec_until = 0;
        log_line("haptic: recording cut short: %s", g_rec_path);
    }
}

/* One record per chunk: "HREC", u32 ms, i32 read result, u32 bytes, the
 * 0x18-byte meta, the 8 bytes before the data, then the data. */
static void record_chunk(const unsigned char *buf, int k, const unsigned char *meta, int got)
{
    long now = now_ms();
    uint32_t hdr[4];

    if (!g_rec_until) return;
    if (!g_rec) {
        snprintf(g_rec_path, sizeof g_rec_path, REC_DIR "/haptic-%ld.raw", now);
        g_rec = fopen(g_rec_path, "wb");
        log_line("haptic: recording to %s %s", g_rec_path, g_rec ? "" : "FAILED");
        if (!g_rec) { g_rec_until = 0; return; }
    }
    if (now >= g_rec_until) {
        fclose(g_rec);
        g_rec = NULL;
        g_rec_until = 0;
        log_line("haptic: recording done: %s", g_rec_path);
        return;
    }
    memcpy(&hdr[0], "HREC", 4);
    hdr[1] = (uint32_t)now;
    hdr[2] = (uint32_t)got;
    hdr[3] = (uint32_t)k;
    fwrite(hdr, sizeof hdr, 1, g_rec);
    fwrite(meta, META_LEN, 1, g_rec);
    fwrite(buf, 8, 1, g_rec);
    fwrite(buf + 8, (size_t)k, 1, g_rec);
}

int haptic_record(int seconds)
{
    if (!g_open || seconds < 1 || seconds > 30) return 0;
    if (g_rec_until) return 1;
    g_rec_until = now_ms() + seconds * 1000L;
    return 1;
}

const char *haptic_record_path(void)
{
    return g_rec_until ? "" : g_rec_path;
}

static void *thread_main(void *arg)
{
    static unsigned char buf[8 + DATA_LEN + 64];
    unsigned char meta[META_LEN + 8];
    int64_t h = 0;
    long retry_at = 0, backoff = BACKOFF_FIRST_MS, opened_at = 0;
    unsigned long long chunks = 0;

    (void)arg;
    while (!g_quit) {
        long now = now_ms();
        int got;

        if (!g_want) {
            if (h) {
                session_close(&h, 0);
                log_line("haptic: closed (no pad of ours, or game rumble off); %llu chunks", chunks);
            }
            usleep(200000);
            continue;
        }
        if (!h) {
            if (now < retry_at) {
                usleep(100000);
                continue;
            }
            if (!lib_load() || !session_open(&h)) {
                session_close(&h, 1);
                retry_at = now + backoff;
                log_line("haptic: could not open the stream; again in %ld ms", backoff);
                backoff = backoff * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoff * 2;
                continue;
            }
            g_open = 1;
            opened_at = now;
            chunks = 0;
            log_line("haptic: stream open: game rumble goes to AnyPad's pads");
        }

        memset(meta, 0, sizeof meta);
        *(uint32_t *)meta = META_LEN;
        got = L.read(h, buf, meta, 1);
        if (got > 0) {
            int k = got < DATA_LEN ? got : DATA_LEN;
            pthread_mutex_lock(&g_mx);
            haptic_env_feed(&g_env, buf + 8, k, now_ms());
            pthread_mutex_unlock(&g_mx);
            record_chunk(buf, k, meta, got);
            chunks++;
            if (now - opened_at > 10000) backoff = BACKOFF_FIRST_MS;   /* it has been working */
            continue;
        }
        if ((unsigned)got == AVCAP2_EMPTY || (unsigned)got == AVCAP2_OVERRUN) {
            usleep(4000);
            continue;
        }
        /* Ended, invalid or anything else: start over, after a pause. */
        log_line("haptic: stream stopped (%#x) after %llu chunks; reopening in %ld ms",
                 (unsigned)got, chunks, backoff);
        session_close(&h, 1);
        retry_at = now_ms() + backoff;
        backoff = backoff * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoff * 2;
    }
    if (h) session_close(&h, 1);
    return NULL;
}

int haptic_start(void)
{
    if (g_thr_up) return 1;
    haptic_env_reset(&g_env);
    g_quit = 0;
    g_thr_up = pthread_create(&g_thr, NULL, thread_main, NULL) == 0;
    if (!g_thr_up) log_line("haptic: could not start the thread: no game rumble");
    return g_thr_up;
}

void haptic_want(int on)
{
    g_want = on;
}

void haptic_quit(void)
{
    g_quit = 1;
    g_want = 0;
    /* Not joined: a capture call that never returns must not stop AnyPad from leaving. */
}

int haptic_levels(uint8_t *strong, uint8_t *weak)
{
    pthread_mutex_lock(&g_mx);
    haptic_env_levels(&g_env, now_ms(), strong, weak);
    pthread_mutex_unlock(&g_mx);
    return g_open;
}

/* ---- the probe ---------------------------------------------------------------- */

static void hex(char *out, size_t n, const unsigned char *p, int len)
{
    int i;

    out[0] = 0;
    for (i = 0; i < len && (size_t)(2 * i + 3) < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

int haptic_probe(const unsigned char *param, int plen, int open, int flag, int ms, int auth,
                 char *out, int n)
{
    static unsigned char buf[8 + DATA_LEN + 64];
    unsigned char block[64], meta[META_LEN + 8], first_meta[META_LEN], first_data[32];
    char hp[160], hm[64], hd[80], hh[24];
    uint64_t old_auth = 0;
    fn_Open opn;
    int64_t h = 0;
    int r_init = 0, r_open, r_start = -1, got_first = 0, peak = 0;
    unsigned ok = 0, empty = 0, overrun = 0, ended = 0, invalid = 0, other = 0, last_other = 0;
    unsigned long long bytes = 0;
    long end;

    if (plen < 0 || plen > (int)sizeof block || ms < 100 || ms > 3000 || (open != 2 && open != 3))
        return snprintf(out, (size_t)n, "{\"error\":\"arguments\"}");
    if (g_want || g_open)
        return snprintf(out, (size_t)n, "{\"error\":\"game rumble holds the stream: turn it off first\"}");
    if (!lib_load()) return snprintf(out, (size_t)n, "{\"error\":\"libSceAvcap2 not loaded\"}");
    opn = open == 2 ? L.open_haptic : L.open_other;
    if (!opn) return snprintf(out, (size_t)n, "{\"error\":\"that open is missing\"}");

    if (auth) {
        old_auth = kernel_get_ucred_authid(-1);
        kernel_set_ucred_authid(-1, AUTHID_AVCAP2);
    }
    if (!g_inited) {
        r_init = L.init();
        if (r_init == 0) g_inited = 1;
    }

    memset(block, 0, sizeof block);
    memcpy(block, param, (size_t)plen);
    hex(hp, sizeof hp, block, plen);
    r_open = opn(&h, block, 0);
    log_line("haptic: probe open %d block %s -> %#x handle %#llx", open, hp, (unsigned)r_open,
             (unsigned long long)h);

    if (r_open == 0 && h) {
        r_start = L.start(h);
        end = now_ms() + ms;
        while (r_start == 0 && now_ms() < end) {
            int got;
            memset(meta, 0, sizeof meta);
            *(uint32_t *)meta = META_LEN;
            got = L.read(h, buf, meta, flag);
            if (got > 0) {
                int i, k = got < DATA_LEN ? got : DATA_LEN;
                ok++;
                bytes += (unsigned)got;
                for (i = 0; i + 1 < k; i += 2) {
                    int v = (int16_t)(buf[8 + i] | buf[9 + i] << 8);
                    if (v < 0) v = -v;
                    if (v > peak) peak = v;
                }
                if (!got_first) {
                    memcpy(first_meta, meta, META_LEN);
                    memcpy(first_data, buf, sizeof first_data);
                    got_first = 1;
                }
                continue;
            }
            switch ((unsigned)got) {
            case AVCAP2_EMPTY:   empty++; break;
            case AVCAP2_OVERRUN: overrun++; break;
            case AVCAP2_ENDED:   ended++; break;
            case AVCAP2_INVALID: invalid++; break;
            default:             other++; last_other = (unsigned)got; break;
            }
            if ((unsigned)got == AVCAP2_ENDED || (unsigned)got == AVCAP2_INVALID) break;
            usleep(4000);
        }
        L.stop(h);
        L.close(h);
    }
    /* A stream that ended or an open refused leaves the library stuck: start it again next time. */
    if (r_open != 0 || ended || invalid) {
        L.term();
        g_inited = 0;
    }
    if (auth) kernel_set_ucred_authid(-1, old_auth);

    hm[0] = hd[0] = 0;
    if (got_first) {
        hex(hm, sizeof hm, first_meta, META_LEN);
        hex(hd, sizeof hd, first_data, (int)sizeof first_data);
    }
    snprintf(hh, sizeof hh, "%#llx", (unsigned long long)h);
    return snprintf(out, (size_t)n,
                    "{\"open\":%d,\"block\":\"%s\",\"init\":\"%#x\",\"open_rv\":\"%#x\",\"handle\":\"%s\",\"start\":\"%#x\","
                    "\"reads\":%u,\"bytes\":%llu,\"peak\":%d,\"empty\":%u,\"overrun\":%u,\"ended\":%u,\"invalid\":%u,"
                    "\"other\":%u,\"last_other\":\"%#x\",\"meta\":\"%s\",\"data\":\"%s\"}",
                    open, hp, (unsigned)r_init, (unsigned)r_open, hh, (unsigned)r_start, ok, bytes, peak, empty,
                    overrun, ended, invalid, other, last_other, hm, hd);
}
