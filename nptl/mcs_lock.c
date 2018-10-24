/* Copyright (C) 2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Kemi Wang <kemi.wang@intel.com>, 2018.

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

void mcs_lock (mcs_lock_t **lock, mcs_lock_t *node, int *token)
{
  mcs_lock_t *prev;
  /* Initialize node.  */

  node->next = NULL;
  node->locked = 0;
  node->tag = 0;

  prev = atomic_exchange_acquire(lock, node);

  /* No spinners waiting in the queue, lock is acquired immediately.  */
  if (prev == NULL)
    {
      node->locked = 1;
      // need check token in case someone has acquired the token
	  while (atomic_compare_and_exchange_val_acq (token, 1, 0) != 0)
	    atomic_spin_nop ();
      return;
    }

  /* Add current spinner into the queue.  */
  atomic_store_release (&prev->next, node);
  atomic_full_barrier ();
  /* Waiting unless waken up by the previous spinner or timeout.  */
  while (!atomic_load_relaxed (&node->locked))
	  atomic_spin_nop ();

  if (atomic_compare_and_exchange_val_acq(token, 1, 0) == 0)
     return;
  else
    goto loop;

loop:
  node->tag = 1;
  while(atomic_compare_and_exchange_val_acq (token, 1, 0) != 0)
  	atomic_spin_nop ();

  return;
}

void mcs_unlock (mcs_lock_t **lock, mcs_lock_t *node, int *token)
{

  atomic_store_release(token, 0);
  if (node->tag == 1)
  	return;
  mcs_lock_t *next; 
//do
//  {
loop:
  next = node->next;
  //mcs_lock_t *next = node->next;

  if (next == NULL)
    {
      /* Check the tail of the queue:
       * a) Release the lock and return if current node is the tail
       * (lock == node).  */
      if (atomic_compare_and_exchange_val_acq(lock, NULL, node) == node)
        return;

      /* b) Waiting until new node is added to the queue if current node is
       * not the tail (lock != node).  */
      while (! (next = atomic_load_relaxed (&node->next)))
        atomic_spin_nop ();
    }

//  if (next->locked == 1)
//    return;
  /* Wake up the next spinner.  */
  atomic_store_release (&next->locked, 1);
  atomic_full_barrier ();
  atomic_spin_nop ();
//  }
//  while (0);
//  while(atomic_load_relaxed (token) == 0);
 if (atomic_load_relaxed (token) == 0)
 {
   next->tag = 1;
   node = next;
   goto loop;
 }
}
