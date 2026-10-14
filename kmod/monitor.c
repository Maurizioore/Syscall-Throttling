#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/cred.h>
#include <linux/sched.h>  
#include <linux/uidgid.h> 
#include <linux/rcupdate.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/cache.h>
#include <linux/ktime.h>

#include <asm/syscall.h> 
#include <asm/unistd.h> 

#include "../include/monitor_ioctl.h"
#include "monitor.h"
#include "hooking.h"
#include "setup_device.h"


#ifndef ALLOW_BACK_TO_BACK
#define CHECK_BACK_TO_BACK 1
#else
#define CHECK_BACK_TO_BACK 0
#endif

/*this file implements the monitor logic*/

/*declare and initialize the linked list "list_head" for the registration of the variable*/

LIST_HEAD(syscall_number_list);
LIST_HEAD(uid_list);
LIST_HEAD(program_name_list);


/*this is a list for the registered syscall number */
struct linked_list_syscall_number {
	struct rcu_head rcu;
	struct list_head list;
	unsigned int syscall_number;

};

/*this is a list for the registered uid */
struct linked_list_uid {
	struct rcu_head rcu;
	struct list_head list;
	unsigned int uid;
};

/*this is a list for the registered program name */
struct linked_list_program_name {
	struct rcu_head rcu;
	struct list_head list;
	char program[PROGRAM_NAME_DIMENSION] ;
};

static unsigned int max_invocation __read_mostly =100; //max number of invocation for a registered syscall number in 1 second
static bool monitor_enabled __read_mostly =false; 
static atomic_t current_invocation ____cacheline_aligned =ATOMIC_INIT(0); //total number of the invocations in the current second

static int total_number_uid_registered=0;
static int total_number_program_registered=0;

/*define spinlock, one for each linked list, i use cacheline_aligned to separate locks to different cache line*/
static spinlock_t my_syscall_lock ____cacheline_aligned = __SPIN_LOCK_UNLOCKED(my_syscall_lock);
static spinlock_t my_uid_lock ____cacheline_aligned = __SPIN_LOCK_UNLOCKED(my_uid_lock);
static spinlock_t my_prog_lock ____cacheline_aligned = __SPIN_LOCK_UNLOCKED(my_prog_lock);
static spinlock_t my_stats_lock ____cacheline_aligned = __SPIN_LOCK_UNLOCKED(my_stats_lock);
static struct mutex my_monitor_status_mutex ____cacheline_aligned = __MUTEX_INITIALIZER(my_monitor_status_mutex);

static DECLARE_WAIT_QUEUE_HEAD(wq1);

static struct timer_list timer;

#if CHECK_BACK_TO_BACK
static atomic_t sporadic_server_refill=ATOMIC_INIT(0);
#endif

//variable for the stats 
static struct monitor_stats global_stats;
static atomic_t currently_blocked=ATOMIC_INIT(0);

/*function used for setting the max*/
static int set_max(unsigned long arg){

	unsigned int new_max;
		
	pr_info("Syscall Throttling Manager: Starting to set new max invocation \n");
	if(copy_from_user(&new_max,(unsigned int __user*)arg,sizeof(unsigned int))!=0){
		pr_warn("Syscall Throttling Manager: Invalid value passed\n");
		return -EFAULT;
	}
	if(new_max ==0){
		pr_warn("Syscall Throttling Manager: Max invocation must be >0\n");
		return -EINVAL;
	}
	//update the value 
	WRITE_ONCE(max_invocation, new_max);
	pr_info("Syscall Throttling Manager: New max invocation rate set to: %d\n",max_invocation);
	return 0;
}

