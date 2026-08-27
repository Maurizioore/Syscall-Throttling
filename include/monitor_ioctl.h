#ifndef MONITOR_IOCTL_H
#define MONITOR_IOCTL_H
#include <linux/ioctl.h>
#include <linux/types.h>

/*this file contain the contract that the user and the module must adhere to.*/

struct monitor_stats{
	unsigned long peak_delay_ns; //maximum delay in nanoseconds
	char peak_delay_prog[64]; //name of the program delayed
	int peak_delay_uid; //uid of the user delayed
	
	int peak_blocked_threads; //max number of threads blocked simultaneously
	
	unsigned long total_blocked_samples;
	unsigned long total_seconds_sampled;
	
};

/*define the command for ioctl*/
#define MAGIC_NUM 'M'
#define IOCTL_SET_MAX _IOW(MAGIC_NUM,1,int) 
#define IOCTL_ADD_SYSCALL _IOW(MAGIC_NUM,2,int)
#define IOCTL_ADD_UID _IOW(MAGIC_NUM,3,int)
#define IOCTL_ADD_PROGRAM_NAME _IOW(MAGIC_NUM,4,char[64])
#define IOCTL_REMOVE_SYSCALL _IOW(MAGIC_NUM,5,int)
#define IOCTL_REMOVE_UID _IOW(MAGIC_NUM,6,int)
#define IOCTL_REMOVE_PROGRAM_NAME _IOW(MAGIC_NUM,7,char[64])
#define IOCTL_ENABLE_MONITOR _IO(MAGIC_NUM,8)
#define IOCTL_DISABLE_MONITOR _IO(MAGIC_NUM,9)
#define GET_STATS _IOR(MAGIC_NUM,10,struct monitor_stats*)

#endif
