#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <asm/asm-offsets.h> 
#include "../include/monitor_ioctl.h"
#include "monitor.h"


/*this file implements the monitor logic*/

/*declare and initialize the linked list "list_head" for the registration of the variable*/

LIST_HEAD(syscall_number);
LIST_HEAD(program_name);
LIST_HEAD(uid);

/*this is a list syscall number registered*/
static struct linked_list_syscall_number {
	struct list_head head_syscall_list;
	int syscall_number;

};

/*this is a list syscall number registered*/
static struct linked_list_uid {
	struct list_head head_uid_list;
	int uid;
};

/*this is a list syscall number registered*/
static struct linked_list_program_name {
	struct list_head head_program_name_list;
	char* program_name;
};

static int max_invocation; //max number of invocation for a registered syscall number in 1 second
static int monitor_enabled=0; //0: monitor off, 1: monitor on
static int current_invocation; //total number of the invocations in the current second

/*counter to know how much entry we have registered in the diffent linked list structure*/
static int total_number_current_syscall_registerd=0;
static int total_number_uid_registerd=0;
static int total_number_program_registerd=0;

static DEFINE_SPINLOCK(my_list_lock);
/*
* @brief: this function is called when the device file is opened
*/
static int driver_open(struct inode *device_file, struct file *filp){
	printk("open the device driver..\n");
	return 0;
}

/*
* @brief: this function is called when the device file is closed
*/
static int driver_close(struct inode *device_file, struct file *filp){
	printk("close the device driver...\n");
	return 0;
}


/*
*@brief: this function is called from the user space to retrive some statistics about this LKM
*@param: user_buffer 
*/
static ssize_t my_read(struct file *filp, char __user* user_buffer, size_t len, loff_t *off){
	return 0;
}

/*
*@brief: this function implement the ioctl logic
*/

long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	
	switch(cmd){

		/*change the max number of invocation for a registered syscall  in 1 second*/
		case IOCTL_SET_MAX:
			
			break;
		
		/* register a new syscall number*/
		case IOCTL_ADD_SYSCALL:
			printk("starting to adding a new syscall number in the linked list...\n");
			
			int number,not_copied,flags;
			
			not_copied=copy_from_user(&number,(int *)arg,sizeof(int));
			if(not_copied){
				pr_warn("Warning: copy_from_user not copied all the byte\n");
			}
			if (number >= NR_syscalls){
				pr_warn("you are out of the valid syscall number, pass a valid number according x86-64 conventions\n");
				return -EINVAL;
			}
			
			//create the new element to add in the linked list structure
			struct linked_list_syscall_number *new_number=kzalloc(sizeof(struct linked_list_syscall_number), GFP_KERNEL);
			if(!new_number){
				printk("error, not enough memory space, allocation failed\n");
				return -ENOMEM;
			}
			new_number->syscall_number=number;
			
			spin_lock_irqsave(&my_list_lock,flags);
	
			printk("spinlock acquired\n");
			list_add_tail(&new_number->head_syscall_list,&syscall_number);
			
			//update the counter 
			total_number_current_syscall_registerd++;
			
			spin_unlock_irqrestore(&my_list_lock,flags);
			printk("succesfully added a new element in the syscall number list\n");
			break;
		
		/* register a new user-id*/	
		case IOCTL_ADD_UID:
			
			break;
		
		/* register a new programm name*/		
		case IOCTL_ADD_PROGRAM_NAME:
			
			break;
		
		/* remove a syscall*/	
		case IOCTL_REMOVE_SYSCALL:
			
			break;
			
		/* remove a uid*/	
		case IOCTL_REMOVE_UID:
			
			break;
		
		/* remove a programm name*/	
		case IOCTL_REMOVE_PROGRAM_NAME:
			
			break;
			
		/* enable the monitor*/
		case IOCTL_ENABLE_MONITOR:
			
			break;
		
		/* disable the monitor*/	
		case IOCTL_DISABLE_MONITOR:
			
			break;
		
		/* send the stats to the user*/	
		case GET_STATS:
			
			break;
			
		default:
			printk("use a correct command\n");
			return -EINVAL;
	}
	return 0;
}

struct file_operations fops={
	.owner= THIS_MODULE,
	.open=driver_open,
	.release=driver_close,
	.read=my_read,
	.unlocked_ioctl=my_ioctl
};