/*function used to add a new syscall number*/
static int add_syscall_number(unsigned long arg){

	unsigned long flags;
	struct linked_list_syscall_number *new_node;
	unsigned int target_syscall;
	//cursor for the check duplicates
	struct linked_list_syscall_number *cursor;

	pr_info("Syscall Throttling Manager: Starting to add new syscall number in the linked list...\n");
		
	if(copy_from_user(&target_syscall,(unsigned int __user*)arg,sizeof(unsigned int))!=0){
		pr_warn("Syscall Throttling Manager: Failed to retrive the syscall number passed from the user, please pass a correct value\n");
		return -EFAULT;
	}
	
	if (target_syscall >= NR_syscalls){
		pr_warn("Syscall Throttling Manager: You are out of the valid syscall number, pass a valid number according x86-64 conventions\n");
		return -EINVAL;
	}	
	//create the new element to add in the linked list structure
	new_node=kzalloc(sizeof(struct linked_list_syscall_number), GFP_KERNEL);
	if(!new_node){
		pr_warn("Syscall Throttling Manager: Error, not enough memory space, allocation failed\n");
		return -ENOMEM;
	}
	
	new_node->syscall_number=target_syscall;
			
	
	spin_lock_irqsave(&my_syscall_lock,flags);
	
	//check duplicates
	list_for_each_entry(cursor,&syscall_number_list,list){
		if(cursor->syscall_number==target_syscall){
			spin_unlock_irqrestore(&my_syscall_lock,flags);
			kfree(new_node);
			pr_warn("Syscall Throttling Manager: %u alredy registered\n",target_syscall);
			return -EEXIST;
		}
	}

	list_add_tail_rcu(&new_node->list,&syscall_number_list);
			
	spin_unlock_irqrestore(&my_syscall_lock,flags);
	pr_info("Syscall Throttling Manager: Succesfully added a new element in the syscall number list\n");
	
	/*start the wrapping logic*/
	if(add_wrapper_to_this_syscall_number(target_syscall)!=0){
		pr_warn("Syscall Throttling Manager: removing the system call number %d from the linked list\n",target_syscall);
		
		spin_lock_irqsave(&my_syscall_lock, flags);
		list_del_rcu(&new_node->list);
		
		spin_unlock_irqrestore(&my_syscall_lock, flags);
		
		kfree_rcu(new_node,rcu);
		
		return -EFAULT;
	}
	//The entry in the syscall table at the offset "target_syscall is successfully linked to the wrapper
	return 0;
}

/*this function add a new user id*/
static int add_uid(unsigned long arg){

	struct linked_list_uid *new_node;
	unsigned long flags;
	unsigned int new_uid;
	//cursor for the duplicates 
	struct linked_list_uid *cursor;

	pr_info("Syscall Throttling Manager: Starting to add a new user id in the linked list...\n");
		
	if(copy_from_user(&new_uid,(unsigned int __user*)arg,sizeof(unsigned int))){
		pr_warn("Syscall Throttling Manager: pass a valid user id number\n");
		return -EFAULT;
	}
	if(new_uid==(unsigned int)-1){
		pr_warn("Syscall Throttling Manager:: UID %d out of logical bounds\n",new_uid);
		return -EINVAL;
	}
			
	//allocate the new element
	new_node=kzalloc(sizeof(struct linked_list_uid),GFP_KERNEL);
	if(new_node==NULL){
		pr_warn("Syscall Throttling Manager: Creation of the new linked list node for the uid failed\n");
		return -ENOMEM;
	}
	//new linked list node successfully allocated
	new_node->uid=new_uid;
		
	//acquire the lock 
	spin_lock_irqsave(&my_uid_lock,flags);
	
	//check duplicates
	list_for_each_entry(cursor,&uid_list,list){
		if(cursor->uid==new_uid){
			spin_unlock_irqrestore(&my_uid_lock,flags);
			kfree(new_node);
			pr_warn("Syscall Throttling Manager: UID %u alredy registered\n",new_uid);
			return -EEXIST;
		}
	}

	//add the element in the uid linked list 
	list_add_tail_rcu(&new_node->list,&uid_list);
	//update the total number of the element actually present in the uid linked list
	total_number_uid_registered++;
			
	spin_unlock_irqrestore(&my_uid_lock,flags);
	pr_info("Syscall Throttling Manager: User-id %u registered\n",new_uid);
			
	return 0;
}

