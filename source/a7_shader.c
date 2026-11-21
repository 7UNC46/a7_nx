/* a7_shader.c -- the game's GLSL, made acceptable to Mesa.
 *
 * Some of Asphalt 7's shaders (race start, the cutscene before the countdown,
 * the results screen; hardware log 2026-11-20) call
 *     texture2D(sampler2D, vec3)
 * which GLSL ES 1.00 has no overload for. The phones' compilers it was made
 * on (Tegra's above all) took the vec3 and used its .xy; Mesa rejects the
 * shader, the program does not link, and whatever it draws is missing.
 *
 * Here glCompileShader compiles as asked; only when that fails and the log
 * names texture2D is the source rewritten -- every texture2D( call goes
 * through a7_tex2D, whose overloads take vec2 / vec3 / vec4 (and a bias in
 * fragment shaders) and pass .xy on -- and compiled again. Shaders that
 * compiled are never touched. The first failures are logged with their
 * source, rewritten or not, so a shader that still fails can be read in
 * debug.log. MIT.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "util.h"

typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef char GLchar;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_SHADER_SOURCE_LENGTH 0x8B88
#define GL_SHADER_TYPE 0x8B4F

static void (*r_ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*r_CompileShader)(GLuint);
static void (*r_GetShaderiv)(GLuint, GLenum, GLint *);
static void (*r_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static void (*r_GetShaderSource)(GLuint, GLsizei, GLsizei *, GLchar *);
void *eglGetProcAddress(const char *);

static int g_logged, g_fixed;

static const char k_helpers_common[] =
    "mediump vec4 a7_tex2D(sampler2D a7s, mediump vec2 a7c) { return texture2D(a7s, a7c); }\n"
    "mediump vec4 a7_tex2D(sampler2D a7s, mediump vec3 a7c) { return texture2D(a7s, a7c.xy); }\n"
    "mediump vec4 a7_tex2D(sampler2D a7s, mediump vec4 a7c) { return texture2D(a7s, a7c.xy); }\n";
static const char k_helpers_frag[] =
    "mediump vec4 a7_tex2D(sampler2D a7s, mediump vec2 a7c, mediump float a7b) { return texture2D(a7s, a7c, a7b); }\n"
    "mediump vec4 a7_tex2D(sampler2D a7s, mediump vec3 a7c, mediump float a7b) { return texture2D(a7s, a7c.xy, a7b); }\n";

static int ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* The source with texture2D( -> a7_tex2D( and the helpers after the leading
 * #version / #extension lines (they must come first). malloc'd. */
static char *rewrite(const char *src, int frag) {
  size_t n = strlen(src), extra = sizeof k_helpers_common + sizeof k_helpers_frag + 16;
  size_t calls = 0;
  for (const char *p = strstr(src, "texture2D"); p; p = strstr(p + 9, "texture2D"))
    calls++;
  char *out = malloc(n + extra + calls * 2 + 1);
  if (!out)
    return NULL;
  /* where the helpers go: after the last leading # line that is #version
   * or #extension (blank lines and comments in between are rare) */
  const char *ins = src, *p = src;
  for (;;) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
      p++;
    if (!strncmp(p, "#version", 8) || !strncmp(p, "#extension", 10)) {
      const char *e = strchr(p, '\n');
      p = e ? e + 1 : p + strlen(p);
      ins = p;
      continue;
    }
    break;
  }
  size_t o = 0;
  memcpy(out, src, (size_t)(ins - src));
  o = (size_t)(ins - src);
  if (o && out[o - 1] != '\n')
    out[o++] = '\n';
  memcpy(out + o, k_helpers_common, sizeof k_helpers_common - 1);
  o += sizeof k_helpers_common - 1;
  if (frag) {
    memcpy(out + o, k_helpers_frag, sizeof k_helpers_frag - 1);
    o += sizeof k_helpers_frag - 1;
  }
  for (const char *s = ins; *s;) {
    if (!strncmp(s, "texture2D", 9) && (s == src || !ident_char(s[-1]))) {
      const char *q = s + 9;
      while (*q == ' ' || *q == '\t')
        q++;
      if (*q == '(') {
        memcpy(out + o, "a7_tex2D", 8);
        o += 8;
        s += 9;
        continue;
      }
    }
    out[o++] = *s++;
  }
  out[o] = 0;
  return out;
}

