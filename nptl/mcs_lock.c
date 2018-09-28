/* Copyright (C) 2018 Free Software Foundation, Inc.
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
	NULL,
	0
};

static mcs_lock_t * mcs_wait_next (mcs_lock_t **lock, mcs_lock_t *node,
                                   mcs_lock_t *prev)
{
  mcs_lock_t *next = NULL;
  for (;;)
  {
  /*
    If prev is NULL, this function is called from mcs_unlock(), cmpxchg
	success indicates the last spinner is calling mcs_unlock(), then, set
	the tail pointer of queue to NULL;
	If prev is not NULL, this function is called from unqueue in
	mcs_lock(), cmpxchg success indicates the last spinner want to unqueue
	due to timeout, then, move the tail pointer of queue back to point its
	predecessor.
  */
    if (*lock == node && atomic_compare_and_exchange_val_acq (lock, prev,
	    node) == node)
	  break;

  /*
    Current node's unqueue's step 2 races against its successor's unqueue's
	step 1.
	From current node's perspective, if node->next is NULL, it indicates 
	its successor's undo prev->next at unqueue's step 1 competes
	successfully. Thus, current node loops until its successor unqueue
	successfully and reset its prev pointer. Or, if node->next is non-NULL,
	acquire the node->next by setting it to NULL to block the undo
	prev->next of its successor in unqueue's step 1.
  */
    if (node->next)
	  {
	    next = atomic_exchange_acquire (&node->next, NULL);
	    if (next)
		  break;
	  }

	atomic_spin_nop ();
  }

  return next;
}

bool mcs_lock (mcs_lock_t **lock)
{
  mcs_lock_t *prev, *curr, *next;
  int cnt = 0;

  curr = &node;
  /* Initialize node.  */
  curr->next = NULL;
  curr->locked = 0;

  prev = atomic_exchange_acquire (lock, curr);

  /* First spinner, spin on the lock immediately.  */
  if (prev == NULL)
      return true;

  curr->prev = prev;
  atomic_full_barrier ();  // write barrier?
  /* Add current spinner into the queue.  */
  atomic_store_release (&prev->next, curr);

  /* Waiting unless waken up by the previous spinner or unqeueue if timeout.  */
  while (!atomic_load_relaxed (&curr->locked))
    {
	  atomic_spin_nop ();
	  /* If timeout, unqueue.  */
	  if (++cnt >= MAX_ADAPTIVE_COUNT)   //  adaptive spin?
	    goto unqueue;
    }
  return true;

unqueue:
  /* Step 1: undo prev->next assignment  */
  for (;;)
    {
	  /* 
	    This cmpxchg races against its predecessor's mcs_unlock() or its
		predecessor's concurrent unqueue in step 2.

		For its predecessor's unlock, if cmpxchg wins, it will block
		mcs_unlock() until unqueue accomplishment, if cmpxchg fails, it
		will see curr->locked set to 1 by its predecessor sooner.

		For its predecessor's concurent unqueue in step 2, if cmpxchg wins,
		the unqueue of its predecessor will be blocked in until current
		unqueue accomplishment, if cmpxchg fails, current unqueue will be
		blocked until the unqueue accomplishment of its predecessor. 
	  */
	  if (prev->next == curr && 
          atomic_compare_and_exchange_val_acq (&prev->next, NULL, curr) == curr)
	   break;

      /* 
	     cmpxchg fails, if race against its predecessor's mcs_unlock(),
		 will see curr->locked is set to 1 soon.
	  */
	     
      if (atomic_load_relaxed (&curr->locked))
         return true;

	  atomic_spin_nop ();

	  /*
	    cmpxchg fails, if race against its predecessor's unqueue, its
		curr->prev will be updated by its predecessor at unqueue's step 3.
	  */
	  prev = atomic_load_relaxed (&curr->prev);
	}

  /* Step: 2: stabilize @next.  */

  next = mcs_wait_next (lock, curr, prev);
  if (!next)
    return false;


  /* Step 3: unlink.  */

  atomic_store_release (&next->prev, prev);
  atomic_store_release (&prev->next, next);

  return false;
}

void mcs_unlock (mcs_lock_t **lock)
{
  mcs_lock_t *next, *curr;

  curr = &node;
  /* Fast path for uncontended case. */
  if (atomic_compare_and_exchange_val_acq (lock, NULL, curr) == curr)
    return;

  /*
     Racing against a concurrent undoing prev->next at unqueue()'s Step 1,
	 if undoing prev->next wins, unlock() waits until the complishment of 
	 unqueue; if xchg() wins, undoing prev->next at unqueue()'s step 1
	 loops until next->locked is set to 1.
  */
	
  next = atomic_exchange_acquire (&curr->next, NULL);
  if (next)
    {
      atomic_store_release (&next->locked, 1);
	  return;
	}

  next = mcs_wait_next (lock, curr, NULL);
  if (next)
    atomic_store_release (&next->locked, 1);
} 