/*this function add a program name in the linked list*/
static int register_program_name(unsigned long arg){

	char buffer[PROGRAM_NAME_DIMENSION];
	struct linked_list_program_name *new_node;
	int copied;
	unsigned long flags;
	long res;
	//for the check duplicates
	struct linked_list_program_name *cursor;
	
	memset(buffer, 0, sizeof(buffer));
	res=strncpy_from_user(buffer,(char __user*)arg,PROGRAM_NAME_DIMENSION);

	if(res<0){
		pr_warn("Syscall Throttling Manager: reading from the user space failed\n");
		return -EFAULT;
	}
	
	if(res>=sizeof(buffer)){
		pr_warn("Syscall Throttling Manager: Program name exceeds limit\n");
		return -EINVAL;
	}
	
	if(res==0){
		pr_warn("Syscall Throttling Manager: the empty string is not valid\n");
		return -EINVAL;
	}
	/*create the node and insert the element in the linked list of the program name*/
	new_node=kzalloc(sizeof(struct linked_list_program_name),GFP_KERNEL);
	if(new_node==NULL){
		pr_warn("Syscall Throttling Manager: Failed to allocate the new entry for the linked list that contain the program name\n");
		return-ENOMEM;
	}
	/*succesfully allocated the new node, popolate it*/
			
	copied=strscpy(new_node->program,buffer,sizeof(new_node->program));
			
	if(copied<0){
		pr_warn("Syscall Throttling Manager: the string passed from the user is not copied to the kerbel buffer, error code %d\n",copied);
		kfree(new_node);
		return -ENOMEM;
	}
			
	/*adding the created node in the lonked list*/
	spin_lock_irqsave(&my_prog_lock,flags);
	
	/*check duplicates */
	list_for_each_entry(cursor,&program_name_list,list){
		if(strncmp(cursor->program,buffer,PROGRAM_NAME_DIMENSION)==0){
			spin_unlock_irqrestore(&my_prog_lock,flags);
			kfree(new_node);
			pr_warn("Syscall Throttling Manager: the program %s is alredy present in the list\n",buffer);
			return -EEXIST;
		}
	}
	list_add_tail_rcu(&new_node->list,&program_name_list);
	total_number_program_registered++;
			
	spin_unlock_irqrestore(&my_prog_lock,flags);
			
	pr_info("Syscall Throttling Manager: the program %s is successfully added into the linked list\n",buffer);
	return 0;
					
}

/*this function allow to remove a syscall number from the linked list*/
static int remove_syscall_number(unsigned long arg){

	/*user pass a int from the input, i store this input in a variable*/
	unsigned int target;
	bool found = false;
	unsigned long flags;
	struct linked_list_syscall_number *next,*cursor,*to_delete=NULL;
		
	pr_info("Syscall Throttling Manager: Start removing the syscall number passed from the user from the linked list...\n");
	
	/*copy the user input in "target"*/
	if(copy_from_user(&target,(unsigned int __user*)arg,sizeof(unsigned int))){
		pr_warn("Syscall Throttling Manager: failed to copy the syscall number passed from the input, please verify the passed parameter is correct \n");
		return -EFAULT;	
	}
	/*copy successfully completed, verify that the number passed is in the entry*/
	spin_lock_irqsave(&my_syscall_lock,flags);
	
	list_for_each_entry_safe(cursor, next, &syscall_number_list, list){
		if(cursor->syscall_number==target){
			to_delete=cursor;
			list_del_rcu(&cursor->list);
			found=true;
			break;
		}
	}
	spin_unlock_irqrestore(&my_syscall_lock,flags);
	if(!found){
		pr_warn("Syscall Throttling Manager: Syscall %u not found in registration list\n",target);
		return -ENOENT;
	}
	restore_original_dispatcher(target);
	
	kfree_rcu(to_delete,rcu);
	pr_info("Syscall Throttling Manager: Successfully remove the syscall number %u from the linked list\n",target);
	
	return 0;
}

/*this function allow to remove a uid from the linked list */
static int remove_user_id(unsigned long arg){
	
	unsigned int target;
	bool found=false;
	unsigned long flags;
	struct linked_list_uid *cursor,*next,*to_delete=NULL;
			
	if(copy_from_user(&target,(unsigned int __user*)arg, sizeof(unsigned int))!=0){
		pr_warn("Syscall Throttling Manager: Failed to copy the user id passed\n");
		return -EFAULT;
	}
	if(target==(unsigned int)-1){
		pr_warn("Syscall Throttling Manager: UID %d out of logical bounds\n",target);
		return -EINVAL;
	}
	//now iterate cross the list
	spin_lock_irqsave(&my_uid_lock, flags);
			
	list_for_each_entry_safe(cursor,next,&uid_list,list){
		if(cursor->uid==target){
			to_delete=cursor;
			list_del_rcu(&cursor->list);
			found=true;
			total_number_uid_registered--;
			break;
		}
			
	}
	spin_unlock_irqrestore(&my_uid_lock, flags);

	if(!found){
		pr_warn("Syscall Throttling Manager: UID %d not found in the linked list \n",target);
		return -ENOENT;
	}
	kfree_rcu(to_delete,rcu);
	pr_info("Syscall Throttling Manager: Successfully remove the uid %u from the linked list\n",target);
	return 0;
}

