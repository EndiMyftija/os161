#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <test.h>

static struct rwlock *testlock;
static struct semaphore *donesem;

// A shared variable to prove the writer actually worked
static volatile int shared_value = 0; 

static void
readerthread(void *p, unsigned long thread_num)
{
        (void)p;
        
        //If I am a late reader (thread_num 4 or 5), I want to yield first so the writer gets in line before me.
        if (thread_num == 4 || thread_num == 5) {
            thread_yield();
            thread_yield();
        }
        // 2. Acquire read lock
        // If the writer is waiting, late readers should block here.
        rwlock_acquire_read(testlock);

        // 3. Print that I entered, and what the shared_value is.
        kprintf("Reader %lu entered. Shared value: %d\n", thread_num, shared_value);
        
        // 4. Yield a few times to hold the lock open and create the traffic jam!
        thread_yield();
        thread_yield();
        
        // 5. Release read lock
        rwlock_release_read(testlock);
        
        // 6. Tell the main thread I am done
        V(donesem);
}

static void
writerthread(void *p, unsigned long thread_num)
{
        (void)p;
        
        // 1. Yield once so the early readers get inside the lock before I arrive.
        thread_yield();
            
        // Try to acquire the lock.
        // 2. If other readers are already inside, this will block.
        rwlock_acquire_write(testlock);

        // 3. I am inside the vault! Only ONE thread can be here.
        shared_value++; // Increment the value!
        kprintf("Writer %lu entered. Changed Shared Value to: %d\n", thread_num, shared_value);
        
        // 4. Yield just to be annoying.
        thread_yield();
        
        // 5. Release write lock
        rwlock_release_write(testlock);
        
        // 6. Tell the main thread I am done
        V(donesem);
}

int
rwtest(int nargs, char **args)
{
        (void)nargs;
        (void)args;

        kprintf("Starting rwt1: The Starvation Crucible...\n");

        testlock = rwlock_create("testlock");
        donesem = sem_create("donesem", 0);
        shared_value = 0;

        // Spawn 2 Early Readers
        thread_fork("R1", NULL, readerthread, NULL, 1);
        thread_fork("R2", NULL, readerthread, NULL, 2);
        
        // Spawn 1 Writer
        thread_fork("W1", NULL, writerthread, NULL, 3);
        
        // Spawn 2 Late Readers
        thread_fork("R3", NULL, readerthread, NULL, 4);
        thread_fork("R4", NULL, readerthread, NULL, 5);

        // Wait for all 5 threads to finish
        for (int i=0; i<5; i++) {
                P(donesem);
        }

        rwlock_destroy(testlock);
        sem_destroy(donesem);

        kprintf("rwt1 complete.\n");
        return 0;
}