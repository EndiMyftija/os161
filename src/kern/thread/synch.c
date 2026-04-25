/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(*sem));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(*lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

        // add stuff here as needed

	lock->lk_wchan = wchan_create(lock->lk_name);
        if (lock->lk_wchan == NULL) {
                //Clean up lock->lk_name as well
                kfree(lock->lk_name);
                kfree(lock);
                return NULL;
        }

        // initialize spinlock. (this cannot fail, so no null check)
        spinlock_init(&(lock->lk_lock));

        // no one owns the lock when it is first created
        lock->lk_owner = NULL;

        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed

        KASSERT(lock->lk_owner == NULL); // can't destroy a lock if it's still owned
	spinlock_cleanup(&lock->lk_lock);
        wchan_destroy(lock->lk_wchan);

        kfree(lock->lk_name);
        kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	/* Call this (atomically) before waiting for a lock */
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

        // Write this

        KASSERT(lock != NULL);

        // May not block in an interrupt handler.
        KASSERT(curthread->t_in_interrupt == false);

        spinlock_acquire(&lock->lk_lock);
        while(lock->lk_owner != NULL) {
                wchan_sleep(lock->lk_wchan, &lock->lk_lock);
        }
        KASSERT(lock->lk_owner == NULL);
        lock->lk_owner = curthread;
        spinlock_release(&lock->lk_lock);

	/* Call this (atomically) once the lock is acquired */
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
}

void
lock_release(struct lock *lock)
{
	/* Call this (atomically) when the lock is released */
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

        // Write this
        KASSERT(lock != NULL);
        KASSERT(lock->lk_owner == curthread); // Only the owner of the lock can release it

        spinlock_acquire(&lock->lk_lock);
        lock->lk_owner = NULL;
        wchan_wakeone(lock->lk_wchan, &lock->lk_lock);

        spinlock_release(&lock->lk_lock);
}

bool
lock_do_i_hold(struct lock *lock)
{
        // Write this
        KASSERT(lock != NULL);

        return (curthread == lock->lk_owner);
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(*cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

        // add stuff here as needed
        cv->cv_wchan = wchan_create(cv->cv_name);
        if (cv->cv_wchan == NULL) {
                kfree(cv->cv_name);
                kfree(cv);
                return NULL;
        }

        spinlock_init(&cv->cv_lock);

        return cv;
}

void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        // add stuff here as needed

        spinlock_cleanup(&cv->cv_lock);
        wchan_destroy(cv->cv_wchan);
        kfree(cv->cv_name);
        kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
        // Write this
        KASSERT(cv != NULL); KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));

        spinlock_acquire(&cv->cv_lock);
        lock_release(lock);
        wchan_sleep(cv->cv_wchan, &cv->cv_lock);

        //This code runs after we wake up from sleep
        spinlock_release(&cv->cv_lock);
        lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
        // Write this
        KASSERT(cv != NULL); KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));

        spinlock_acquire(&cv->cv_lock); //Only 1 thread can call signal at a time even if there are multiple threads trying to signal the same CV on different CPU cores
        wchan_wakeone(cv->cv_wchan, &cv->cv_lock);
        spinlock_release(&cv->cv_lock);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
        KASSERT(cv != NULL); KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));

        spinlock_acquire(&cv->cv_lock); //Only 1 thread can call signal at a time even if there are multiple threads trying to signal the same CV on different CPU cores
        wchan_wakeall(cv->cv_wchan, &cv->cv_lock);
        spinlock_release(&cv->cv_lock);
}

struct rwlock *rwlock_create(const char *name) {
        struct rwlock *rwlock;

        rwlock = kmalloc(sizeof(*rwlock));
        if (rwlock == NULL) {
                return NULL;
        }

        rwlock->rwlock_name = kstrdup(name);
        if (rwlock->rwlock_name == NULL) {
                kfree(rwlock);
                return NULL;
        }