/*thsi function alllow to remove a program identified by his name from the linked list*/
static int remove_program_name(unsigned long arg){
	
	char buffer[PROGRAM_NAME_DIMENSION];
	long res;
	unsigned long flags;
	bool found=false;
	struct linked_list_program_name *cursor,*next,*to_delete=NULL;
			
	res=strncpy_from_user(buffer,(char __user*)arg,PROGRAM_NAME_DIMENSION);
	if(res<0){
		pr_warn("Syscall Throttling Manager: reading from the user space failed\n");
		return -EFAULT;
	}
	/*number byte copied is greater than the buffer dimension*/
	if(res>=sizeof(buffer)){
		pr_warn("Syscall Throttling Manager: pass a valid programm name\n");
		return -EINVAL;
	}

	if(res == 0){
		pr_warn("Syscall Throttling Manager: the empty string is not valid\n");
		return -EINVAL;
	}
			
	/*now the element passed is a valid one, iterate throught the list*/
	spin_lock_irqsave(&my_prog_lock,flags);
			
	list_for_each_entry_safe(cursor,next,&program_name_list,list){
		if(strncmp(cursor->program,buffer,sizeof(cursor->program))==0){
			//string are equal
			to_delete=cursor;
			found=true;
			list_del_rcu(&cursor->list);
			total_number_program_registered--;
			break;
		}
	}	
			
	spin_unlock_irqrestore(&my_prog_lock,flags);
	if(!found){
		pr_warn("Syscall Throttling Manager: program %s not found in the linked list \n",buffer);
		return -ENOENT;
	}
	kfree_rcu(to_delete,rcu);
	pr_info("Syscall Throttling Manager: Successfully deleted the program %s from the linked list\n",buffer);
	return 0;
}

/*
*@brief: this function enable the monitor 
*/
static int enable_monitor(void){

	mutex_lock(&my_monitor_status_mutex);
	if(!monitor_enabled){
		monitor_enabled=true;
		mod_timer(&timer,jiffies + msecs_to_jiffies(TIME_TO_WAIT_UNITIL_TIMER_ELAPSE));
		mutex_unlock(&my_monitor_status_mutex);
		pr_info("Syscall Throttling Manager: Successfully enabled the monitor\n");
		return 0;
	}
	mutex_unlock(&my_monitor_status_mutex);
	/*the monitor is alredy enable*/
	return 0;
}

/* 
*@brief: this function disable the monitor 
*/
static int disable_monitor(void){

	mutex_lock(&my_monitor_status_mutex);
	if(monitor_enabled){
		monitor_enabled=false;
		cleanup_timer();
		pr_info("Syscall Throttling Manager: Successfully disabled the monitor\n");
	}
	mutex_unlock(&my_monitor_status_mutex);
	return 0;
}

static int get_stats(struct monitor_stats* arg){
	
	struct monitor_stats snapshot;
	unsigned long flags;
	
	memset(&snapshot, 0, sizeof(snapshot));
	
	spin_lock_irqsave(&my_stats_lock, flags);
    	snapshot = global_stats;
    	spin_unlock_irqrestore(&my_stats_lock, flags);
    	if(copy_to_user(arg,&snapshot,sizeof(struct monitor_stats))!=0){
    		return -EFAULT;
    	}
	return 0;
}


/*
* @brief: this function is called when the device file is opened
*/
static int driver_open(struct inode *device_file, struct file *filp){
	return 0;
}

/*
* @brief: this function is called when the device file is closed
*/
static int driver_close(struct inode *device_file, struct file *filp){
	return 0;
}


