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

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

static int fd = -1;

static void check(bool condition, const char *description, int expected_errno)
{
	total_tests++;
	if (condition) {
		passed_tests++;
		printf("[OK] %s\n", description);
	} else {
		failed_tests++;
		if (expected_errno != 0) {
			printf("[ERROR] %s (Expected errno: %d [%s], Received: %d [%s])\n",
			       description, expected_errno, strerror(expected_errno), errno, strerror(errno));
		} else {
			printf("[ERROR] %s (Unexpected error: %d [%s])\n",
			       description, errno, strerror(errno));
		}
	}
}

/*
*@brief: this function permit to verify if the user that execute the program is root or not. The method used is based on the invocation of a privileged command "set max". If the action is permit => we are root, otherwhise we are non privileged user.
*/
static void test_permissions(void)
{
	printf("\n--- Test 1: permission level check ---\n");
	unsigned int val = 10;
	int ret = ioctl(fd, IOCTL_SET_MAX, &val);
	
	if (geteuid() != 0) {
		check(ret < 0 && errno == EPERM,
		      "Block non-root user on privileged command", EPERM);
	} else {
		check(ret == 0,
		      "Execution allowed for root user", 0);
	}
}

/*
*@brief: this function test the set max 
*/
static void test_set_max(void)
{
	printf("\n--- Test 2: MAX threshold configuration ---\n");
	unsigned int valid_val = 150;
	unsigned int zero_val = 0;

	/*valid value*/
	int ret = ioctl(fd, IOCTL_SET_MAX, &valid_val);
	check(ret == 0, "Set new rate limit to 150", 0);

	/*zero value*/
	ret = ioctl(fd, IOCTL_SET_MAX, &zero_val);
	check(ret < 0 && errno == EINVAL, "Reject zero value with EINVAL", EINVAL);

	/*NULL pointer*/
	ret = ioctl(fd, IOCTL_SET_MAX, NULL);
	check(ret < 0 && errno == EFAULT, "Reject NULL pointer with EFAULT", EFAULT);
}

/*
*@brief: this function test the add and remove of syscall number 
*/
static void test_syscall(void)
{
	printf("\n--- Test 3: syscall registration and removal ---\n");
	unsigned int valid_sc = SYS_mkdir;
	unsigned int invalid_sc = 100000;
	unsigned int unregistered_sc = SYS_rmdir;

	/*valid insertion*/
	int ret = ioctl(fd, IOCTL_ADD_SYSCALL, &valid_sc);
	check(ret == 0, "Register valid syscall (mkdir)", 0);

	/*duplicate attempt*/
	ret = ioctl(fd, IOCTL_ADD_SYSCALL, &valid_sc);
	if (ret < 0 && errno == EEXIST) {
		check(true, "Reject duplicate syscall with EEXIST", EEXIST);
	} else if (ret == 0) {
		printf("[INFO] Duplicate checking disabled at build time, removing duplicate entry\n");
		ioctl(fd, IOCTL_REMOVE_SYSCALL, &valid_sc);
	} else {
		check(false, "Duplicate handling failed", EEXIST);
	}

	/* syscall out of x86-64 bounds */
	ret = ioctl(fd, IOCTL_ADD_SYSCALL, &invalid_sc);
	check(ret < 0 && errno == EINVAL, "Reject out-of-bounds syscall with EINVAL", EINVAL);

	/*NULL pointer on add */
	ret = ioctl(fd, IOCTL_ADD_SYSCALL, NULL);
	check(ret < 0 && errno == EFAULT, "ADD_SYSCALL with NULL fails with EFAULT", EFAULT);

	/*remove registered syscall*/
	ret = ioctl(fd, IOCTL_REMOVE_SYSCALL, &valid_sc);
	check(ret == 0, "Remove registered syscall (mkdir)", 0);

	/*remove unregistered syscall*/
	ret = ioctl(fd, IOCTL_REMOVE_SYSCALL, &unregistered_sc);
	check(ret < 0 && errno == ENOENT, "Remove non-existent syscall fails with ENOENT", ENOENT);

	/*NULL pointer on remove*/
	ret = ioctl(fd, IOCTL_REMOVE_SYSCALL, NULL);
	check(ret < 0 && errno == EFAULT, "REMOVE_SYSCALL with NULL fails with EFAULT", EFAULT);
}

