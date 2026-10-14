#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/version.h>
#include "setup_device.h"
#include "monitor.h"

/*Variables for device and device class*/
static dev_t my_device_nr;
static struct class *my_class;
static struct cdev my_device;

/*
*@brief: callback to set the privilege of the node /dev to 0666 (rw-rw-rw-)
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static char *my_devnode(const struct device *dev, umode_t *mode)
#else
static char *my_devnode(struct device *dev, umode_t *mode)
#endif
{
	if (mode)
		*mode = 0666;
	return NULL;
}

/*
*@brief: this function allocate and initialize the device
*@return: 0 in succesfully case, -1 if we have some error
*/
int setup_device(void){

	int ret;
	
	pr_info("Syscall Throttling Manager: Starting mounting the device driver...\n");
	
	/*Allocate a device nr*/
	ret=alloc_chrdev_region(&my_device_nr,0,1,DRIVER_NAME);
	if (ret<0){
		pr_err("Syscall Throttling Manager: Failed to allocate char device region\n");
		return ret;
	}
	
	cdev_init(&my_device,&fops);
	my_device.owner=THIS_MODULE;
	
	ret=cdev_add(&my_device,my_device_nr, 1);
	if(ret<0){
		pr_err("Syscall Throttling Manager: Failed to add cdev!\n");
		goto err_cdev;
	}
	
	/*Create device class*/
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	my_class = class_create(DRIVER_CLASS);
#else
	my_class = class_create(THIS_MODULE, DRIVER_CLASS);
#endif

	if(IS_ERR(my_class)){
		pr_err("Syscall Throttling Manager: Failed to create device class\n");
		ret = PTR_ERR(my_class);
		goto err_class;
	}
	
	my_class->devnode = my_devnode;
	
	/* Create Device File*/
	if(IS_ERR(device_create(my_class,NULL,my_device_nr,NULL,DRIVER_NAME))){
		pr_err("Syscall Throttling Manager: Failed to create the device\n");
		ret = -ENODEV;
		goto err_device;
	} 
	
	pr_info("Syscall Throttling Manager: Device /dev/%s registered (Major: %d, Minor: %d)\n",
		DRIVER_NAME, MAJOR(my_device_nr), MINOR(my_device_nr));
	return 0;

err_device:
	class_destroy(my_class);
err_class:
	device_destroy(my_class,my_device_nr);
err_cdev:	
	unregister_chrdev_region(my_device_nr,1);
	return ret;
}

/*
*@brief: this function remove the allocated device
*/
void remove_device(void){

	pr_info("Syscall Throttling Manager: Start to remove the driver driver...\n");
	cdev_del(&my_device);
	device_destroy(my_class,my_device_nr);
	class_destroy(my_class);
	unregister_chrdev_region(my_device_nr,1);
	pr_info("Syscall Throttling Manager: Device /dev/%s removed succesfully\n", DRIVER_NAME);
	
}



