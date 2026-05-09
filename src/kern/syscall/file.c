#include <file.h>
#include <kern/errno.h>
#include <lib.h>
#include <vfs.h>

struct file_handle *file_handle_create(struct vnode *vn, int access_flag) {
    struct file_handle *fh = kmalloc(sizeof(struct file_handle));
    if (fh == NULL) {
        return NULL;
    }

    fh->fh_lock = lock_create("file_lock");
    if (fh->fh_lock == NULL) {
        kfree(fh); // DO NOT LEAK MEMORY
        return NULL;
    }

    fh->offset = 0;
    fh->refcount = 1; // It exists, so it has 1 reference
    fh->vn = vn;
    fh->access_flag = access_flag; // Fixed missing semicolon

    return fh;
}

void file_handle_decref(struct file_handle *fh) {
    KASSERT(fh != NULL);

    lock_acquire(fh->fh_lock);
    fh->refcount--;
    int count_after_decref = fh->refcount; // Safely read while locked
    lock_release(fh->fh_lock);

    if (count_after_decref == 0) {
        // We are the last ones holding this file. Burn it down.
        lock_destroy(fh->fh_lock);
        vfs_close(fh->vn);
        kfree(fh);
    }
}

void file_handle_incref(struct file_handle *fh) {
    KASSERT(fh != NULL);
    lock_acquire(fh->fh_lock);
    fh->refcount++;
    lock_release(fh->fh_lock);
}