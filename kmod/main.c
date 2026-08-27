#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include "../include/monitor_ioctl.h"
#include "monitor.h"
#include "setup_device.h"

/*this file contains the module initialization and cleanup functions for the monitor module */

/*Meta Information*/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maurizio Renzi: mauriziorenzi68@gmail.com");
MODULE_DESCRIPTION("this is a LKM mdoule that implement a syscall throttling logic");
MODULE_VERSION("1.0");


/*
* @brief: this function is called when the module is loaded into the kernel
*/
	
static int __init MonitorModuleInit(void){
	printk("Starting Initialization module\n");
	int ret=setup_device();
	if(ret!=0){
		return ret;
	}
	
	return 0;
}

/*
*@brief: This function is called when the module is removed from the kernel
*/

static void __exit MonitorModuleExit(void){
	remove_device();
}

module_init(MonitorModuleInit);
module_exit(MonitorModuleExit);

