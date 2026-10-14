#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "../include/monitor_ioctl.h"
#include "monitor.h"
#include "setup_device.h"
#include "hooking.h"

/*this file contains the module initialization and cleanup functions for the monitor module */

/*Meta Information*/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maurizio Renzi: mauriziorenzi68@gmail.com");
MODULE_DESCRIPTION("System Call Monitor and Throttling Module");
MODULE_VERSION("1.0");


/*
* @brief: this function is called when the module is loaded into the kernel
*/
	
static int __init MonitorModuleInit(void){

	int ret;
	
	ret=setup_device();
	if(ret!=0){
		return ret;
	}
	setup_timer();
	return 0;
}

/*
*@brief: This function is called when the module is removed from the kernel
*/

static void __exit MonitorModuleExit(void){
	
	//remove device, no other ioctl command can be enter
	remove_device();
	//reset all the original entry of the syscall table, no other thread from this point execute our wrapper
	restore_all_registered_dispatchers();
	//signal to all thread that the module is in exit phase 
	notify_module_unloading();
	//turn off the timer and wake up all the threads
	cleanup_timer();
	//insert this thread (the rmmod) in wq to wait the end of all activities
	wait_for_active_threads();
	//all threads have finished it's execution, free the lists
	monitor_cleanup_lists();
	pr_info("Syscall Throttling Manager: Module unloaded cleanly and safely.\n");

	
}

module_init(MonitorModuleInit);
module_exit(MonitorModuleExit);