/*
*@brief: this function is called from the user space to retrive some statistics about this LKM
*@param: user_buffer 
*/
static ssize_t my_read(struct file *filp, char __user* user_buffer, size_t len, loff_t *off){
    
	char *kbuf;
	int pos = 0;
	ssize_t ret = 0;
    
	struct linked_list_syscall_number *syscall_cursor;
	struct linked_list_uid *uid_cursor;
	struct linked_list_program_name *program_cursor;
	
	// if off is 0 => we are in End of File EOF
	if (*off > 0){
		return 0;
   	}
	//allocate kernel buffer with flag GFP_KERNEL cause the read is executed in process context and we can go to sleep 
	kbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!kbuf){
		return -ENOMEM;
	}
	//start to write from kbuf+pos and not pass the "PAGE_SIZE - pos" dimension.
	pos += scnprintf(kbuf + pos, PAGE_SIZE - pos, "=== REGISTERED SYSCALLS ===\n");
    	rcu_read_lock();
    	list_for_each_entry_rcu(syscall_cursor, &syscall_number_list, list) {
        	pos += scnprintf(kbuf + pos, PAGE_SIZE - pos, "Syscall: %u\n", syscall_cursor->syscall_number);
    	}

    	pos += scnprintf(kbuf + pos, PAGE_SIZE - pos, "\n=== REGISTERED UIDS ===\n");
    	list_for_each_entry_rcu(uid_cursor, &uid_list, list) {
        	pos += scnprintf(kbuf + pos, PAGE_SIZE - pos, "UID: %u\n", uid_cursor->uid);
    	}

    	pos += scnprintf(kbuf + pos, PAGE_SIZE - pos, "\n=== REGISTERED PROGRAMS ===\n");
    	list_for_each_entry_rcu(program_cursor, &program_name_list, list) {
        	pos += scnprintf(kbuf + pos, PAGE_SIZE - pos, "Program: %s\n", program_cursor->program);
    	}
    	rcu_read_unlock();
	
	//verify that the bytes written are less than the bytes requested by the user
    	if (pos > len){
        	pos = len;
	}
	
    	if(copy_to_user(user_buffer, kbuf, pos)) {
    		//the user passed an invalid memory address
        	ret = -EFAULT;
    	}else{
    		//update the current reading position of the file
        	*off += pos;
        	ret = pos;
    	}
    	kfree(kbuf);
    	return ret;
}

/*
*@brief: this function implement the ioctl logic
*/

static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	
	/*verify if the commando passed is open to all the user */
	
	switch(cmd){
		case IOCTL_GET_STATS:
			//this can be called by normal user 
			return get_stats((struct monitor_stats *)arg);
	}
	
	/*verify that the user has the permissions to execute the other commands*/
	if (!uid_eq(current_euid(), GLOBAL_ROOT_UID)) {
		pr_warn("Syscall Throttling Manager: Unauthorized access attempt, euid is not root\n");
		return -EPERM;
	}
	
	switch(cmd){

		/*change the max number of invocation for a registered syscall  in 1 second*/
		case IOCTL_SET_MAX:
			return set_max(arg);
		
		
		/* register a new syscall number*/
		case IOCTL_ADD_SYSCALL:
			return add_syscall_number(arg);
				
		
		/* register a new user-id*/	
		case IOCTL_ADD_UID:
			return add_uid(arg);
		
		
		/* register a new programm name*/		
		case IOCTL_ADD_PROGRAM_NAME:
			return register_program_name(arg);
		
		/* remove a syscall number from the linked list*/	
		case IOCTL_REMOVE_SYSCALL:
			return remove_syscall_number(arg);	
		
		/* remove a uid*/	
		case IOCTL_REMOVE_UID:
			return remove_user_id(arg);
		
		
		/* remove a programm name*/	
		case IOCTL_REMOVE_PROGRAM_NAME:
			return remove_program_name(arg);
		
		/* enable the monitor*/
		case IOCTL_ENABLE_MONITOR:
			return enable_monitor();
		
		/* disable the monitor*/	
		case IOCTL_DISABLE_MONITOR:
			return disable_monitor();
			
		default:
			pr_warn("Syscall Throttling Manager: Unknown ioctl command, pleae use a correct command\n");
			return -ENOTTY;

	}
}

