#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <time.h>

#define NUM_THREADS 120
#define DUMMY_DIR "/tmp/cavia_dir"

/*
*@brief: this function is used to calculate the exact time in milliseconds. It takes seconds and nanoseconds and convert them
into a single decimal number 
*/
static double get_time_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	//return tge exact millisecond 
	return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

static double start_time;

void *worker_thread(void *arg)
{	
	//id of the current thread
	long id = (long)arg;
	char dirname[64];
	//store in dirname the string: "/tmp/cavia_dir_thread#"
	snprintf(dirname, sizeof(dirname), "%s_%ld", DUMMY_DIR, id);
	
	//get the actual time in ms 
	double t0 = get_time_ms();
	
	//create the dir "dirname"
	syscall(SYS_mkdir, dirname, 0777);
	//delete the dir 
	syscall(SYS_rmdir, dirname);
	
	//get the time and compute the delta 
	double t1 = get_time_ms();
	double delta = t1 - t0;

	printf("[THREAD #%02ld] Awaken/Finished in %7.2f ms %s\n",id, delta, 
	(delta > 300.0) ? " <-- [SUSPENDED IN WAITQUEUE]" : " <-- [PASSED IMMEDIATELY]");

	return NULL;
}

int main(void)
{	
	
	pthread_t threads[NUM_THREADS];
	printf("[TEST MT] launching %d concurrent threads with MAX = 5...\n", NUM_THREADS);
	
	
	start_time = get_time_ms();
	
	//launch NUM_THREADS threads with "i" as argument 
	for (long i = 0; i < NUM_THREADS; i++) {
		pthread_create(&threads[i], NULL, worker_thread, (void *)i);
	}
	
	//wait all threads
	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

	printf("[TEST MT] All threads have completed execution.\n");
	return 0;
}
