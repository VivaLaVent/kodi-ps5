/*
 *  Minimum thread stack size for the title.
 *
 *  Kodi (std::thread), FFmpeg and libc++ create threads with default
 *  attributes, and a title's default pthread stack is far smaller than on a
 *  desktop OS - the first thumbnail encode overflowed it. Every
 *  pthread_create() is routed here (lld --wrap=pthread_create, see
 *  scripts/30-deploy.sh) and gets at least PS5_MIN_THREAD_STACK.
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>

#define PS5_MIN_THREAD_STACK (1024u * 1024u) /* as Kodi gets on Android */

int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                          void* (*start)(void*), void* arg);
int sceKernelDebugOutText(int channel, const char* text);

static void log_default_once(size_t system_default)
{
  static int logged;
  if (logged)
    return;
  logged = 1;
  char msg[160];
  snprintf(msg, sizeof(msg),
           "[kodi-ps5] threads: system default stack %zu KiB, raised to at least %u KiB\n",
           system_default / 1024, PS5_MIN_THREAD_STACK / 1024);
  sceKernelDebugOutText(0, msg);
}

int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                          void* (*start)(void*), void* arg)
{
  pthread_attr_t local;
  const pthread_attr_t* use = attr;
  int own = 0;
  size_t size = 0;

  if (!attr)
  {
    if (pthread_attr_init(&local) == 0)
    {
      if (pthread_attr_getstacksize(&local, &size) == 0)
        log_default_once(size);
      if (size < PS5_MIN_THREAD_STACK)
        pthread_attr_setstacksize(&local, PS5_MIN_THREAD_STACK);
      use = &local;
      own = 1;
    }
  }
  else if (pthread_attr_getstacksize(attr, &size) == 0 && size < PS5_MIN_THREAD_STACK)
  {
    /* callers hand us their own attr object; raising its stack size is harmless */
    pthread_attr_setstacksize((pthread_attr_t*)attr, PS5_MIN_THREAD_STACK);
  }

  const int result = __real_pthread_create(thread, use, start, arg);
  if (own)
    pthread_attr_destroy(&local);
  return result;
}
