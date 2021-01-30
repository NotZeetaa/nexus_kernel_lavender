// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Google, Inc.
 * Robert Love <rlove@google.com>
 * Copyright (C) 2021 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#define pr_fmt(fmt) "ashmem: " fmt

#include <linux/miscdevice.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>
#include "ashmem.h"

/**
 * struct ashmem_area - The anonymous shared memory area
 * @mmap_lock:		The mmap mutex lock
 * @file:		The shmem-based backing file
 * @size:		The size of the mapping, in bytes
 * @prot_mask:		The allowed protection bits, as vm_flags
 *
 * The lifecycle of this structure is from our parent file's open() until
 * its release().
 *
 * Warning: Mappings do NOT pin this structure; It dies on close()
 */
struct ashmem_area {
	struct mutex mmap_lock;
	struct file *file;
	size_t size;
	unsigned long prot_mask;
};

static struct kmem_cache *ashmem_area_cachep __read_mostly;
static struct kmem_cache *ashmem_range_cachep __read_mostly;

#define range_size(range) \
	((range)->pgend - (range)->pgstart + 1)

#define range_on_lru(range) \
	((range)->purged == ASHMEM_NOT_PURGED)

#define page_range_subsumes_range(range, start, end) \
	(((range)->pgstart >= (start)) && ((range)->pgend <= (end)))

#define page_range_subsumed_by_range(range, start, end) \
	(((range)->pgstart <= (start)) && ((range)->pgend >= (end)))

#define page_in_range(range, page) \
	(((range)->pgstart <= (page)) && ((range)->pgend >= (page)))

#define page_range_in_range(range, start, end) \
	(page_in_range(range, start) || page_in_range(range, end) || \
		page_range_subsumes_range(range, start, end))

#define range_before_page(range, page) \
	((range)->pgend < (page))

#define PROT_MASK		(PROT_EXEC | PROT_READ | PROT_WRITE)

/**
 * ashmem_open() - Opens an Anonymous Shared Memory structure
 * @inode:	   The backing file's index node(?)
 * @file:	   The backing file
 *
 * Please note that the ashmem_area is not returned by this function - It is
 * instead written to "file->private_data".
 *
 * Return: 0 if successful, or another code if unsuccessful.
 */
static int ashmem_open(struct inode *inode, struct file *file)
{
	struct ashmem_area *asma;
	int ret;

	ret = generic_file_open(inode, file);
	if (unlikely(ret))
		return ret;

	asma = kmem_cache_alloc(ashmem_area_cachep, GFP_KERNEL);
	if (unlikely(!asma))
		return -ENOMEM;

	*asma = (typeof(*asma)){
		.mmap_lock = __MUTEX_INITIALIZER(asma->mmap_lock),
		.prot_mask = PROT_MASK
	};

	file->private_data = asma;

	return 0;
}

/**
 * ashmem_release() - Releases an Anonymous Shared Memory structure
 * @ignored:	      The backing file's Index Node(?) - It is ignored here.
 * @file:	      The backing file
 *
 * Return: 0 if successful. If it is anything else, go have a coffee and
 * try again.
 */
static int ashmem_release(struct inode *ignored, struct file *file)
{
	struct ashmem_area *asma = file->private_data;

	if (asma->file)
		fput(asma->file);
	kmem_cache_free(ashmem_area_cachep, asma);

	return 0;
}

/**
 * ashmem_read() - Reads a set of bytes from an Ashmem-enabled file
 * @file:	   The associated backing file.
 * @buf:	   The buffer of data being written to
 * @len:	   The number of bytes being read
 * @pos:	   The position of the first byte to read.
 *
 * Return: 0 if successful, or another return code if not.
 */
