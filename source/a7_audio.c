/* a7_audio.c -- android.media.AudioTrack, on audout.
 *
 * Asphalt 7's Vox mixer does not use OpenSL ES here: it runs its own thread
 * and streams PCM through a Java AudioTrack (MODE_STREAM):
 *   AudioTrack.getMinBufferSize(rate, channelConfig, encoding)
 *   new AudioTrack(stream, rate, channelConfig, encoding, bytes, mode)
 *   play(), then write(byte[], offset, size) in a loop.
 * On a phone write() BLOCKS until the track has room -- that is what paces
 * the mixer thread. Unhandled, write() returned 0 at once: no sound, and the
 * mixer thread spun at full speed on the one core every guest thread shares
 * (dcr_sched.c), taking time from the game's own frames.
 *
 * Here write() converts to the device's 48 kHz stereo s16 (linear resampling),
 * fills RT_AUDOUT_FRAMES-frame buffers and hands each to rt_audout_submit,
 * which blocks while all three audout buffers are queued: the same pacing.
 * One track streams at a time (Vox makes one). MIT.
 */
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "jni.h"
#include "rt_audout.h"
#include "util.h"

/* AudioFormat / AudioTrack constants (API 5+; the deprecated CHANNEL_
 * CONFIGURATION_MONO/STEREO are 2/3). */
#define CH_OUT_MONO        4
#define CH_OUT_STEREO      12
#define CH_CONF_MONO       2
#define CH_CONF_STEREO     3
#define ENC_PCM_16BIT      2
#define ENC_PCM_8BIT       3
#define STATE_INITIALIZED  1
#define PLAYSTATE_STOPPED  1
#define PLAYSTATE_PAUSED   2
#define PLAYSTATE_PLAYING  3

typedef struct {
  int rate, ch, bits;
  int state;               /* PLAYSTATE_* */
  float vol_l, vol_r;
  uint32_t head;           /* frames played (getPlaybackHeadPosition) */
  /* resampler: position in input frames, 16.16; the previous input frame */
  uint32_t step;           /* (rate << 16) / 48000 */
  uint32_t frac;
  int16_t prev[2];
} Track;

static int16_t g_out[RT_AUDOUT_FRAMES * 2];
static int g_out_n;              /* frames in g_out */
static Track *g_active;          /* the track that owns g_out */
static int g_open_failed;
static unsigned long g_writes;

static int channels_of(int conf) {
  return (conf == CH_OUT_STEREO || conf == CH_CONF_STEREO) ? 2 : 1;
}

static void flush_out(void) {
  if (g_out_n < RT_AUDOUT_FRAMES)
    return;
  if (rt_audout_submit(g_out) != 0)
    /* not open / cancelled at exit: keep the caller's pace anyway */
    svcSleepThread((s64)RT_AUDOUT_FRAMES * 1000000000ll / 48000);
  g_out_n = 0;
}

static inline void in_frame(const Track *t, const uint8_t *in, int i, int16_t o[2]) {
  if (t->bits == 16) {
    const int16_t *s = (const int16_t *)in + i * t->ch;
    o[0] = s[0];
    o[1] = t->ch == 2 ? s[1] : s[0];
  } else { /* 8-bit unsigned */
    const uint8_t *s = in + i * t->ch;
    o[0] = (int16_t)((s[0] - 128) << 8);
    o[1] = t->ch == 2 ? (int16_t)((s[1] - 128) << 8) : o[0];
  }
}

static inline int16_t clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }

/* Resample n input frames into g_out, submitting each full buffer. */
static void push_pcm(Track *t, const uint8_t *in, int n) {
  if (n <= 0)
    return;
  const int vl = (int)(t->vol_l * 256.0f), vr = (int)(t->vol_r * 256.0f);
  /* frac: 16.16 position, relative to the first frame of this block; the
   * frame at -1 is t->prev. */
  uint32_t pos = t->frac;
  for (;;) {
    int i = (int)(pos >> 16);
    if (i >= n)
      break;
    int f = (int)(pos & 0xFFFF);
    int16_t a[2], b[2];
    if (i == 0) {
      a[0] = t->prev[0], a[1] = t->prev[1];
    } else {
      in_frame(t, in, i - 1, a);
    }
    in_frame(t, in, i, b);
    int l = a[0] + (((b[0] - a[0]) * f) >> 16);
    int r = a[1] + (((b[1] - a[1]) * f) >> 16);
    g_out[g_out_n * 2] = clamp16((l * vl) >> 8);
    g_out[g_out_n * 2 + 1] = clamp16((r * vr) >> 8);
    if (++g_out_n == RT_AUDOUT_FRAMES)
      flush_out();
    pos += t->step;
  }
  in_frame(t, in, n - 1, t->prev);
  t->frac = pos - ((uint32_t)n << 16);
  t->head += (uint32_t)n;
}

static Track *track_of(JObj *self) { return self ? (Track *)self->p : NULL; }

static void track_finalize(JObj *self) {
  Track *t = track_of(self);
  if (t == g_active)
    g_active = NULL;
  free(t);
  self->p = NULL;
}