static void log_source(const char *why, const char *src) {
  debugPrintf("[shader] %s -- source:\n", why);
  /* line by line, numbered as the compiler numbers them */
  int line = 1;
  for (const char *p = src; *p;) {
    const char *e = strchr(p, '\n');
    int len = e ? (int)(e - p) : (int)strlen(p);
    debugPrintf("[shader] %3d: %.*s\n", line++, len > 200 ? 200 : len, p);
    if (!e)
      break;
    p = e + 1;
  }
}

static int compiled(GLuint sh) {
  GLint ok = 0;
  r_GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  return ok;
}

static char *info_log(GLuint sh) {
  GLint n = 0;
  r_GetShaderiv(sh, GL_INFO_LOG_LENGTH, &n);
  char *s = malloc(n > 0 ? (size_t)n + 1 : 1);
  if (s) {
    s[0] = 0;
    if (n > 0)
      r_GetShaderInfoLog(sh, n + 1, NULL, s);
  }
  return s;
}

static char *source_of(GLuint sh) {
  GLint n = 0;
  r_GetShaderiv(sh, GL_SHADER_SOURCE_LENGTH, &n);
  if (n <= 0)
    return NULL;
  char *s = malloc((size_t)n + 1);
  if (s)
    r_GetShaderSource(sh, n + 1, NULL, s);
  return s;
}

static void w_CompileShader(GLuint sh) {
  r_CompileShader(sh);
  if (compiled(sh))
    return;
  char *log = info_log(sh), *src = source_of(sh);
  if (log && src && strstr(log, "texture2D")) {
    GLint type = 0;
    r_GetShaderiv(sh, GL_SHADER_TYPE, &type);
    char *fixed = rewrite(src, type == GL_FRAGMENT_SHADER);
    if (fixed) {
      const GLchar *one = fixed;
      r_ShaderSource(sh, 1, &one, NULL);
      r_CompileShader(sh);
      if (compiled(sh)) {
        if (g_fixed++ < 8)
          debugPrintf("[shader] %s shader %u: texture2D with a vec3/vec4 coordinate -> .xy; compiles now\n",
                      type == GL_FRAGMENT_SHADER ? "fragment" : "vertex", sh);
        free(fixed);
        free(log);
        free(src);
        return;
      }
      free(log);
      log = info_log(sh);
      if (g_logged < 4) {
        g_logged++;
        debugPrintf("[shader] shader %u still fails after the texture2D rewrite: %s\n", sh, log ? log : "");
        log_source("rewritten", fixed);
      }
      free(fixed);
    }
  } else if (g_logged < 4) {
    g_logged++;
    debugPrintf("[shader] shader %u does not compile: %s\n", sh, log ? log : "");
    if (src)
      log_source("as the game gave it", src);
  }
  free(log);
  free(src);
}

uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  if (!strcmp(name, "glCompileShader") && real) {
    r_CompileShader = (void *)real;
    r_ShaderSource = eglGetProcAddress("glShaderSource");
    r_GetShaderiv = eglGetProcAddress("glGetShaderiv");
    r_GetShaderInfoLog = eglGetProcAddress("glGetShaderInfoLog");
    r_GetShaderSource = eglGetProcAddress("glGetShaderSource");
    if (r_ShaderSource && r_GetShaderiv && r_GetShaderInfoLog && r_GetShaderSource)
      return (uintptr_t)w_CompileShader;
  }
  return 0;
}