/*
*@brief:this function is used to free all the entry in the 3 linked lists
*/
void monitor_cleanup_lists(void){
	
	struct linked_list_syscall_number *number_cursor,*number_next;
	struct linked_list_uid *uid_cursor, *uid_next;
	struct linked_list_program_name *name_cursor, *name_next;
	unsigned long flags;
	
	pr_info("Syscall Throttling Manager: Start to clean up the three linked lists...\n");
	
	spin_lock_irqsave(&my_syscall_lock, flags);
	list_for_each_entry_safe(number_cursor,number_next,&syscall_number_list,list){
		list_del_rcu(&number_cursor->list);
		kfree_rcu(number_cursor, rcu);
	}
	
	spin_unlock_irqrestore(&my_syscall_lock,flags);
	
	spin_lock_irqsave(&my_uid_lock, flags);
	list_for_each_entry_safe(uid_cursor,uid_next,&uid_list,list){
		list_del_rcu(&uid_cursor->list);
		kfree_rcu(uid_cursor, rcu);
	}
	total_number_uid_registered=0;
	spin_unlock_irqrestore(&my_uid_lock,flags);
	
	spin_lock_irqsave(&my_prog_lock, flags);
	list_for_each_entry_safe(name_cursor,name_next,&program_name_list,list){
		list_del_rcu(&name_cursor->list);
		kfree_rcu(name_cursor,rcu);
	}
	total_number_program_registered=0;
	spin_unlock_irqrestore(&my_prog_lock,flags);
	
	//waiting all the kfree_rcu are successfully executed
	rcu_barrier();
	
	pr_info("Syscall Throttling Manager: All registered lists cleaned up successfully...\n");
	
	
}

struct file_operations fops={
	.owner= THIS_MODULE,
	.open=driver_open,
	.release=driver_close,
	.read=my_read,
	.unlocked_ioctl=my_ioctl
};

/*
*@brief:this function permit to verify if the user or programm that pass through the syscall table entry (wrapped) is registered in the linked list and check the state of the monitor
*@retur: true if the user or the program are registered, false if monitor is off or the user or the program name are not registered
*/
bool monitor_is_target(unsigned int uid_to_check, char * program_name_to_check){
	//check if the monitor is enable 
	bool user_found=false;
	bool program_found=false;
	struct linked_list_uid *uid_cursor;
	struct linked_list_program_name *pn_cursor;
	
	
	if(!monitor_enabled){
		pr_info("Syscall Throttling Manager: the monitor is not enable, do whatever you want\n");
		return false;
	}
	if(total_number_uid_registered == 0 && total_number_program_registered ==0){
		pr_info("Syscall Throttling Manager: no uid or program mane registered in the linked list\n");
		return false;
	}
	//check the syscall number list 
	rcu_read_lock();
	list_for_each_entry_rcu(uid_cursor,&uid_list,list){
		if(uid_cursor->uid==uid_to_check){
			user_found=true;
			break;
		}
	}
	
	if(!user_found){
		list_for_each_entry_rcu(pn_cursor,&program_name_list,list){
			if(strncmp(pn_cursor->program,program_name_to_check,PROGRAM_NAME_DIMENSION)==0){
				program_found=true;
				break;
			}
		}
	}
	rcu_read_unlock();
	return(user_found||program_found);
}