        rwlock->rwlock_readers_wchan = wchan_create(rwlock->rwlock_name);
        if (rwlock->rwlock_readers_wchan == NULL) {
                kfree(rwlock->rwlock_name);
                kfree(rwlock);
                return NULL;
        }

        rwlock->rwlock_writers_wchan = wchan_create(rwlock->rwlock_name);
        if (rwlock->rwlock_writers_wchan == NULL) {
                kfree(rwlock->rwlock_name);
                wchan_destroy(rwlock->rwlock_readers_wchan);
                kfree(rwlock);
                return NULL;
        }

        spinlock_init(&rwlock->rwlock_lock);
        rwlock->rwlock_num_active_readers = 0;
        rwlock->rwlock_writers_waiting = 0;
        rwlock->rwlock_writer_owner = NULL;
        return rwlock;
}

void rwlock_destroy(struct rwlock *rwlock) {
        KASSERT(rwlock != NULL);

        spinlock_cleanup(&rwlock->rwlock_lock);
        wchan_destroy(rwlock->rwlock_readers_wchan);
        wchan_destroy(rwlock->rwlock_writers_wchan);
        kfree(rwlock->rwlock_name);
        kfree(rwlock);
}

void 
rwlock_acquire_read(struct rwlock *rwlock) 
{
        KASSERT(rwlock != NULL);

        // 1. Lock the door BEFORE looking at the variables
        spinlock_acquire(&rwlock->rwlock_lock);

        // 2. Re-evaluate the condition in a WHILE loop
        while (rwlock->rwlock_writer_owner != NULL || rwlock->rwlock_writers_waiting > 0) {
                // We cannot read right now. Go to sleep.
                // (This safely drops the spinlock while we sleep)
                wchan_sleep(rwlock->rwlock_readers_wchan, &rwlock->rwlock_lock);
        }

        // This code runs after wake up from sleep.
        rwlock->rwlock_num_active_readers++;

        // 4. Unlock the door
        spinlock_release(&rwlock->rwlock_lock);
}

void rwlock_release_read(struct rwlock *rwlock) {
        KASSERT(rwlock != NULL);

        spinlock_acquire(&rwlock->rwlock_lock);
        KASSERT(rwlock->rwlock_num_active_readers > 0); // Can't release if there are no active readers
        rwlock->rwlock_num_active_readers--;

        if (rwlock->rwlock_num_active_readers == 0 && rwlock->rwlock_writers_waiting > 0) {
                // If there are no more active readers and there are waiting writers, wake one writer
                wchan_wakeone(rwlock->rwlock_writers_wchan, &rwlock->rwlock_lock);
        }

        spinlock_release(&rwlock->rwlock_lock);
}

void rwlock_acquire_write(struct rwlock *rwlock) {
        KASSERT(rwlock != NULL);

        spinlock_acquire(&rwlock->rwlock_lock);
        rwlock->rwlock_writers_waiting++;
        while (rwlock->rwlock_writer_owner != NULL || rwlock->rwlock_num_active_readers > 0) {
                // If there is an active writer or active readers, we need to wait
                wchan_sleep(rwlock->rwlock_writers_wchan, &rwlock->rwlock_lock);
        }
        
        //This code runs after wake-up from sleep
        rwlock->rwlock_writer_owner = curthread;
        rwlock->rwlock_writers_waiting--;
        spinlock_release(&rwlock->rwlock_lock);
}

void rwlock_release_write(struct rwlock *rwlock) {
        KASSERT(rwlock->rwlock_writer_owner == curthread);

        spinlock_acquire(&rwlock->rwlock_lock);
        KASSERT(rwlock != NULL);
        rwlock->rwlock_writer_owner = NULL;
        if (rwlock->rwlock_writers_waiting > 0) {
                // If there are waiting writers, wake one writer
                wchan_wakeone(rwlock->rwlock_writers_wchan, &rwlock->rwlock_lock);
        } else {
                // Otherwise, wake all readers
                wchan_wakeall(rwlock->rwlock_readers_wchan, &rwlock->rwlock_lock);
        }
        spinlock_release(&rwlock->rwlock_lock);
}