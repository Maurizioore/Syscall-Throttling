#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include "../include/monitor_ioctl.h"

#define DEVICE_PATH "/dev/syscall_throttling"


int main(void){

	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Critical error during the open of: " DEVICE_PATH);
		printf("Verify that the module is correctly loaded\n");
		return EXIT_FAILURE;
	}
	struct monitor_stats stats;
	int ret = ioctl(fd, IOCTL_GET_STATS, &stats);
	if(ret!=0){
		printf("some error occurred during the get stats operation\n");
		return -1;
	}
	printf("\n --------Ioctl Get Stats Command ------------\n");
	printf("maximum time a thread waited in wq until now express in nanoseconds: %lld\nname of the program delayed: %s\nuid of the user delayed : %d\nmax number of threads blocked simultaneously: %d\nnumber of thread blocked until now : %lld\nnumer of seconds passed until now from the enable of the monitor : %lld\naverage_thread_blocked: %lld\n", stats.peak_delay_ns,stats.peak_delay_prog,stats.peak_delay_uid,
	stats.peak_blocked_threads,stats.total_blocked_samples,stats.total_seconds_sampled,stats.average_thread_blocked/1000);
	
	printf("\n --------Read call------------\n");
	printf("Content of the lists:\n");
	char read_buffer[4096];
	read(fd,read_buffer,4096);
	printf("%s\n",read_buffer);

	close(fd);

}