static ssize_t ashmem_read(struct file *file, char __user *buf,
			   size_t len, loff_t *pos)
{
	struct ashmem_area *asma = file->private_data;
	int ret = 0;

	mutex_lock(&ashmem_mutex);
	struct ashmem_area *asma = iocb->ki_filp->private_data;
	struct file *vmfile;
	ssize_t ret;

	/* If size is not set, or set to 0, always return EOF. */
	if (!READ_ONCE(asma->size))
		return 0;

	vmfile = READ_ONCE(asma->file);
	if (!vmfile)
		return -EBADF;

	mutex_unlock(&ashmem_mutex);

	/*
	 * asma and asma->file are used outside the lock here.  We assume
	 * once asma->file is set it will never be changed, and will not
	 * be destroyed until all references to the file are dropped and
	 * ashmem_release is called.
	 */
	ret = __vfs_read(asma->file, buf, len, pos);
	if (ret >= 0)
		/** Update backing file pos, since f_ops->read() doesn't */
		asma->file->f_pos = *pos;
	return ret;

out_unlock:
	mutex_unlock(&ashmem_mutex);
	ret = vfs_iter_read(vmfile, iter, &iocb->ki_pos, 0);
	if (ret > 0)
		vmfile->f_pos = iocb->ki_pos;
	return ret;
}

static loff_t ashmem_llseek(struct file *file, loff_t offset, int origin)
{
	struct ashmem_area *asma = file->private_data;
	struct file *vmfile;
	loff_t ret;

	if (!READ_ONCE(asma->size))
		return -EINVAL;

	vmfile = READ_ONCE(asma->file);
	if (!vmfile)
		return -EBADF;

	ret = vfs_llseek(vmfile, offset, origin);
	if (ret < 0)
		return ret;

	/** Copy f_pos from backing file, since f_ops->llseek() sets it */
	file->f_pos = vmfile->f_pos;
	return ret;
}

static inline vm_flags_t calc_vm_may_flags(unsigned long prot)
{
	return _calc_vm_trans(prot, PROT_READ,  VM_MAYREAD) |
	       _calc_vm_trans(prot, PROT_WRITE, VM_MAYWRITE) |
	       _calc_vm_trans(prot, PROT_EXEC,  VM_MAYEXEC);
}

static int ashmem_vmfile_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* do not allow to mmap ashmem backing shmem file directly */
	return -EPERM;
}

static unsigned long
ashmem_vmfile_get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

static int ashmem_file_setup(struct ashmem_area *asma, size_t size,
			     struct vm_area_struct *vma)
{
	static struct file_operations vmfile_fops;
	static DEFINE_SPINLOCK(vmfile_fops_lock);
	struct file *vmfile;

	/* user needs to SET_SIZE before mapping */
	if (unlikely(!asma->size)) {
		ret = -EINVAL;
		goto out;
	}

	/* requested mapping size larger than object size */
	if (vma->vm_end - vma->vm_start > PAGE_ALIGN(asma->size)) {
		ret = -EINVAL;
		goto out;
	}

	/* requested protection bits must match our allowed protection mask */
	if (unlikely((vma->vm_flags & ~calc_vm_prot_bits(asma->prot_mask)) &
		     calc_vm_prot_bits(PROT_MASK))) {
		ret = -EPERM;
		goto out;
	}
	vma->vm_flags &= ~calc_vm_may_flags(~asma->prot_mask);

	if (!asma->file) {
		char *name = ASHMEM_NAME_DEF;
		struct file *vmfile;
	vmfile = shmem_file_setup(ASHMEM_NAME_DEF, size, vma->vm_flags);
	if (IS_ERR(vmfile))
		return PTR_ERR(vmfile);

	/*
	 * override mmap operation of the vmfile so that it can't be
	 * remapped which would lead to creation of a new vma with no
	 * asma permission checks. Have to override get_unmapped_area
	 * as well to prevent VM_BUG_ON check for f_ops modification.
	 */
	if (!READ_ONCE(vmfile_fops.mmap)) {
		spin_lock(&vmfile_fops_lock);
		if (!vmfile_fops.mmap) {
			vmfile_fops = *vmfile->f_op;
			vmfile_fops.get_unmapped_area =
				ashmem_vmfile_get_unmapped_area;
			WRITE_ONCE(vmfile_fops.mmap, ashmem_vmfile_mmap);
		}
		spin_unlock(&vmfile_fops_lock);
	}
	vmfile->f_op = &vmfile_fops;
	vmfile->f_mode |= FMODE_LSEEK;

	WRITE_ONCE(asma->file, vmfile);
	return 0;
}

static int ashmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ashmem_area *asma = file->private_data;
	unsigned long prot_mask;
	size_t size;

	/* user needs to SET_SIZE before mapping */
	size = READ_ONCE(asma->size);
	if (unlikely(!size))
		return -EINVAL;