/*
*@brief: this function implements the throttle logic 
*/
int monitor_throttle(unsigned int syscall_number, unsigned int euid, char* program_name){

	int ret;
	ktime_t start, end,delta;
	unsigned long flags;
	bool has_waited=false;
	u64 delta_ns;
	
	while(1){
		if(is_module_unloading()){
			return 0;
		}
		//try to aquire the quota to pass 
		if (atomic_add_unless(&current_invocation, 1, READ_ONCE(max_invocation))) {
			//if the thread come from a wq, update the stats
			if (has_waited) {
			
				end = ktime_get();
				delta = ktime_sub(end, start);
				delta_ns = (u64)ktime_to_ns(delta);
				
				spin_lock_irqsave(&my_stats_lock, flags);
				if (delta_ns > READ_ONCE(global_stats.peak_delay_ns)) {
					global_stats.peak_delay_ns = delta_ns;
					global_stats.peak_delay_uid = euid;
					strscpy(global_stats.peak_delay_prog, program_name, PROGRAM_NAME_DIMENSION);
					 // pr_info("Syscall Throttling Manager: new max timer waited in the wait queue: %lld by user d ",global_stats.peak_delay_ns, global_stats.peak_delay_uid);
				}
				spin_unlock_irqrestore(&my_stats_lock, flags);
			}
			return 0;
		}

#if CHECK_BACK_TO_BACK

		//if thread arrive here => he go to the wq and if this is the first time in the current second he reach the wait queueue, apply the sporadic server logic to refill the timer to TIME second from now and avoid the back to back event
		if (atomic_cmpxchg(&sporadic_server_refill, 0, 1) == 0) {
    			mod_timer(&timer, jiffies + msecs_to_jiffies(TIME_TO_WAIT_UNITIL_TIMER_ELAPSE));
    			//pr_info(" Syscall Throttling Manager: set sporadic to 1, it's the first time the queue is fill, start timer from now \n");
    		}
    		
#endif
		//signal that this thread is in queue 
		//before sendig the thread in th wq, exstract the enter time
		if (!has_waited) {
			start = ktime_get();
			has_waited = true;
		}

		//sending the thread in the wq
		//pr_info("Syscall Throttling Manager: Send the thread in the wait queue\n");
		atomic_inc(&currently_blocked);
		ret = wait_event_interruptible(wq1,
			(atomic_read(&current_invocation) < READ_ONCE(max_invocation)) || !READ_ONCE(monitor_enabled) || is_module_unloading());

		// decrement the number of currently blocked threads
		atomic_dec(&currently_blocked);
		//pr_info("Syscall Throttling Manager: Awaken from wait queue!\n");
		
		if (is_module_unloading()){
           		return 0;
           	}
		
		if (ret == -ERESTARTSYS) {
			return ret;
		}
	}
}

/*
*@brief: this function will be executed every second
*/

static void timer_callback(struct timer_list *t){
	
	unsigned long flags;
	int curr_block=atomic_read(&currently_blocked);
	int curr_inv=atomic_read(&current_invocation);
	//update the global stats element
	spin_lock_irqsave(&my_stats_lock, flags);
   	
   	global_stats.total_seconds_sampled++;
    	global_stats.total_blocked_samples += (__u64)curr_inv;
    	if(curr_block>global_stats.peak_blocked_threads){
    		global_stats.peak_blocked_threads=curr_block;
    	}
    	global_stats.average_thread_blocked= (__u64) (global_stats.total_blocked_samples*1000)/global_stats.total_seconds_sampled;
    	
    	//pr_info("Syscall Throttling Manager: I'm the timer function and  passed %lld second and max threads blocked until now are: %lld\n", global_stats.total_seconds_sampled,global_stats.peak_blocked_threads);
    	
    	spin_unlock_irqrestore(&my_stats_lock, flags);
   
    	
	//reset to 0 the current invocation for the next second
	atomic_set(&current_invocation,0);
	
	//if the waitqueue is non empty
	if(waitqueue_active(&wq1)){
		if (READ_ONCE(monitor_enabled)) {
			//if monitor is on, wake up "max_invocation" thread and restart from 1 second 
        		wake_up_nr(&wq1, max_invocation);
    
    		}else{
			//if monitor is off, awake all the threads and not restart every second 
			wake_up_all(&wq1);
   		}	
	}
	
#if CHECK_BACK_TO_BACK
	//if the wait queue is empty check the value of sporadic server refill 
	else if((int) atomic_read(&sporadic_server_refill) == 1){
		pr_info("Syscall Throttling Manager: the wq is empty, set sporadic refill to 0\n");
		//no thread present in the wait queue, we come from a state where in the past we have filled the wait queue one or more time
		atomic_set(&sporadic_server_refill,0);
	}	
#endif	

	if (READ_ONCE(monitor_enabled) && !is_module_unloading() ){
		//Regardless of whether the queue is full or empty, or whether the back-to-back operation is executed or skipped, the timer must reset
		mod_timer(&timer,jiffies + msecs_to_jiffies(TIME_TO_WAIT_UNITIL_TIMER_ELAPSE));
	}
}

/*
*@brief:this function is called from the MonitorModuleInit tu setup the timer 
*/
void setup_timer(void){
	//pr_info("Syscall Throttling Manager: i'm the setup timer function\n");
	/*initialize the timer, when the timer goes off the function will be executed */
	timer_setup(&timer,timer_callback,0);
}

void cleanup_timer(void){
	del_timer_sync(&timer);
	wake_up_all(&wq1);
	//pr_info("Syscall Throttling Manager: i'm the cleanup timer function\n");
}
