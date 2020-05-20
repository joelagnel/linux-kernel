#include "/s/routines.h"
#define PR_SET_CORE_SCHED              57
#define cpu_relax()

static int *stop_test;
static int *start_test;

#define create_var(var) var = mmap(NULL, sizeof *var, PROT_READ | PROT_WRITE, \
				MAP_SHARED | MAP_ANONYMOUS, -1, 0)

static void task(int task_id) {
    unsigned long i;
    bool prc = (task_id == 1);

    while(!*start_test)
	cpu_relax();

    trace_printk("Task %d: Started test\n", task_id);

    // 500 ms
    for (i = 0; i < 200000000ULL; i++);

    if (prc) {
	trace_printk("Task %d: Setting prctl\n", task_id);
	// Set prctl, tasks should be migrated away.
	if (prctl(PR_SET_CORE_SCHED, 1)) {
	    perror("prctl failed\n");
	    trace_printk("Task %d: Failed prctl\n", task_id);
	} else {
	    trace_printk("Task %d: Success prctl\n", task_id);
	}
    }


    // 500 ms - let run a bit after migrations
    for (i = 0; i < 200000000ULL; i++);

    while(!*stop_test)
	cpu_relax();

    trace_printk("Task %d: Ended test\n", task_id);
}

#define NR_TASKS 4

void main(void) {
    int pid[NR_TASKS], i;
    char pid_cmd[100];
    struct sched_param param;

    create_var(stop_test);
    create_var(start_test);

    for (i = 0; i < NR_TASKS; i++) {
        FORK_FN(pid[i], task, i);
        pin_to_cpu_range(pid[i], 2, 3);
    }

    sleep_ms(100);

    /* Start test */
    *start_test = 1;

    trace_printk("Tests started\n");

    sleep_ms(2000);

    *stop_test = 1;
    while(wait(NULL) != -1);

    trace_printk("All threads exited, test done\n");
}