/*
*@brief: this function test the add and remove of uid
*/
static void test_uid(void)
{
	printf("\n--- Test 4: UID registration and removal ---\n");
	unsigned int valid_uid = 1002;
	unsigned int invalid_uid = (unsigned int)-1;
	unsigned int unregistered_uid = 9999;

	/* valid UID insertion*/
	int ret = ioctl(fd, IOCTL_ADD_UID, &valid_uid);
	check(ret == 0, "Register valid UID (1002)", 0);

	/*duplicate attempt*/
	ret = ioctl(fd, IOCTL_ADD_UID, &valid_uid);
	if (ret < 0 && errno == EEXIST) {
		check(true, "Reject duplicate UID with EEXIST", EEXIST);
	} else if (ret == 0) {
		printf("[INFO] Duplicate checking disabled, cleaning up duplicated entry\n");
		ioctl(fd, IOCTL_REMOVE_UID, &valid_uid);
	} else {
		check(false, "Duplicate UID handling failed", EEXIST);
	}

	/*invalid UID (-1)*/
	ret = ioctl(fd, IOCTL_ADD_UID, &invalid_uid);
	check(ret < 0 && errno == EINVAL, "Reject (unsigned)-1 UID with EINVAL", EINVAL);

	/*NULL pointer on add*/
	ret = ioctl(fd, IOCTL_ADD_UID, NULL);
	check(ret < 0 && errno == EFAULT, "ADD_UID with NULL fails with EFAULT", EFAULT);

	/*remove registered UID*/
	ret = ioctl(fd, IOCTL_REMOVE_UID, &valid_uid);
	check(ret == 0, "Remove registered UID (1002)", 0);

	/*remove unregistered UID*/
	ret = ioctl(fd, IOCTL_REMOVE_UID, &unregistered_uid);
	check(ret < 0 && errno == ENOENT, "Remove non-existent UID fails with ENOENT", ENOENT);

	/*NULL pointer on remove*/
	ret = ioctl(fd, IOCTL_REMOVE_UID, NULL);
	check(ret < 0 && errno == EFAULT, "REMOVE_UID with NULL fails with EFAULT", EFAULT);
}

/*
*@brief: this function test the add and remove of program name
*/
static void test_program_name(void)
{
	printf("\n--- Test 5: program name registration and removal ---\n");
	char valid_prog[PROGRAM_NAME_DIMENSION] = "test_probe";
	char unregistered_prog[PROGRAM_NAME_DIMENSION] = "non_existent";
	char *out_of_limits_program_name="test_the_out_of_limits_program_name";
	char empty_prog[PROGRAM_NAME_DIMENSION] = "";

	/*valid registration*/
	int ret = ioctl(fd, IOCTL_ADD_PROGRAM_NAME, valid_prog);
	check(ret == 0, "Register valid program name ('test_probe')", 0);

	/*duplicate attempt*/
	ret = ioctl(fd, IOCTL_ADD_PROGRAM_NAME, valid_prog);
	if (ret < 0 && errno == EEXIST) {
		check(true, "Reject duplicate program name with EEXIST", EEXIST);
	} else if (ret == 0) {
		printf("[INFO] Duplicate checking disabled, cleaning up duplicated entry\n");
		ioctl(fd, IOCTL_REMOVE_PROGRAM_NAME, valid_prog);
	} else {
		check(false, "Duplicate program name handling failed", EEXIST);
	}

	/*empty string*/
	ret = ioctl(fd, IOCTL_ADD_PROGRAM_NAME, empty_prog);
	check(ret < 0 && errno == EINVAL, "Reject empty string with EINVAL", EINVAL);

	/*NULL pointer*/
	ret = ioctl(fd, IOCTL_ADD_PROGRAM_NAME, NULL);
	check(ret < 0 && errno == EFAULT, "ADD_PROGRAM_NAME with NULL fails with EFAULT", EFAULT);
	
	/*out of limit*/
	ret=ioctl(fd,IOCTL_ADD_PROGRAM_NAME,out_of_limits_program_name);
	check(ret<0 && errno == EINVAL, "Program name passed is out of the limit", EINVAL);
	/*remove registered program*/
	ret = ioctl(fd, IOCTL_REMOVE_PROGRAM_NAME, valid_prog);
	check(ret == 0, "Remove registered program name ('test_probe')", 0);

	/*remove unregistered program*/
	ret = ioctl(fd, IOCTL_REMOVE_PROGRAM_NAME, unregistered_prog);
	check(ret < 0 && errno == ENOENT, "Remove non-existent program fails with ENOENT", ENOENT);

	/*remove with empty string*/
	ret = ioctl(fd, IOCTL_REMOVE_PROGRAM_NAME, empty_prog);
	check(ret < 0 && errno == EINVAL, "Remove with empty string fails with EINVAL", EINVAL);

	/*remove NULL pointer*/
	ret = ioctl(fd, IOCTL_REMOVE_PROGRAM_NAME, NULL);
	check(ret < 0 && errno == EFAULT, "REMOVE_PROGRAM_NAME with NULL fails with EFAULT", EFAULT);
}

