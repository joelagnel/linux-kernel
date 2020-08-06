// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core-scheduling selftests.
 *
 * Copyright (C) 2020, Joel Fernandes.
 */

// XXX: On failures, clean up of cgroup, memory alloc, pthread barrier etc is needed.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

pthread_barrier_t *start_barrier;
pthread_barrierattr_t attr;

unsigned long nr_busy_loops = 100000000; /* 100M (~200ms)- Controls userspace cpu time (no syscalls) */
size_t zero_read_size = 50000000; /* 50 MB. */
int nr_tasks_per_group = 2;
int zero_read_ms = 200;
int nr_groups = 2;

char mntpath[256];
char *zero_buff;

struct group {
	pid_t *child_pids;
};

struct group *all_groups;

/* For debugging pthread_barrier, dumps bytes. */
char dump_pb(char *ctx)
{
	int i = 0;
	printf("%s: ", ctx);

	for (; i < sizeof(pthread_barrier_t); i++)
		printf("%x ", ((char *)start_barrier)[i]);

	printf("\n");
}

static uint64_t to_ns(struct timespec ts) {
    return ts.tv_sec * (uint64_t)1000000000L + ts.tv_nsec;
}

void read_zeros_for_ms(int ms)
{
        struct timespec ts_start, ts_current;
	uint64_t ns;
	int fd;

	ns = 1000000 * ms;
	fd = open("/dev/zero", O_RDONLY);

        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        for(;;) {
                clock_gettime(CLOCK_MONOTONIC, &ts_current);

		if ((to_ns(ts_current) - to_ns(ts_start)) > ns)
			break;

		if (read(fd, zero_buff, zero_read_size) != zero_read_size) {
			perror("Failed to read /dev/zero: ");
			exit(-1);
		}
	}

	close(fd);
}

void child_func(int group_num)
{
    int i;

    pthread_barrier_wait(start_barrier);
    printf("child in %d group starting\n", group_num);

    while(1) {
	read_zeros_for_ms(zero_read_ms);

	for(i = 0; i < nr_busy_loops; i++);
    }
}

int make_child(int group_num)
{
	pid_t pid = fork();

	if (pid < 0) {
		perror("Fork failed: ");
		exit(-1);
	} else if (pid != 0) {
		return pid;
	}

	child_func(group_num);
}

void make_cgroup(void)
{
	char *mnt;
	int ret;

	sprintf(mntpath, "/tmp/coresched-test-XXXXXX");
	mnt = mkdtemp(mntpath);
	if (!mnt) {
		perror("Failed to create mount: ");
		exit(-1);
	}

	ret = mount("nodev", mnt, "cgroup", 0, "cpu");
	if (ret == -1) {
		perror("Failed to mount cgroup: ");
		exit(-1);
	}

	return;
}

int make_group(int group_num)
{
	int i, ret, tfd;
	char cgroup_path[256], tasks_path[256], tag_path[256];
	char wrbuf[32];

	/* Make the cgroup node for this group */
	sprintf(cgroup_path, "%s/group%d", mntpath, group_num);
	ret = mkdir(cgroup_path, 0777);
	if (ret == -1) {
		perror("Failed to create group in cgroup: ");
		exit(-1);
	}
	sprintf(tasks_path, "%s/tasks", cgroup_path);

	/* Enable core-scheduling on the group */
	sprintf(tag_path, "%s/group%d/cpu.tag", mntpath, group_num);
	tfd = open(tag_path, O_WRONLY, 0666);
	if (tfd == -1) {
		perror("Open of cgroup tag path failed: ");
		exit(-1);
	}

	if (write(tfd, "1", 1) != 1) {
		perror("Failed to enable coresched on cgroup: ");
		exit(-1);
	}

	if (close(tfd) == -1) {
		perror("Failed to close tag fd: ");
		exit(-1);
	}

	/* Allocate child pids */
	all_groups[group_num].child_pids =
		calloc(nr_tasks_per_group, sizeof(struct group));
	if (!all_groups[group_num].child_pids) {
		perror("Memory alloc failed: ");
		exit(-1);
	}

	for (i = 0; i < nr_tasks_per_group; i++) {

		/* Fork */
		all_groups[group_num].child_pids[i] = make_child(i);

		/* Add child pid to group */
		tfd = open(tasks_path, O_WRONLY, 0666);
		if (tfd == -1) {
			perror("Open of cgroup tasks path failed: ");
			exit(-1);
		}

		sprintf(wrbuf, "%d\n", all_groups[group_num].child_pids[i]);
		if (write(tfd, wrbuf, strlen(wrbuf)) != strlen(wrbuf)) {
			perror("Failed to add task to cgroup: ");
			exit(-1);
		}

		if (close(tfd) == -1) {
			perror("Failed to close task fd: ");
			exit(-1);
		}
	}
}

/* pthread barrier ready to after this, call pthread_barrier_wait() */
void init_pthread_barrier()
{
	start_barrier = mmap(NULL, 8192, PROT_READ|PROT_WRITE,
			MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	memset(start_barrier, 0, 8192);
	pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_barrier_init(start_barrier, &attr, (nr_groups * nr_tasks_per_group) + 1);
}

int main()
{
	int i;

	init_pthread_barrier();

	/* Share memory all processes can read into. */
	zero_buff = mmap(NULL, zero_read_size, PROT_READ|PROT_WRITE,
			MAP_SHARED|MAP_ANONYMOUS, -1, 0);

	/* Before calling make_group() */
	make_cgroup();

	/* Before calling make_group() */
	all_groups = calloc(nr_groups, sizeof(struct group));
	if (!all_groups) {
		perror("Memory alloc failed: ");
		exit(-ENOMEM);
	}

	for (i = 0; i < nr_groups; i++) {
		make_group(i);
	}

	pthread_barrier_wait(start_barrier);
	printf("parent starting\n");

	while(1);
}

