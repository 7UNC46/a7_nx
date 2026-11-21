/* a7_cursor.c -- the menu pointer (right stick moves it, ZR taps) and the
 * garage crosshair.
 *
 * Not every Flash menu of the game answers the d-pad (the car screen's
 * paint / tuning / buy row, the music player): a pointer reaches all of
 * them, as a finger would. It is a ring in the HUD's blue with a white dot,
 * like a touch point, drawn just before each frame is presented
 * (port_gl_before_swap) with scissored clears only -- one row of pixels at a
 * time, no shader, buffer or texture -- and every piece of GL state it
 * touches is put back, so the engine's cached GL state stays true.
 * MIT.
 */
#include <GLES2/gl2.h>
#include <math.h>
#include <switch.h>

#include "a7.h"
#include "rt_window.h"

static float g_cx = 640.0f, g_cy = 360.0f; /* screen pixels, y down */
static int g_shown, g_pressed, g_cross;

void a7_cursor_set(float x, float y, int shown, int pressed, int crosshair) {
  g_cx = x;
  g_cy = y;
  g_shown = shown;
  g_pressed = pressed;
  g_cross = crosshair;
}

static int g_fb_h;

static void rect(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0)
    return;
  glScissor(x, g_fb_h - y - h, w, h);
  glClear(GL_COLOR_BUFFER_BIT);
}

static void color(float r, float g, float b) { glClearColor(r, g, b, 1.0f); }

/* The pixels of a ring between radii r_in and r_out around (cx, cy), one
 * row at a time (r_in 0: a disc). */
static void ring(int cx, int cy, float r_out, float r_in) {
  int top = (int)ceilf(r_out);
  for (int dy = -top; dy <= top; dy++) {
    float y = (float)dy;
    float o2 = r_out * r_out - y * y;
    if (o2 < 0)
      continue;
    int xo = (int)(sqrtf(o2) + 0.5f);
    float i2 = r_in * r_in - y * y;
    if (r_in <= 0 || i2 < 0) {
      rect(cx - xo, cy + dy, 2 * xo + 1, 1);
      continue;
    }
    int xi = (int)(sqrtf(i2) + 0.5f);
    rect(cx - xo, cy + dy, xo - xi, 1);         /* left part */
    rect(cx + xi + 1, cy + dy, xo - xi, 1);     /* right part */
  }
}

static void draw_pointer(int cx, int cy, float k) {
  float R = 12.0f * k, W = 3.5f * k;
  color(0.02f, 0.05f, 0.10f); /* dark edge, for bright backgrounds */
  ring(cx, cy, R + 1.5f * k, R - W - 1.5f * k);
  if (g_pressed)
    color(1.0f, 1.0f, 1.0f);
  else
    color(0.25f, 0.80f, 1.0f); /* the HUD's blue */
  ring(cx, cy, R, R - W);
  color(0.02f, 0.05f, 0.10f);
  ring(cx, cy, 4.0f * k, 0.0f);
  color(1.0f, 1.0f, 1.0f);
  ring(cx, cy, 2.5f * k, 0.0f);
}

static void draw_crosshair(int cx, int cy, float k) {
  int len = (int)(10 * k), gap = (int)(5 * k), t = (int)(2 * k) | 1, e = (int)k > 0 ? (int)k : 1;
  color(0.02f, 0.05f, 0.10f);
  rect(cx - gap - len - e, cy - t / 2 - e, len + 2 * e, t + 2 * e);
  rect(cx + gap - e, cy - t / 2 - e, len + 2 * e, t + 2 * e);
  rect(cx - t / 2 - e, cy - gap - len - e, t + 2 * e, len + 2 * e);
  rect(cx - t / 2 - e, cy + gap - e, t + 2 * e, len + 2 * e);
  if (g_pressed)
    color(1.0f, 1.0f, 1.0f);
  else
    color(0.25f, 0.80f, 1.0f);
  rect(cx - gap - len, cy - t / 2, len, t);
  rect(cx + gap, cy - t / 2, len, t);
  rect(cx - t / 2, cy - gap - len, t, len);
  rect(cx - t / 2, cy + gap, t, len);
  color(1.0f, 1.0f, 1.0f);
  ring(cx, cy, 1.5f * k, 0.0f);
}

void port_gl_before_swap(void) {
  if (!g_shown)
    return;
  int w = 1280, h = 720;
  dcr_window_size(&w, &h);
  g_fb_h = h;
  float k = (float)h / 720.0f;

  /* what is touched, as it was */
  GLint fbo = 0, box[4];
  GLfloat clear[4];
  GLboolean mask[4], scissor = glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_SCISSOR_BOX, box);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
  glGetBooleanv(GL_COLOR_WRITEMASK, mask);

  if (fbo)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!scissor)
    glEnable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  if (g_cross)
    draw_crosshair((int)g_cx, (int)g_cy, k);
  else
    draw_pointer((int)g_cx, (int)g_cy, k);

  glClearColor(clear[0], clear[1], clear[2], clear[3]);
  glColorMask(mask[0], mask[1], mask[2], mask[3]);
  glScissor(box[0], box[1], box[2], box[3]);
  if (!scissor)
    glDisable(GL_SCISSOR_TEST);
  if (fbo)
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
}
