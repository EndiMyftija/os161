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

#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <copyinout.h>
#include <limits.h>
#include <proc.h>
#include <vnode.h>
#include <uio.h>
#include <kern/iovec.h>
#include <vnode.h>
#include <synch.h>
#include <file.h>
#include <kern/fcntl.h>
#include <vfs.h>

/*
 * System call dispatcher.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception-*.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. 64-bit arguments are passed in *aligned*
 * pairs of registers, that is, either a0/a1 or a2/a3. This means that
 * if the first argument is 32-bit and the second is 64-bit, a1 is
 * unused.
 *
 * This much is the same as the calling conventions for ordinary
 * function calls. In addition, the system call number is passed in
 * the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, or v0 and v1 if 64-bit. This is also like an ordinary
 * function call, and additionally the a3 register is also set to 0 to
 * indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/user/lib/libc/arch/mips/syscalls-mips.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * If you run out of registers (which happens quickly with 64-bit
 * values) further arguments must be fetched from the user-level
 * stack, starting at sp+16 to skip over the slots for the
 * registerized values, with copyin().
 */
void
syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err = 0;

	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values,
	 * like write.
	 */

	retval = 0;

	switch (callno) {
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

	    case SYS___time:
		err = sys___time((userptr_t)tf->tf_a0,
				 (userptr_t)tf->tf_a1);
		break;

	    /* Add stuff here */

		case SYS_write:
		{
			//1. Extraction and Validation phase
			int fd = tf->tf_a0;
			if (fd < 0 || fd >= __OPEN_MAX) {
				err = EBADF;
				break;
			}

			// 2. FDT Extraction & Null Check
			spinlock_acquire(&curproc->p_lock);
			struct file_handle *fh = curproc->p_fdt[fd];
			if (fh == NULL) {
				spinlock_release(&curproc->p_lock);
				err = EBADF;
				break;
			}
			spinlock_release(&curproc->p_lock);

			// 3. File Handle Lock & Permission Check
			lock_acquire(fh->fh_lock);

			// O_ACCMODE is a bitmask (usually value 3) used to extract the read/write bits.
			int how = fh->access_flag & O_ACCMODE;
			if (how == O_RDONLY) {
				lock_release(fh->fh_lock); // Release before breaking!
				err = EBADF; 
				break;
			}

			// 4. uio/iovec Setup
			struct iovec iov;
			struct uio u;
			size_t length = tf->tf_a2;

			//Set up the shipping container (iovec)
			iov.iov_ubase = (userptr_t)tf->tf_a1; // The hostile user buffer
			iov.iov_len = length;

			// Set up the truck (uio)
			u.uio_iov = &iov;
			u.uio_iovcnt = 1;
			u.uio_offset = fh->offset; // Start writing at the file's current offset
			u.uio_resid = length; // Total bytes to write
			u.uio_segflg = UIO_USERSPACE; // Buffer is in user space
			u.uio_rw = UIO_WRITE; // This is a write operation
			u.uio_space = proc_getas(); // Address space of the current process

			// 5. Perform the Write
			// Pass the VFS the vnode and uio instructions
			// It will safely copyin the data and write to the disk


			err = VOP_WRITE(fh->vn, &u);
			if (err) {
				// Disk full, I/O error or bad user pointer
				lock_release(fh->fh_lock);
				break;
			}
		
			// 6. Clean-up
			// VOP_WRITE() automatically updates uio_offset and uio_resid by the amount of bytes it successfully wrote.
			fh->offset = u.uio_offset;
			lock_release(fh->fh_lock);

			// uio_resid should be 0 if everything was written, but if it's not, we return the amount that was successfully write
			// So (Total length - residual) = Bytes successfully written
			retval = length - u.uio_resid;
			break;
		}

		case SYS_read:
		{
			//1. Extraction and Validation phase
			int fd = tf->tf_a0;
			if (fd < 0 || fd >= __OPEN_MAX) {
				err = EBADF;
				break;
			}

			// 2. FDT Extraction & Null Check
			spinlock_acquire(&curproc->p_lock);
			struct file_handle *fh = curproc->p_fdt[fd];
			if (fh == NULL) {
				spinlock_release(&curproc->p_lock);
				err = EBADF;
				break;
			}
			spinlock_release(&curproc->p_lock);

			// 3. File Handle Lock & Permission Check
			lock_acquire(fh->fh_lock);

			// O_ACCMODE is a bitmask (usually value 3) used to extract the read/write bits.
			int how = fh->access_flag & O_ACCMODE;
			if (how == O_WRONLY || how == O_RDWR) {
				lock_release(fh->fh_lock); // Release before breaking!
				err = EBADF; 
				break;
			}

			// 4. uio/iovec Setup
			struct iovec iov;
			struct uio u;
			size_t length = tf->tf_a2;

			//Set up the shipping container (iovec)
			iov.iov_ubase = (userptr_t)tf->tf_a1; // The hostile user buffer
			iov.iov_len = length;

			// Set up the truck (uio)
			u.uio_iov = &iov;
			u.uio_iovcnt = 1;
			u.uio_offset = fh->offset; // Start writing at the file's current offset
			u.uio_resid = length; // Total bytes to write
			u.uio_segflg = UIO_USERSPACE; // Buffer is in user space
			u.uio_rw = UIO_READ; // This is a read operation
			u.uio_space = proc_getas(); // Address space of the current process

			// 5. Perform the Read
			// Pass the VFS the vnode and uio instructions
			// It will safely copyin the data and write to the disk


			err = VOP_READ(fh->vn, &u);
			if (err) {
				// Disk full, I/O error or bad user pointer
				lock_release(fh->fh_lock);
				break;
			}
		
			// 6. Clean-up
			// VOP_READ() automatically updates uio_offset and uio_resid by the amount of bytes it successfully read.
			fh->offset = u.uio_offset;
			lock_release(fh->fh_lock);

			// uio_resid should be 0 if everything was written, but if it's not, we return the amount that was successfully write
			// So (Total length - residual) = Bytes successfully written
			retval = length - u.uio_resid;
			break;
		}

		case SYS_close:
		{
			//1. Array bounds check
			int fd = tf->tf_a0;
			if (fd < 0 || fd >= __OPEN_MAX) {
				err = EBADF;
				break;
			}

			spinlock_acquire(&curproc->p_lock);
			struct file_handle *fh =curproc->p_fdt[fd];
			if (fh == NULL) {
				spinlock_release(&curproc->p_lock);
				err = EBADF;
				break;
			}
			curproc->p_fdt[fd] = NULL; // Remove the file handle from the FDT
			spinlock_release(&curproc->p_lock);

			file_handle_decref(fh);

			err = 0; // Success
			break;
		}

		case SYS_open:
		{
			const_userptr_t user_path = (const_userptr_t) tf->tf_a0;
			int access_flag = tf->tf_a1; 

			// 1. Allocate Kernel Buffer
			char* kern_buffer = kmalloc(__PATH_MAX);
			if (kern_buffer == NULL) {
				err = ENOMEM;
				break;
			}

			// 2. Safe Copy
			size_t path_length;
			err = copyinstr(user_path, kern_buffer, __PATH_MAX, &path_length);
			if (err) {
				kfree(kern_buffer);
				break;
			}

			// 3. Open the File (Let the VFS do the heavy lifting)
			struct vnode *vn;
			err = vfs_open(kern_buffer, access_flag, 0, &vn);
			
			// The string is no longer needed. Free it NOW.
			kfree(kern_buffer);

			if (err) {
				break; 
			}

			// 4. Create the Handle
			struct file_handle* fh = file_handle_create(vn, access_flag);
			if (fh == NULL) {
				vfs_close(vn); // Don't leak the vnode if kmalloc fails!
				err = ENOMEM;
				break;
			}

			// 5. The FDT Search
			spinlock_acquire(&curproc->p_lock);
			int fd = -1;
			for (int i = 3; i < __OPEN_MAX; i++) {
				if (curproc->p_fdt[i] == NULL) {
					curproc->p_fdt[i] = fh;
					fd = i;
					break;
				}
			}
			spinlock_release(&curproc->p_lock);

			// 6. Handle the Full Table
			if (fd == -1) {
				// We failed to find a slot. Destroy the handle (this also closes the vnode).
				file_handle_decref(fh);
				err = EMFILE; 
				break;
			}

			// 7. Success
			retval = fd;
			err = 0;
			break;
		}

		case SYS__exit:
		{
			// Hack: Just violently destroy the thread so it doesn't return to user space.
			// We will replace this with proper sys__exit logic later.
			thread_exit();
			break;
		}

	    default:
		kprintf("unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * return the error code. this gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}

	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */

	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);
}

/*
 * Enter user mode for a newly forked process.
 *
 * This function is provided as a reminder. You need to write
 * both it and the code that calls it.
 *
 * Thus, you can trash it and do things another way if you prefer.
 */
void
enter_forked_process(struct trapframe *tf)
{
	(void)tf;
}
