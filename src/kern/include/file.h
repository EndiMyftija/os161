#ifndef _FILE_H_
#define _FILE_H_

#include <types.h>
#include <synch.h>
#include <vnode.h>

struct file_handle {
    struct vnode *vn;      // Pointer to the underlying VFS file
    off_t offset;          // Current read/write position
    int access_flag;      // O_RDONLY, O_WRONLY, O_RDWR (from <kern/fcntl.h>)
    struct lock *fh_lock;  // Synchronization lock for this specific file handle
    int refcount;          // Number of file descriptors pointing to this object
};

// Function prototypes for file.c go here...
struct *file_handle file_handle_create(struct *vnode, int access_flag);
void file_handle_decref(struct file_handle *file_handle_ptr);
void file_handle_incref(struct file_handle *file_handle_ptr);

#endif /* _FILE_H_ */