	/* requested mapping size larger than object size */
	if (vma->vm_end - vma->vm_start > PAGE_ALIGN(size))
		return -EINVAL;

	/* requested protection bits must match our allowed protection mask */
	prot_mask = READ_ONCE(asma->prot_mask);
	if (unlikely((vma->vm_flags & ~calc_vm_prot_bits(prot_mask, 0)) &
		     calc_vm_prot_bits(PROT_MASK, 0)))
		return -EPERM;

	vma->vm_flags &= ~calc_vm_may_flags(~prot_mask);

	if (!READ_ONCE(asma->file)) {
		int ret = 0;

		mutex_lock(&asma->mmap_lock);
		if (!asma->file)
			ret = ashmem_file_setup(asma, size, vma);
		mutex_unlock(&asma->mmap_lock);

		if (ret)
			return ret;
	}

	get_file(asma->file);

	if (vma->vm_flags & VM_SHARED) {
		shmem_set_file(vma, asma->file);
	} else {
		len = sizeof(ASHMEM_NAME_DEF);
		memcpy(local_name, ASHMEM_NAME_DEF, len);
	}
	mutex_unlock(&ashmem_mutex);

	/*
	 * Now we are just copying from the stack variable to userland
	 * No lock held
	 */
	if (unlikely(copy_to_user(name, local_name, len)))
		ret = -EFAULT;
	return ret;
}

/*
 * ashmem_pin - pin the given ashmem region, returning whether it was
 * previously purged (ASHMEM_WAS_PURGED) or not (ASHMEM_NOT_PURGED).
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_pin(struct ashmem_area *asma, size_t pgstart, size_t pgend)
{
	struct ashmem_range *range, *next;
	int ret = ASHMEM_NOT_PURGED;

	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* moved past last applicable page; we can short circuit */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to pin pages that span multiple ranges,
		 * or to pin pages that aren't even unpinned, so this is messy.
		 *
		 * Four cases:
		 * 1. The requested range subsumes an existing range, so we
		 *    just remove the entire matching range.
		 * 2. The requested range overlaps the start of an existing
		 *    range, so we just update that range.
		 * 3. The requested range overlaps the end of an existing
		 *    range, so we just update that range.
		 * 4. The requested range punches a hole in an existing range,
		 *    so we have to update one side of the range and then
		 *    create a new range for the other side.
		 */
		if (page_range_in_range(range, pgstart, pgend)) {
			ret |= range->purged;

			/* Case #1: Easy. Just nuke the whole thing. */
			if (page_range_subsumes_range(range, pgstart, pgend)) {
				range_del(range);
				continue;
			}

			/* Case #2: We overlap from the start, so adjust it */
			if (range->pgstart >= pgstart) {
				range_shrink(range, pgend + 1, range->pgend);
				continue;
			}

			/* Case #3: We overlap from the rear, so adjust it */
			if (range->pgend <= pgend) {
				range_shrink(range, range->pgstart,
					     pgstart - 1);
				continue;
			}

			/*
			 * Case #4: We eat a chunk out of the middle. A bit
			 * more complicated, we allocate a new range for the
			 * second half and adjust the first chunk's endpoint.
			 */
			range_alloc(asma, range, range->purged,
				    pgend + 1, range->pgend);
			range_shrink(range, range->pgstart, pgstart - 1);
			break;
		}
	}

	return ret;
}

/*
 * ashmem_unpin - unpin the given range of pages. Returns zero on success.
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_unpin(struct ashmem_area *asma, size_t pgstart, size_t pgend)
{
	struct ashmem_range *range, *next;
	unsigned int purged = ASHMEM_NOT_PURGED;

restart:
	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* short circuit: this is our insertion point */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to unpin pages that are already entirely
		 * or partially pinned. We handle those two cases here.
		 */
		if (page_range_subsumed_by_range(range, pgstart, pgend))
			return 0;
		if (page_range_in_range(range, pgstart, pgend)) {
			pgstart = min_t(size_t, range->pgstart, pgstart);
			pgend = max_t(size_t, range->pgend, pgend);
			purged |= range->purged;
			range_del(range);
			goto restart;
		}
	}

	return range_alloc(asma, range, purged, pgstart, pgend);
}

