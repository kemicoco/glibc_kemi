/* MCS-style lock for queuing spinners
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

#include "pthreadP.h"
#include <atomic.h>

static __thread mcs_lock_t node = {
  NULL,
  0,
};

void
mcs_lock (mcs_lock_t **lock)
{
  mcs_lock_t *prev;

  /* Initialize node.  */
  node.next = NULL;
  node.locked = 0;

  /* Move the tail pointer of the queue (lock) to point to current TLS node
   * atomically, and return the previous tail pointer of the queue. This
   * atomic_exchange_acquire ensures the strict FIFO order in the queue when
   * multiple spinners compete to enqueue.  */
  prev = atomic_exchange_acquire(lock, &node);

  /* No active spinner, mcs lock is acquired immediately.  */
  if (prev == NULL)
    return;

  /* Add current spinner into the queue.  */
    atomic_store_relaxed (&prev->next, &node);
  /* Waiting until mcs lock holder is passed down from the previous spinner.
   * Using atomic_load_acquire to synchronizes-with the atomic_store_release
   * in mcs_unlock, and ensure that prior critical sections happen-before
   * this critical section.  */
  while (!atomic_load_acquire (&node.locked))
    atomic_spin_nop ();
}

void
mcs_unlock (mcs_lock_t **lock)
{
  mcs_lock_t *next = node.next;

  /* If the next pointer is not NULL, it indicates that we are not the last
   * spinner in the queue, thus we have to pass down mcs lock holder to the
   * next spinner before exiting. If the next pointer is NULL, there are
   * two possible cases, a) we are the last one in the queue, thus we reset
   * the tail pointer of the queue to NULL, b) the tail pointer of the
   * queue has moved to point to a new spinner, but this new spinner has
   * not been added to the queue. thus we have to wait until it is added to
   * the queue.  */
  if (next == NULL)
    {
      /* a) Use a CAS instruction to set lock to NULL if the tail pointer
       * points to current node.  */
      if (atomic_compare_and_exchange_val_acq(lock, NULL, &node) == &node)
        return;

      /* b) When we reaches here, we hit case b) aforementioned because
       * there is a time window between updating the tail pointer of queue
       * and adding a new spinner to the queue in mcs_lock (). Using
       * relax MO until we observe the new spinner is added to the queue.  */
      while (! (next = atomic_load_relaxed (&node.next)))
        atomic_spin_nop ();
    }

  /* Pass down mcs lock holder to next spinner, synchronize-with
   * atomic_load_acquire in mcs_lock.  */
  atomic_store_release (&next->locked, 1);
}
