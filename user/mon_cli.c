#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <string.h>
#include "include/monitor_ioctl.h"
/*define the command for ioctl*/

#define IOCTL_SET_MAX _IOW('V',1, int) //update the linked_list_queue_dimension (MAX_NUM_MESSAGE)
#define IOCTL_SET_TIMER _IOW('V',2,int) //update the timer (TIMER_TICK)

#define BUFFER_DIMENSION 255
int fd;
void* write_function(void* arg){
	int thr_num=(int)(long)arg;
	char buffer[BUFFER_DIMENSION];
	
	/*prepare the message*/
	int writed=snprintf(buffer,sizeof(buffer),"I'm the thread %d\n",thr_num);
	if(writed>=BUFFER_DIMENSION){
		writed=BUFFER_DIMENSION-1;
	}
	//send the message to the kernel
	if(write(fd,buffer,writed)<0){
		perror("some error has occurred during the write function\n");
	}
	return NULL;

}

void read_stats(){
	char buffer[BUFFER_DIMENSION];
	
	//reset the offset to the zero value 
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
		perror("some error has occurred during the lseek");
		return;
    	}
    		
    	/*clean the buffer */
	memset(buffer, 0, BUFFER_DIMENSION);
	
	/*read from the kernel*/
	int bytes_read = read(fd, buffer, BUFFER_DIMENSION - 1);
    
	if (bytes_read == -1) {
		perror("Failed to read from the kernel space");
	} 
	else if(bytes_read == 0){
		printf("EOF: no data read from the kerbel, wrong offset?\n");
	} 
	else{
		printf("\n--- response from the kerbel ---\n%s---------------------------\n", buffer);
	}
    	
    	
}
int main(){
	pthread_t threads[20];
	
	fd = open("/dev/def_msg", O_RDWR);
	if(fd<0){
		perror("something went wrong with the opening of the file driver!\n");
		return -1;
	}
	printf("channel to /dev/def_msg open successfully\n");
	
	/*start with ioctl commands*/
	
	int new_max = 30;
   	printf("\n[IOCTL] set new MAX_NUM_MESSAGE in the linked list to: %d...\n", new_max);
    	if (ioctl(fd, IOCTL_SET_MAX, &new_max) < 0) {
        	perror("Some error has occurred with IOCTL_SET_MAX");
    	}
    	
    	int new_timer = 4000; // timer goes off every 4 seconds
    	printf("[IOCTL] set new TIMER value a %d ms...\n", new_timer);
    	if (ioctl(fd, IOCTL_SET_TIMER, &new_timer) < 0) {
        	perror("Some error has occurred with  IOCTL_SET_TIMER");
    	}
	
	/*start to write in the kernel in concurrency*/
	printf("\n[WRITE] Launch %d thread simultaneously. to write in the queue...\n", 20);
    	for(int i = 0; i < 20; i++){
        	pthread_create(&threads[i], NULL, write_function, (void*)(long)i);
    	}
    
    	// Waiting all thread end
    	for(int i = 0; i < 20; i++){
        	pthread_join(threads[i], NULL);
    	}
    	
    	//start reading the stats from the kernel
    	printf("\n[READ 1] Richiedo le statistiche attuali al Kernel...\n");
    	read_stats(); // Expected: total=20, in the queue=19/20


    	printf("\n[WAIT] sleep for 5 seconds...\n");
    	sleep(5);
	printf("\n[WRITE] write new thread (ID: 100) \n");
   	write_function((void*)(long)100);

    	printf("\n[READ 2] read the actual stats...\n");
    	read_stats(); 

    	printf("\nTest completed. Close the device.\n");
    	close(fd);
    	return 0;
}
