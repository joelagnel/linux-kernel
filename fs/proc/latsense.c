// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) ChromeOS Authors.
 *
 *  Added as an out-of-tree way of setting tasks as latency sensitive
 *  using /proc/pid/tasks/tid/latency_sensitive interface.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/sched/latsense.h>
#include <linux/sched/mm.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/task.h>
#include <linux/sched/cputime.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/times.h>
#include <linux/cpuset.h>
#include <linux/rcupdate.h>
#include <linux/delayacct.h>
#include <linux/seq_file.h>
#include <linux/pid_namespace.h>
#include <linux/prctl.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/string_helpers.h>
#include <linux/user_namespace.h>
#include <linux/fs_struct.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include "internal.h"

/*
 * Print out latsense related information:
 */
static int sched_latsense_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p;

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;

	seq_printf(m, "%d\n", !!proc_sched_get_latency_sensitive(p));

	put_task_struct(p);

	return 0;
}

static ssize_t
sched_latsense_write(struct file *file, const char __user *buf,
	    size_t count, loff_t *offset)
{
	struct inode *inode = file_inode(file);
	struct task_struct *p;
	char buffer[PROC_NUMBUF];
	int val;
	int err;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtoint(strstrip(buffer), 0, &val);
	if (err < 0)
		return err;

	if (val != 0 && val != 1)
		return -EINVAL;

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;

	err = proc_sched_set_latency_sensitive(p, val);
	if (err)
		count = err;

	put_task_struct(p);

	return count;
}

static int sched_latsense_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = single_open(filp, sched_latsense_show, NULL);
	if (!ret) {
		struct seq_file *m = filp->private_data;

		m->private = inode;
	}
	return ret;
}

const struct file_operations proc_tid_latsense_operations = {
	.open		= sched_latsense_open,
	.read		= seq_read,
	.write		= sched_latsense_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
