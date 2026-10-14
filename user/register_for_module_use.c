#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <stdbool.h>


#include "../include/monitor_ioctl.h"

#define PATH_NAME "/dev/syscall_throttling"

int fd;

void set_max(int new_max){
	printf("change max\n");
	ioctl(fd,IOCTL_SET_MAX,&new_max);
	printf("successfully set %d value as new max\n", new_max);
	
}
void enable_monitor(void){
	printf("start to enable the monitor\n");
	ioctl(fd,IOCTL_ENABLE_MONITOR);
	
}
bool register_userid(unsigned int uid){
	int ret;
	printf("start to registry the euid %d\n",uid);
	ret=ioctl(fd,IOCTL_ADD_UID,&uid);
	if(ret<0 && errno!= EEXIST){
		printf("some error has occured while registering the uid\n");
		return false;
	}
	printf("succesfully registered the uid %d\n",uid);
	return true;

}

bool register_program_name(char * program_name){
	int ret;
	printf("start to registry the program name %s\n",program_name);
	ret=ioctl(fd,IOCTL_ADD_PROGRAM_NAME,program_name);
	if(ret<0 && errno != EEXIST){
		printf("some errors has occured during the add of the program name\n");
		return false;
	}
	printf("succesfully registered the program: %s\n",program_name);
	return true;
}

bool register_syscall(unsigned int syscall_number){
	int ret;
	printf("start to registry the syscall passed in input\n");
	ret=ioctl(fd,IOCTL_ADD_SYSCALL,&syscall_number);
	if(ret<0 && errno != EEXIST){
		printf("some errors has occured during the add of the syscall number\n");
		return 	false;
	}
	printf("succesfully registered the syscall number: %d\n",syscall_number);
	return true;
	
}

int main(int argc, char **argv){

	int new_max=10;
	unsigned int syscall_to_registry=SYS_mkdir;
	char * program_to_registry="4_launch_thread";
	unsigned int euid_to_registry=1000;
	if(geteuid() != 0){
		printf("This program run only for root user\n");
		return EXIT_FAILURE;
	}
	//open the device 
	fd=open(PATH_NAME,O_RDWR);
	if(fd<0){
		perror("Critical error during the open of: " PATH_NAME);
		printf("Verify that the module is correctly loaded \n");
		return EXIT_FAILURE;
	}
	
	//the device driver is open, now register a syscall
	if(!register_syscall(syscall_to_registry)){
		return EXIT_FAILURE;
	}
	
	//now register the program 
	if(!register_program_name(program_to_registry)){
		return EXIT_FAILURE;
	}
	
	//now register the userid
	if(!register_userid(euid_to_registry)){
		return EXIT_FAILURE;
	}
	//now the user ad the program are registered, try also to register only the user and delete the part of registration of the program
	
	//now enable the monitor
	enable_monitor();
	
	//change max
	set_max(new_max);
	
	//now we have monitor enabled,syscall #, program name and uid registered
	close(fd);
	
}
