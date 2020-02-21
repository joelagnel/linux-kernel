#include "/s/routines.h"

#define cpu_relax()

static int *stop_test;
static int *start_test_cfs;
static int *cfs_started_test;
static int *start_test_rt;

#define create_var(var) var = mmap(NULL, sizeof *var, PROT_READ | PROT_WRITE, \
				MAP_SHARED | MAP_ANONYMOUS, -1, 0)

void child_rt() {
    while(!*start_test_rt)
	cpu_relax();

    while(!*stop_test)
	cpu_relax();
}

void child_cfs() {
    trace_printk("starting CFS sleep\n");
    while(!*start_test_cfs)
	sleep_ms(2000);
    trace_printk("ending CFS sleep\n");

    *cfs_started_test = 1;

    while(!*stop_test)
	cpu_relax();
}

void main(void) {
    int cfs_pid, rt_pid;
    char pid_cmd[100];
    struct sched_param param;

    create_var(stop_test);
    create_var(start_test_cfs);
    create_var(cfs_started_test);
    create_var(start_test_rt);

    FORK_FN(cfs_pid, child_cfs);
    FORK_FN(rt_pid, child_rt);

    pin_to_cpu(0, 1); /* Current on core 1 */

    /* Siblings */
    pin_to_cpu(cfs_pid, 2);
    pin_to_cpu(rt_pid, 3);

    sleep_ms(10);

    param.sched_priority = 50;
    if(sched_setscheduler(rt_pid, SCHED_FIFO, &param) == -1)
	perror("sched_setscheduler failed");

    trace_printk("Tagging started\n");

    /* Tag child_cfs */
    system("umount /mnt/temp-test");
    system("rm -rf /mnt/temp-test");
    system("mkdir -p /mnt/temp-test");
    system("mount -t cgroup2 none /mnt/temp-test");
    system("mkdir /mnt/temp-test/tagged/");

    write_to_file("/mnt/temp-test/cgroup.subtree_control", "+cpu");
    write_to_file("/mnt/temp-test/tagged/cgroup.subtree_control", "+cpu");

    snprintf(pid_cmd, 100, "%d", cfs_pid);
    write_to_file("/mnt/temp-test/tagged/cgroup.procs", pid_cmd);
    write_to_file("/mnt/temp-test/tagged/cpu.tag", "1");

    trace_printk("Tagging completed\n");

    /* Start test */
    *start_test_rt = 1;
    *start_test_cfs = 1;

    trace_printk("Tests started\n");

    while(!*cfs_started_test)
	cpu_relax();

    trace_printk("CFS wake-up detected\n");

    // During this interval it would have kicked out the RT thread
    // when it shouldn't have.
    sleep_ms(500);

    *stop_test = 1;
    while(wait(NULL) != -1);

    trace_printk("All threads exited, test done\n");
}
