/* Benchmark pthread adaptive spin mutex lock and unlock functions.
   Copyright (C) 2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sched.h>

#include "bench-timing.h"
#include "json-lib.h"

/* Benchmark duration in seconds.  */
#define BENCHMARK_DURATION	15
#define TYPE PTHREAD_MUTEX_ADAPTIVE_NP
#define mb() asm ("" ::: "memory")
#define UNLOCK_DELAY 10

#if defined (__i386__) || defined (__x86_64__)
# define cpu_relax() asm ("rep; nop")
#else
# define cpu_relax() do { } while (0)
#endif

static volatile int start_thread;
static unsigned long long val;
static pthread_mutexattr_t attr;
static pthread_mutex_t mutex;

#define WORKING_SET_SIZE  4
int working_set[] = {1, 10, 100, 1000};

struct thread_args
{
  unsigned long long iters;
  int working_set;
  timing_t elapsed;
};

static void
init_mutex (void)
{
  pthread_mutexattr_init (&attr);
  pthread_mutexattr_settype (&attr, TYPE);
  pthread_mutex_init (&mutex, &attr);
}

static void
init_parameter (int size, struct thread_args *args, int num_thread)
{
  int i;
  for (i = 0; i < num_thread; i++)
    {
      memset(&args[i], 0, sizeof(struct thread_args));
      args[i].working_set = size;
    }
}

static volatile bool timeout;

static void
alarm_handler (int signum)
{
  timeout = true;
}

static inline void
delay (int number)
{
  while (number > 0)
  {
    cpu_relax ();
    number--;
  }
}

/* Lock and unlock for protecting the critical section.  */
static unsigned long long
mutex_benchmark_loop (int number)
{
  unsigned long long iters = 0;

  while (!start_thread)
   cpu_relax ();

  while (!timeout)
    {
      pthread_mutex_lock (&mutex);
      val++;
      delay (number);
      pthread_mutex_unlock (&mutex);
      iters++;
      delay (UNLOCK_DELAY);
    }
  return iters;
}

static void *
benchmark_thread (void *arg)
{
  struct thread_args *args = (struct thread_args *) arg;
  unsigned long long iters;
  timing_t start, stop;

  TIMING_NOW (start);
  iters = mutex_benchmark_loop (args->working_set);
  TIMING_NOW (stop);

  TIMING_DIFF (args->elapsed, start, stop);
  args->iters = iters;

  return NULL;
}

static void
do_benchmark (size_t num_thread, struct thread_args *args)
{

  pthread_t threads[num_thread];

  for (size_t i = 0; i < num_thread; i++)
    {
      pthread_attr_t attr;
      cpu_set_t set;

      pthread_attr_init (&attr);
      CPU_ZERO (&set);
      CPU_SET (i, &set);
      pthread_attr_setaffinity_np (&attr, sizeof(cpu_set_t), &set);
      pthread_create (&threads[i], &attr, benchmark_thread, args + i);
      pthread_attr_destroy (&attr);
    }

  mb ();
  start_thread = 1;
  mb ();
  sched_yield ();
  for (size_t i = 0; i < num_thread; i++)
    pthread_join(threads[i], NULL);
}

static void
usage(const char *name)
{
  fprintf (stderr, "%s: <num_thread>\n", name);
  exit (1);
}

int
main (int argc, char **argv)
{
  int i, j, num_thread = 1;
  json_ctx_t json_ctx;
  struct sigaction act;

  if (argc == 1)
    num_thread = 1;
  else if (argc == 2)
    {
      long ret;

      errno = 0;
      ret = strtol(argv[1], NULL, 10);

      if (errno || ret == 0)
	    usage(argv[0]);

      num_thread = ret;
    }
  else
    usage(argv[0]);

  /* Benchmark for different critical section size.  */
  for (i = 0; i < WORKING_SET_SIZE; i++)
    {
      int size = working_set[i];
      struct thread_args args[num_thread];
      unsigned long long iters = 0, min_iters = -1ULL, max_iters = 0;
      double d_total_s = 0, d_total_i = 0;

      timeout = false;
      init_mutex ();
      init_parameter (size, args, num_thread);

      json_init (&json_ctx, 0, stdout);

      json_document_begin (&json_ctx);

      json_attr_string (&json_ctx, "timing_type", TIMING_TYPE);

      json_attr_object_begin (&json_ctx, "functions");

      json_attr_object_begin (&json_ctx, "mutex");

      json_attr_object_begin (&json_ctx, "");

      memset (&act, 0, sizeof (act));
      act.sa_handler = &alarm_handler;

      sigaction (SIGALRM, &act, NULL);

      alarm (BENCHMARK_DURATION);

      do_benchmark (num_thread, args);

      for (j = 0; j < num_thread; j++)
        {
          iters = args[j].iters;
          if (iters < min_iters)
            min_iters = iters;
          if (iters >= max_iters)
            max_iters = iters;
          d_total_i += iters;
          TIMING_ACCUM (d_total_s, args[j].elapsed);
        }
      json_attr_double (&json_ctx, "duration", d_total_s);
      json_attr_double (&json_ctx, "total_iterations", d_total_i);
      json_attr_double (&json_ctx, "min_iteration", min_iters);
      json_attr_double (&json_ctx, "max_iteration", max_iters);
      json_attr_double (&json_ctx, "time_per_iteration", d_total_s / d_total_i);
      json_attr_double (&json_ctx, "threads", num_thread);
      json_attr_double (&json_ctx, "critical_section_size", size);

      json_attr_object_end (&json_ctx);
      json_attr_object_end (&json_ctx);
      json_attr_object_end (&json_ctx);

      json_document_end (&json_ctx);
      fputs("\n", (&json_ctx)->fp);
    }
  return 0;
}