/* ---------------------------------------------------------------- handlers */
JNI_H_DECL(a7_at_min_buffer) {
  int rate = a ? a[0].i : 44100, ch = channels_of(a ? a[1].i : CH_OUT_STEREO);
  int bytes = (a && a[2].i == ENC_PCM_8BIT) ? 1 : 2;
  /* about 46 ms, as an ICS phone's mixer answers for 44.1 kHz */
  int frames = rate / 22 > 1024 ? rate / 22 : 1024;
  return jv_i(frames * ch * bytes);
}

JNI_H_DECL(a7_at_init) {
  Track *t = calloc(1, sizeof *t);
  if (!t || !self)
    return jv_none();
  t->rate = a ? a[1].i : 44100;
  if (t->rate < 4000 || t->rate > 96000)
    t->rate = 44100;
  t->ch = channels_of(a ? a[2].i : CH_OUT_STEREO);
  t->bits = (a && a[3].i == ENC_PCM_8BIT) ? 8 : 16;
  t->state = PLAYSTATE_STOPPED;
  t->vol_l = t->vol_r = 1.0f;
  t->step = (uint32_t)(((uint64_t)t->rate << 16) / rt_audout_rate());
  self->p = t;
  self->finalize = track_finalize;
  if (!g_open_failed && rt_audout_open() != 0)
    g_open_failed = 1;
  debugPrintf("[audio] AudioTrack(stream %d, %d Hz, %s, %d-bit, %d bytes, mode %d)%s\n", a ? a[0].i : 0,
              t->rate, t->ch == 2 ? "stereo" : "mono", t->bits, a ? a[4].i : 0, a ? a[5].i : 0,
              g_open_failed ? ": audout did not open, silent" : " -> audout");
  return jv_none();
}

JNI_H_DECL(a7_at_play) {
  Track *t = track_of(self);
  if (t) {
    t->state = PLAYSTATE_PLAYING;
    if (g_active != t) {
      g_active = t;
      g_out_n = 0;
    }
  }
  return jv_none();
}

JNI_H_DECL(a7_at_pause) {
  Track *t = track_of(self);
  if (t)
    t->state = PLAYSTATE_PAUSED;
  return jv_none();
}

JNI_H_DECL(a7_at_stop) {
  Track *t = track_of(self);
  if (t) {
    t->state = PLAYSTATE_STOPPED;
    t->head = 0;
  }
  return jv_none();
}

JNI_H_DECL(a7_at_flush) {
  Track *t = track_of(self);
  if (t == g_active)
    g_out_n = 0;
  return jv_none();
}

JNI_H_DECL(a7_at_release) {
  Track *t = track_of(self);
  if (t) {
    t->state = PLAYSTATE_STOPPED;
    if (t == g_active)
      g_active = NULL;
  }
  return jv_none();
}

/* write(byte[] data, int offset, int size) or write(short[], int, int):
 * blocks while audout is full; returns what was taken (in the array's units). */
JNI_H_DECL(a7_at_write) {
  Track *t = track_of(self);
  JObj *arr = a ? (JObj *)a[0].l : NULL;
  int off = a ? a[1].i : 0, size = a ? a[2].i : 0;
  if (!t || !arr || arr->kind != JK_ARRAY || size <= 0 || off < 0 || off + size > arr->a.len)
    return jv_i(-3); /* ERROR_INVALID_OPERATION... -2 bad value; either way */
  int unit = arr->a.elem == 'S' ? 2 : 1;
  const uint8_t *p = (const uint8_t *)arr->a.data + (size_t)off * unit;
  int bytes = size * unit;
  int frame_bytes = t->ch * (t->bits / 8);
  if (g_writes++ == 0)
    debugPrintf("[audio] first AudioTrack.write: %d bytes\n", bytes);
  if (t->state != PLAYSTATE_PLAYING) {
    /* a phone buffers it until play(); keep the mixer's pace */
    svcSleepThread((s64)(bytes / frame_bytes) * 1000000000ll / t->rate);
    return jv_i(size);
  }
  if (g_active != t) {
    g_active = t;
    g_out_n = 0;
  }
  push_pcm(t, p, bytes / frame_bytes);
  return jv_i(size);
}

JNI_H_DECL(a7_at_volume) { /* setStereoVolume(float, float) */
  Track *t = track_of(self);
  if (t && a) {
    t->vol_l = a[0].f < 0 ? 0 : a[0].f > 1 ? 1 : a[0].f;
    t->vol_r = a[1].f < 0 ? 0 : a[1].f > 1 ? 1 : a[1].f;
  }
  return jv_i(0);
}

JNI_H_DECL(a7_at_state) { return jv_i(track_of(self) ? STATE_INITIALIZED : 0); }

JNI_H_DECL(a7_at_play_state) {
  Track *t = track_of(self);
  return jv_i(t ? t->state : PLAYSTATE_STOPPED);
}

JNI_H_DECL(a7_at_rate) {
  Track *t = track_of(self);
  return jv_i(t ? t->rate : 44100);
}

JNI_H_DECL(a7_at_channels) {
  Track *t = track_of(self);
  return jv_i(t ? t->ch : 2);
}

JNI_H_DECL(a7_at_head) {
  Track *t = track_of(self);
  return jv_i(t ? (jint)t->head : 0);
}

JNI_H_DECL(a7_at_max_volume) { return jv_f(1.0f); }
JNI_H_DECL(a7_at_min_volume) { return jv_f(0.0f); }
