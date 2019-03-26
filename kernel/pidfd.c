// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/kernel/pidfd.c
 *
 *  pidfd related operations
 *
 *  2019-03-23: Added pidfd_wait system call
 *              (Joel Fernandes and Daniel Colascione - Google)
 */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/syscalls.h>
#include <linux/uio.h>

struct pidfd_wait_priv {
	struct pid *pid;
	struct task_struct *task;
};

#define pidfd_wait_lock(task)   spin_lock(&task->signal->wait_pidfd.lock)
#define pidfd_wait_unlock(task) spin_unlock(&task->signal->wait_pidfd.lock)

/*
 * Set the task's exit_state to state only if it is in a ZOMBIE state.
 * Return true if the exit_state could be set, otherwise false.
 *
 * Also note that do_notify_parent() is always called before
 * this is called.
 */
bool task_set_exit_state_if_zombie(struct task_struct *task, int state)
{
	pidfd_wait_lock(task);
	if (cmpxchg(&task->exit_state, EXIT_ZOMBIE, state) != EXIT_ZOMBIE) {
		pidfd_wait_unlock(task);
		return false;
	}

	wake_up_all_locked(&task->signal->wait_pidfd);
	pidfd_wait_unlock(task);
	return true;
}

/* 
 * Set the task's exit state
 * Also note that do_notify_parent() is always called before
 * this is called.
 */
void task_set_exit_state(struct task_struct *task, int state)
{
	pidfd_wait_lock(task);
	task->exit_state = state;
	wake_up_all_locked(&task->signal->wait_pidfd);
	pidfd_wait_unlock(task);
	return;
}

/*
 * Will block if task state hasn't been set to exited/reaped
 * TODO: What about EXIT_TRACE ?
 */
static int pidfd_read_exit_status(struct task_struct *task)
{
	int ret;

	pidfd_wait_lock(task);
	if (task->exit_state) {
		ret = task->exit_state;
		goto read_ret;
	}

	ret = wait_event_interruptible_locked(task->signal->wait_pidfd,
					      (task->exit_state != 0));
	if (!ret)
		ret = task->exit_state;
	/*
	 * The wait should return only if the task's state has
	 * changed to something non-zero.
	 */
	WARN_ON_ONCE(!ret);
read_ret:
	pidfd_wait_unlock(task);
	return ret;
}

static __poll_t pidfd_wait_poll(struct file *file, struct poll_table_struct *pts)
{
	struct pidfd_wait_priv *priv = file->private_data;
	struct task_struct *task = priv->task;
	int ret, poll_flags = (POLLIN | POLLRDNORM);

	ret = pidfd_read_exit_status(task);
	if (ret < 0)
		poll_flags |= POLLERR;

	return poll_flags;
}

static ssize_t pidfd_wait_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	void *buf;
	struct file *file = iocb->ki_filp;
	struct pidfd_wait_priv *priv = file->private_data;
	struct task_struct *task = priv->task;
	ssize_t ret, size_ret;

	ret = pidfd_read_exit_status(task);

	/*
	 * Debugging, remove later.
	 * sprintf(buf, "status[pid:%d exit_state=%d]", pid_nr(pid), ret);
	 * trace_printk("Returning exit_state %s\n", buf);
	 */

	size_ret = sizeof(task->exit_state) + sizeof(siginfo_t);
	buf = kmalloc(size_ret, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, &task->exit_state, sizeof(task->exit_state));
	memcpy(buf + sizeof(task->exit_state), &task->signal->exit_siginfo,
	       sizeof(task->signal->exit_siginfo));

	ret = copy_to_iter(buf, size_ret, to);
	kfree(buf);
	return ret;
}

static int pidfd_wait_release(struct inode *inode, struct file *file)
{
	struct pidfd_wait_priv *priv = file->private_data;

	put_pid(priv->pid);
	put_task_struct(priv->task);
	if (priv)
		kfree(priv);

	return 0;
}

static const struct file_operations pidfd_wait_file_ops = {
	.read_iter	= pidfd_wait_read_iter,
	.poll		= pidfd_wait_poll,
	.release	= pidfd_wait_release,
};

SYSCALL_DEFINE1(pidfd_wait, int, pidfd)
{
	struct fd f;
	struct pid *pid;
	int ret = 0;
	struct pidfd_wait_priv *priv = NULL;

	f = fdget_raw(pidfd);
	if (!f.file)
		return -EBADF;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err;
	}

	/* Is this a pidfd? */
	pid = tgid_pidfd_to_pid(f.file);
	if (IS_ERR(pid)) {
		ret = PTR_ERR(pid);
		goto err;
	}
	fdput(f);

	read_lock(&tasklist_lock);
	priv->pid = get_pid(pid);
	priv->task = get_pid_task(pid, PIDTYPE_PID);
	read_unlock(&tasklist_lock);

	if (IS_ERR_OR_NULL(priv->task)) {
		ret = -ESRCH;
		goto err;
	}

	return anon_inode_getfd("pidfd_wait", &pidfd_wait_file_ops,
				priv, O_RDONLY);
err:
	if (priv)
		kfree(priv);
	fdput(f);
	return ret;
}
