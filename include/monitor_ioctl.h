#ifndef MONITOR_IOCTL_H
#define MONITOR_IOCTL_H
#include <linux/ioctl.h>
#include <linux/types.h>

/*this file contain the contract that the user and the module must adhere to.*/
#define PROGRAM_NAME_DIMENSION 16

struct monitor_stats{

	//maximum time a thread waited in wq until now express in nanoseconds
	__u64 peak_delay_ns; 
	
	//name of the program delayed
	char peak_delay_prog[PROGRAM_NAME_DIMENSION]; 
	
	//uid of the user delayed 
	unsigned int  peak_delay_uid; 
	
	//max number of threads blocked simultaneously
	__s32 peak_blocked_threads; 
	
	//number of thread blocked until now 
	__u64 total_blocked_samples;
	
	//numer of seconds passed until now from the enable of the monitor 
	__u64 total_seconds_sampled;
	
	//average number of blocked thread until now defined as total_blocked_samples*1000/total_seconds_sampled, user should divide for 1000 to obtain the exact number of thread blocked per second 
	__u64 average_thread_blocked;
};

/*define the command for ioctl*/
#define MAGIC_NUM 'M'
#define IOCTL_SET_MAX _IOW(MAGIC_NUM,1,int) 
#define IOCTL_ADD_SYSCALL _IOW(MAGIC_NUM,2,int)
#define IOCTL_ADD_UID _IOW(MAGIC_NUM,3,int)
#define IOCTL_ADD_PROGRAM_NAME _IOW(MAGIC_NUM,4,char[16])
#define IOCTL_REMOVE_SYSCALL _IOW(MAGIC_NUM,5,int)
#define IOCTL_REMOVE_UID _IOW(MAGIC_NUM,6,int)
#define IOCTL_REMOVE_PROGRAM_NAME _IOW(MAGIC_NUM,7,char[16])
#define IOCTL_ENABLE_MONITOR _IO(MAGIC_NUM,8)
#define IOCTL_DISABLE_MONITOR _IO(MAGIC_NUM,9)
#define IOCTL_GET_STATS _IOR(MAGIC_NUM,10,struct monitor_stats*)

#endif

