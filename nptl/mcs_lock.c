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

void mcs_lock (mcs_lock_t **lock, mcs_lock_t *node)
{
  mcs_lock_t *prev;
  /* Initialize node.  */
begin:
  node->next = NULL;
  node->locked = 0;
  node->tag = 0;

  prev = atomic_exchange_acquire(lock, node);

  /* No spinners waiting in the queue, lock is acquired immediately.  */
  if (prev == NULL)
    {
      node->locked = 1;
	  (*lock)->tag = 1;
      return;
    }

  /* Add current spinner into the queue.  */
  atomic_store_release (&prev->next, node);
  atomic_full_barrier ();
  /* Waiting unless waken up by the previous spinner or timeout.  */
  while (!atomic_load_relaxed (&node->locked))
	  atomic_spin_nop ();


  if (atomic_compare_and_exchange_val_acq(&((*lock)->tag), 1, 0) == 0)
     return;
  else
    goto begin;
}

void mcs_unlock (mcs_lock_t **lock, mcs_lock_t *node)
{

  atomic_store_release(&((*lock)->tag), 0);
do
  {
  mcs_lock_t *next = node->next;

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

  if (next->locked == 1)
    return;
  /* Wake up the next spinner.  */
  atomic_store_release (&next->locked, 1);

  atomic_full_barrier ();
  atomic_spin_nop();
  }
  while((*lock)->tag == 0);
}








//void mcs_lock (mcs_lock_t **lock, mcs_lock_t *node)
//{
//  mcs_lock_t *prev;
//  int cnt = 0;
//
//  /* Black node is allowed to spin on mutex immediately */
//  if (node->locked == 2)
//    {
//      node->tag = 1;
//      node->next = NULL;
//      return;
//    }
//  /* Initialize node.  */
//  node->next = NULL;
//  node->locked = 0;
//  node->tag = 0;
//
//  prev = atomic_exchange_acquire(lock, node);
//
//  /* No spinners waiting in the queue, lock is acquired immediately.  */
//  if (prev == NULL)
//    {
//      node->locked = 1;
//      return;
//    }
//
//  /* Add current spinner into the queue.  */
//  atomic_store_release (&prev->next, node);
//  atomic_full_barrier ();
//  /* Waiting unless waken up by the previous spinner or timeout.  */
//  while (!atomic_load_relaxed (&node->locked))
//    {
//	  atomic_spin_nop ();
//	  /* If timeout, mark this node black before get the permission.  */
//	  if (++cnt >= MAX_ADAPTIVE_COUNT)
//        {
//          /* Make node->locked = 2 to mark a node black */
//          atomic_compare_and_exchange_val_acq (&node->locked, 2, 0);
//          return;
//        }
//    }
//}
//
//void mcs_unlock (mcs_lock_t **lock, mcs_lock_t *node)
//{
//  if (node->tag == 1)
//	  return;
//
//  mcs_lock_t *next = node->next;
//
//  if (next == NULL)
//    {
//      /* Check the tail of the queue:
//       * a) Release the lock and return if current node is the tail
//       * (lock == node).  */
//      if (atomic_compare_and_exchange_val_acq(lock, NULL, node) == node)
//        return;
//
//      /* b) Waiting until new node is added to the queue if current node is
//       * not the tail (lock != node).  */
//      while (! (next = atomic_load_relaxed (&node->next)))
//        atomic_spin_nop ();
//    }
//  /* Wake up the next spinner.  */
//  atomic_store_release (&next->locked, 1);
//  atomic_full_barrier ();
//}