/*
*@brief: this function test the enable and disable of the monitor
*/
static void test_monitor_state(void)
{
	printf("\n--- Test 6: monitor enable and disable ---\n");

	int ret = ioctl(fd, IOCTL_ENABLE_MONITOR);
	check(ret == 0, "Enable monitor (IOCTL_ENABLE_MONITOR)", 0);

	ret = ioctl(fd, IOCTL_DISABLE_MONITOR);
	check(ret == 0, "Disable monitor (IOCTL_DISABLE_MONITOR)", 0);
}

/*
*@brief: this function test the retrive of the stats 
*/
static void test_stats(void)
{
	printf("\n--- Test 7: read statistics ---\n");
	struct monitor_stats stats;
	memset(&stats, 0, sizeof(stats));

	/* valid statistics retrieval */
	int ret = ioctl(fd, IOCTL_GET_STATS, &stats);
	check(ret == 0, "IOCTL_GET_STATS invocation succeeded", 0);

	if (ret == 0) {
		printf("Peak delay: %llu ns\n", stats.peak_delay_ns);
		printf("Peak program: '%s'\n", stats.peak_delay_prog);
		printf("Peak UID: %u\n", stats.peak_delay_uid);
		printf("Peak blocked threads: %d\n", stats.peak_blocked_threads);
		printf("Blocked thread samples: %llu\n", stats.total_blocked_samples);
		printf("Sampled seconds: %llu\n", stats.total_seconds_sampled);
		printf("Average blocked threads: %llu\n",
		       stats.average_thread_blocked / 1000);
	}

	/* NULL pointer for GET_STATS */
	ret = ioctl(fd, IOCTL_GET_STATS, NULL);
	check(ret < 0 && errno == EFAULT, "GET_STATS with NULL pointer fails with EFAULT", EFAULT);
}

/*
*@brief: this function test the read function
*/
static void test_device_read(void)
{
	printf("\n--- Test 8: Read Registered Lists via read() ---\n");

	/* Register temporary entries to verify they appear in read() output */
	unsigned int sc = SYS_mkdir;
	unsigned int uid = 1000;
	char prog[PROGRAM_NAME_DIMENSION] = "test_read";

	if (geteuid() == 0) {
		ioctl(fd, IOCTL_ADD_SYSCALL, &sc);
		ioctl(fd, IOCTL_ADD_UID, &uid);
		ioctl(fd, IOCTL_ADD_PROGRAM_NAME, prog);
	}

	/* Reopen descriptor with zeroed file offset */
	int read_fd = open(DEVICE_PATH, O_RDONLY);
	if (read_fd < 0) {
		check(false, "Failed to open device in read-only mode", errno);
		return;
	}

	char buffer[4096];
	memset(buffer, 0, sizeof(buffer));

	ssize_t bytes = read(read_fd, buffer, sizeof(buffer) - 1);
	check(bytes > 0, "read() operation returned data from driver", 0);

	close(read_fd);

	/* Clean up registered entries */
	if (geteuid() == 0) {
		ioctl(fd, IOCTL_REMOVE_SYSCALL, &sc);
		ioctl(fd, IOCTL_REMOVE_UID, &uid);
		ioctl(fd, IOCTL_REMOVE_PROGRAM_NAME, prog);
	}
}

int main(void)
{
	printf("============================================================\n");
	printf("   Test Suite: Syscall Throttling Kernel Module\n");
	printf("============================================================\n");

	fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Error opening " DEVICE_PATH);
		printf("Make sure the kernel module is loaded and the device node exists.\n");
		return EXIT_FAILURE;
	}
	
	test_permissions();

	if (geteuid() == 0) {
		test_set_max();
		test_syscall();
		test_uid();
		test_program_name();
		test_monitor_state();
		test_stats();
		test_device_read();
	} else {
		/* Non-root execution: verify read-only accessible operations */
		printf("\n[INFO] Running as non-root user. Verifying read-only commands...\n");
		test_stats();
		test_device_read();
		printf("\nTo test privileged configuration commands, rerun with sudo.\n");
	}

	close(fd);

	printf("\n============================================================\n");
	printf("   Test Summary: %d Total, %d Passed, %d Failed\n",
	       total_tests, passed_tests, failed_tests);
	printf("============================================================\n");

	return (failed_tests == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