/*
 * ashmem_get_pin_status - Returns ASHMEM_IS_UNPINNED if _any_ pages in the
 * given interval are unpinned and ASHMEM_IS_PINNED otherwise.
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_get_pin_status(struct ashmem_area *asma, size_t pgstart,
				 size_t pgend)
{
	struct ashmem_range *range;
	int ret = ASHMEM_IS_PINNED;

	list_for_each_entry(range, &asma->unpinned_list, unpinned) {
		if (range_before_page(range, pgstart))
			break;
		if (page_range_in_range(range, pgstart, pgend)) {
			ret = ASHMEM_IS_UNPINNED;
			break;
		}
		if (vma->vm_file)
			fput(vma->vm_file);
		vma->vm_file = asma->file;
	}

	return 0;
}

static int set_prot_mask(struct ashmem_area *asma, unsigned long prot)
{
	/* the user can only remove, not add, protection bits */
	if (unlikely((READ_ONCE(asma->prot_mask) & prot) != prot))
		return -EINVAL;

	/* does the application expect PROT_READ to imply PROT_EXEC? */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		prot |= PROT_EXEC;

	WRITE_ONCE(asma->prot_mask, prot);
	return 0;
}

static long ashmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ashmem_area *asma = file->private_data;

	switch (cmd) {
	case ASHMEM_SET_NAME:
		return 0;
	case ASHMEM_GET_NAME:
		return 0;
	case ASHMEM_SET_SIZE:
		if (READ_ONCE(asma->file))
			return -EINVAL;

		WRITE_ONCE(asma->size, (size_t)arg);
		return 0;
	case ASHMEM_GET_SIZE:
		return READ_ONCE(asma->size);
	case ASHMEM_SET_PROT_MASK:
		return set_prot_mask(asma, arg);
	case ASHMEM_GET_PROT_MASK:
		return READ_ONCE(asma->prot_mask);
	case ASHMEM_PIN:
		return 0;
	case ASHMEM_UNPIN:
		return 0;
	case ASHMEM_GET_PIN_STATUS:
		return ASHMEM_IS_PINNED;
	case ASHMEM_PURGE_ALL_CACHES:
		return capable(CAP_SYS_ADMIN) ? 0 : -EPERM;
	}

	return -ENOTTY;
}

/* support of 32bit userspace on 64bit platforms */
#ifdef CONFIG_COMPAT
static long compat_ashmem_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	switch (cmd) {
	case COMPAT_ASHMEM_SET_SIZE:
		cmd = ASHMEM_SET_SIZE;
		break;
	case COMPAT_ASHMEM_SET_PROT_MASK:
		cmd = ASHMEM_SET_PROT_MASK;
		break;
	}
	return ashmem_ioctl(file, cmd, arg);
}
#endif

static const struct file_operations ashmem_fops = {
	.owner = THIS_MODULE,
	.open = ashmem_open,
	.release = ashmem_release,
	.read = ashmem_read,
	.llseek = ashmem_llseek,
	.mmap = ashmem_mmap,
	.unlocked_ioctl = ashmem_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ashmem_ioctl,
#endif
};

static struct miscdevice ashmem_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ashmem",
	.fops = &ashmem_fops,
};

static int __init ashmem_init(void)
{
	int ret;

	ashmem_area_cachep = kmem_cache_create("ashmem_area_cache",
					       sizeof(struct ashmem_area),
					       0, 0, NULL);
	if (unlikely(!ashmem_area_cachep)) {
		pr_err("failed to create slab cache\n");
		return -ENOMEM;
	}

	ashmem_range_cachep = kmem_cache_create("ashmem_range_cache",
						sizeof(struct ashmem_range),
						0, 0, NULL);
	if (unlikely(!ashmem_range_cachep)) {
		pr_err("failed to create slab cache\n");
		return -ENOMEM;
	}

	ret = misc_register(&ashmem_misc);
	if (unlikely(ret)) {
		pr_err("failed to register misc device!\n");
		return ret;
		ret = -ENOMEM;
		goto out;
	}

	ret = misc_register(&ashmem_misc);
	if (unlikely(ret)) {
		pr_err("failed to register misc device!\n");
		goto out_free1;
	}

	pr_info("initialized\n");

	return 0;

out_free1:
	kmem_cache_destroy(ashmem_area_cachep);
out:
	return ret;
}
device_initcall(ashmem_init);
