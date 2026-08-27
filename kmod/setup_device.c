#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include "setup_device.h"
#include "monitor.h"

/*Variables for device and device class*/
static dev_t my_device_nr;
static struct class *my_class;
static struct cdev my_device;

/*
*@brief: this function allocate and initialize the device
*@return: 0 in succesfully case, -1 if we have some error
*/
int setup_device(void){
		
	printk("starting mounting the device driver...\n");
	/*Allocate a device nr*/
	if (alloc_chrdev_region(&my_device_nr,0,1,DRIVER_NAME)<0){
		printk("Device could not be allocated!\n");
		return -1;
	}
	printk("syscall throttling manager - Device nr: Major: %d, Minor: %d was registered!\n", my_device_nr>>20, my_device_nr & 0xfffff);
	
	/*Create device class*/
	if((my_class=class_create(DRIVER_CLASS))==NULL){
		printk("device class cannot be created\n");
		goto ClassError;
	}
	/* Create Device File*/
	if(device_create(my_class,NULL,my_device_nr,NULL,DRIVER_NAME)==NULL){
		printk("Failed to create the device\n");
		goto FileError;
	} 
	cdev_init(&my_device,&fops);
	
	if(cdev_add(&my_device,my_device_nr, 1)<0){
		printk("Failed to add the device!\n");
		goto AddError;
	}
	printk("device driver mounted\n");
	return 0;

AddError:
	device_destroy(my_class,my_device_nr);
FileError:
	class_destroy(my_class);
ClassError:	
	unregister_chrdev_region(my_device_nr,1);
	return -1;
}

/*
*@brief: this function remove the allocated device
*/
void remove_device(void){
	printk("start to remove the kernel driver module from the system!\n");
	cdev_del(&my_device);
	device_destroy(my_class,my_device_nr);
	class_destroy(my_class);
	unregister_chrdev_region(my_device_nr,1);
	printk("kernel module removed, goodbye Kernel\n");
}



