/*
 *  OpenGL call profiler (diagnostics).
 *
 *  The GL entry points Kodi's video renderer uses are wrapped at link time
 *  (lld --wrap, see scripts/30-deploy.sh). With the kodi-debug switch, each
 *  call is timed; every 5 seconds in which the wrapped calls took more than
 *  50 ms in total, a table of call counts and times goes to klog, showing where
 *  render time goes. Without the switch the wrappers only forward.
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int sceKernelDebugOutText(int channel, const char* text);

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef uint64_t GLuint64;
typedef struct __GLsync* GLsync;

enum
{
  P_TEXSUBIMAGE2D,
  P_TEXIMAGE2D,
  P_MAPBUFFER,
  P_MAPBUFFERRANGE,
  P_UNMAPBUFFER,
  P_BUFFERDATA,
  P_BUFFERSUBDATA,
  P_DRAWARRAYS,
  P_DRAWELEMENTS,
  P_CLIENTWAITSYNC,
  P_FENCESYNC,
  P_FINISH,
  P_FLUSH,
  P_CLEAR,
  P_COUNT
};

static const char* const kNames[P_COUNT] = {
    "glTexSubImage2D", "glTexImage2D",    "glMapBuffer",      "glMapBufferRange",
    "glUnmapBuffer",   "glBufferData",    "glBufferSubData",  "glDrawArrays",
    "glDrawElements",  "glClientWaitSync", "glFenceSync",     "glFinish",
    "glFlush",         "glClear"};

static struct
{
  uint64_t calls, ns, maxNs;
} g_stats[P_COUNT];
static uint64_t g_windowStart;

static uint64_t now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int g_enabled = -1;
static int enabled(void)
{
  if (g_enabled < 0)
    g_enabled = access("/app0/kodi-debug", F_OK) == 0;
  return g_enabled;
}

static void account(int which, uint64_t start)
{
  if (!enabled())
    return;
  const uint64_t end = now_ns();
  const uint64_t took = end - start;
  g_stats[which].calls++;
  g_stats[which].ns += took;
  if (took > g_stats[which].maxNs)
    g_stats[which].maxNs = took;

  if (!g_windowStart)
    g_windowStart = end;
  if (end - g_windowStart < 5000000000ull)
    return;

  uint64_t total = 0;
  for (int i = 0; i < P_COUNT; ++i)
    total += g_stats[i].ns;
  if (total > 50000000ull) /* > 50 ms of GL time in the window: report */
  {
    char line[160];
    snprintf(line, sizeof(line), "[kodi-ps5] GL profile over %.1f s (%.0f ms in GL):\n",
             (end - g_windowStart) / 1e9, total / 1e6);
    sceKernelDebugOutText(0, line);
    for (int i = 0; i < P_COUNT; ++i)
    {
      if (!g_stats[i].calls)
        continue;
      snprintf(line, sizeof(line),
               "[kodi-ps5]   %-17s %6llu calls  %8.1f ms total  %7.2f ms avg  %7.2f ms max\n",
               kNames[i], (unsigned long long)g_stats[i].calls, g_stats[i].ns / 1e6,
               g_stats[i].ns / 1e6 / g_stats[i].calls, g_stats[i].maxNs / 1e6);
      sceKernelDebugOutText(0, line);
    }
  }
  for (int i = 0; i < P_COUNT; ++i)
    g_stats[i].calls = g_stats[i].ns = g_stats[i].maxNs = 0;
  g_windowStart = end;
}

#define TIMED_VOID(which, call) \
  do \
  { \
    const uint64_t t0 = now_ns(); \
    call; \
    account(which, t0); \
  } while (0)

void __real_glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                            const void*);
void __wrap_glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f,
                            GLenum ty, const void* p)
{
  TIMED_VOID(P_TEXSUBIMAGE2D, __real_glTexSubImage2D(t, l, x, y, w, h, f, ty, p));
}

void __real_glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*);
void __wrap_glTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f,
                         GLenum ty, const void* p)
{
  TIMED_VOID(P_TEXIMAGE2D, __real_glTexImage2D(t, l, i, w, h, b, f, ty, p));
}

void* __real_glMapBuffer(GLenum, GLenum);
void* __wrap_glMapBuffer(GLenum t, GLenum a)
{
  const uint64_t t0 = now_ns();
  void* r = __real_glMapBuffer(t, a);
  account(P_MAPBUFFER, t0);
  return r;
}

void* __real_glMapBufferRange(GLenum, GLintptr, GLsizeiptr, GLbitfield);
void* __wrap_glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr l, GLbitfield a)
{
  const uint64_t t0 = now_ns();
  void* r = __real_glMapBufferRange(t, o, l, a);
  account(P_MAPBUFFERRANGE, t0);
  return r;
}

unsigned char __real_glUnmapBuffer(GLenum);
unsigned char __wrap_glUnmapBuffer(GLenum t)
{
  const uint64_t t0 = now_ns();
  unsigned char r = __real_glUnmapBuffer(t);
  account(P_UNMAPBUFFER, t0);
  return r;
}

void __real_glBufferData(GLenum, GLsizeiptr, const void*, GLenum);
void __wrap_glBufferData(GLenum t, GLsizeiptr s, const void* d, GLenum u)
{
  TIMED_VOID(P_BUFFERDATA, __real_glBufferData(t, s, d, u));
}

void __real_glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void*);
void __wrap_glBufferSubData(GLenum t, GLintptr o, GLsizeiptr s, const void* d)
{
  TIMED_VOID(P_BUFFERSUBDATA, __real_glBufferSubData(t, o, s, d));
}

void __real_glDrawArrays(GLenum, GLint, GLsizei);
void __wrap_glDrawArrays(GLenum m, GLint f, GLsizei c)
{
  TIMED_VOID(P_DRAWARRAYS, __real_glDrawArrays(m, f, c));
}

void __real_glDrawElements(GLenum, GLsizei, GLenum, const void*);
void __wrap_glDrawElements(GLenum m, GLsizei c, GLenum t, const void* i)
{
  TIMED_VOID(P_DRAWELEMENTS, __real_glDrawElements(m, c, t, i));
}

GLenum __real_glClientWaitSync(GLsync, GLbitfield, GLuint64);
GLenum __wrap_glClientWaitSync(GLsync s, GLbitfield f, GLuint64 t)
{
  const uint64_t t0 = now_ns();
  GLenum r = __real_glClientWaitSync(s, f, t);
  account(P_CLIENTWAITSYNC, t0);
  return r;
}

GLsync __real_glFenceSync(GLenum, GLbitfield);
GLsync __wrap_glFenceSync(GLenum c, GLbitfield f)
{
  const uint64_t t0 = now_ns();
  GLsync r = __real_glFenceSync(c, f);
  account(P_FENCESYNC, t0);
  return r;
}

void __real_glFinish(void);
void __wrap_glFinish(void)
{
  TIMED_VOID(P_FINISH, __real_glFinish());
}

void __real_glFlush(void);
void __wrap_glFlush(void)
{
  TIMED_VOID(P_FLUSH, __real_glFlush());
}

void __real_glClear(GLbitfield);
void __wrap_glClear(GLbitfield m)
{
  TIMED_VOID(P_CLEAR, __real_glClear(m));
}
