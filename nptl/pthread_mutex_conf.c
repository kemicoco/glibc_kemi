/* pthread_mutex_conf.c: Pthread mutex tunable parameters.
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

#include "config.h"
#include <pthreadP.h>
#include <init-arch.h>
#include <pthread_mutex_conf.h>
#include <unistd.h>

#if HAVE_TUNABLES
# define TUNABLE_NAMESPACE mutex
#endif
#include <elf/dl-tunables.h>


struct mutex_config __mutex_aconf =
{
  /* The maximum number of times a thread should spin on the lock before
  calling into kernel to block.  */
  .spin_count = 100,
};

#if HAVE_TUNABLES
static inline void __always_inline
do_set_mutex_spin_count (int32_t value)
{
  __mutex_aconf.spin_count = value;
}

void
TUNABLE_CALLBACK (set_mutex_spin_count) (tunable_val_t *valp)
{
  int32_t value = (int32_t) (valp)->numval;
  do_set_mutex_spin_count (value);
}
#endif

static void
mutex_tunables_init (int argc __attribute__ ((unused)),
			      char **argv  __attribute__ ((unused)),
					      char **environ)
{
#if HAVE_TUNABLES
  TUNABLE_GET (spin_count, int32_t,
               TUNABLE_CALLBACK (set_mutex_spin_count));
#endif
}

#ifdef SHARED
# define INIT_SECTION ".init_array"
#else
# define INIT_SECTION ".preinit_array"
#endif

void (*const __pthread_mutex_tunables_init_array []) (int, char **, char **)
  __attribute__ ((section (INIT_SECTION), aligned (sizeof (void *)))) =
{
  &mutex_tunables_init
};
