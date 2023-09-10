#ifndef NOLIBC
#include <unistd.h>
#include <sys/time.h>
#endif

volatile unsigned long delaycount;

// Write a function fork and exec stress-ng with some params
// ex: stress-ng --cpu 1 --cpu-method matrixprod --cpu-ops 1000000 --cpu-maximize --metrics-brief --perf -t 10
void run_stress_ng() {
    pid_t pid = fork();
    if (pid == 0) {
	char *args[] = {"stress-ng", "--cpu", "1", "--cpu-method", "matrixprod", "--cpu-ops", "1000000", "--perf", "-t", "5", NULL};
	execve(args[0], args, NULL);
    }
}

int main(int argc, char *argv[])
{
	int i;
	struct timeval tv;
	struct timeval tvb;

	run_stress_ng();

	for (;;) {
		sleep(1);
		/* Need some userspace time. */
		if (gettimeofday(&tvb, NULL))
			continue;
		do {
			for (i = 0; i < 1000 * 100; i++)
				delaycount = i * i;
			if (gettimeofday(&tv, NULL))
				break;
			tv.tv_sec -= tvb.tv_sec;
			if (tv.tv_sec > 1)
				break;
			tv.tv_usec += tv.tv_sec * 1000 * 1000;
			tv.tv_usec -= tvb.tv_usec;
		} while (tv.tv_usec < 1000);
	}
	return 0;
}
