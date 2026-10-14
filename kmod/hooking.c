#include <linux/kprobes.h>
#include <asm/syscall.h>    
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/ftrace.h>

#include "hooking.h"
#include "monitor.h"
#include "../include/monitor_ioctl.h"

#ifndef X86_CR0_WP
#define X86_CR0_WP 0x00010000
#endif

static bool syscall_table_found=false;
static unsigned long syscall_table_address=0;
unsigned long cr0, cr4;

/*syscall function pointer signature on x86_64*/
typedef asmlinkage long (*sys_call_ptr_t)(const struct pt_regs *);

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

/*table to store the original syscall pointers*/
static sys_call_ptr_t original_syscall_table[NR_syscalls];

#if ENABLE_FTRACE
/* Tabella di ftrace_ops, una per ciascuna syscall monitorabile */
static struct ftrace_ops ftrace_ops_table[NR_syscalls];
static bool ftrace_hooked[NR_syscalls];
#endif

/*this variables mantain information about the threads that have no finish yet they're execution, if this variable is != 0 => the module can not be removed*/
static atomic_t active_threads = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(shutdown_wq);
static bool module_unloading = false;

asmlinkage long syscall_wrapper(const struct pt_regs * regs);

static inline void write_cr0_forced(unsigned long val){
        unsigned long __force_order;
        asm volatile("mov %0, %%cr0" : "+r"(val), "+m"(__force_order));
}

static inline void write_cr4_forced(unsigned long val){
        unsigned long __force_order;
        asm volatile("mov %0, %%cr4" : "+r"(val), "+m"(__force_order));
}

static inline void unprotect_memory(void) {
	preempt_disable();
	cr0 = read_cr0();
	cr4 = native_read_cr4();
#ifdef X86_CR4_CET
	if (cr4 & X86_CR4_CET)
		write_cr4_forced(cr4 & ~X86_CR4_CET);
#endif
	write_cr0_forced(cr0 & ~X86_CR0_WP);
}

static inline void protect_memory(void) {
	write_cr0_forced(cr0);
#ifdef X86_CR4_CET
	if (cr4 & X86_CR4_CET)
		write_cr4_forced(cr4);
#endif
	preempt_enable();
}

/*
*@brief: this function lookup the base address of the syscall table via kprobes using the syscall table name. It's executed when the "syscall_table_found" variable is set to false
*/
static int find_syscall_table(void){

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)
	int ret;
	kallsyms_lookup_name_t lookup_fn;
	
	/*kprobe used to find the address of the system call table */
	struct kprobe kp={
		.symbol_name = "kallsyms_lookup_name",
	};
	
	ret=register_kprobe(&kp);
	if(ret<0){
		pr_warn("Syscall Throttling Manager: kprobe failed on kallsyms_lookup_name, err:%d\n",ret);
		return ret;
	}
	
	lookup_fn=(kallsyms_lookup_name_t)kp.addr;
	unregister_kprobe(&kp);
	
	syscall_table_address=lookup_fn(SYSCALL_TABLE_NAME);
#else
	syscall_table_address = kallsyms_lookup_name(SYSCALL_TABLE_NAME);
#endif

	if(!syscall_table_address){
		pr_warn("Syscall Throttling Manager: Impossible to find the syscall table\n");
		return -EFAULT;
	}
	syscall_table_found=true;
	pr_info("Syscall Throttling Manager: %s found at 0x%lx\n",SYSCALL_TABLE_NAME,syscall_table_address);
	
	return 0;
}

/*
*@brief: This function is used to find the address of the entry where syscall number is stored, save the original pointer in the array original_syscall_table[] and return the address of the entry (cell) of the system call table
*/
static unsigned long find_entry_address_and_save_the_dispatcher_in_the_array(unsigned int number){
	
	int ret;
	unsigned long entry_address;
	sys_call_ptr_t function_address;
	
	if(!syscall_table_found){
		ret=find_syscall_table();
		if(ret!=0){
			return 0;
		}
	}
	
	/*computate the address of the cell*/
	entry_address=syscall_table_address+(number*8);
	
	
	if(!original_syscall_table[number]){
		/*entry_address is the address of the cell which in turn contains the address to the dispatcher of the function. I access to this "entry_address" variable and save the address of the original function in the array original_syscall_table[]. Every single entry in the system call table is a function pointer and by the documentation the member of this table are the type of sys_call_ptr_t in the sense that they are functions with the same signature of the sys_call_ptr_t type defined on top of this file. Type cast "entry_address" from "unsigned long" to "sys_call_prt_t *" and access to it */
		function_address=*((sys_call_ptr_t*)entry_address);
	
		/*save this pointer*/
		original_syscall_table[number]=function_address;
	}
	return entry_address;
}	

#if ENABLE_FTRACE
static void notrace ftrace_thunk(unsigned long ip, unsigned long parent_ip,
                                 struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	//retrive the value of the cpu register saved by assembly code of ftrace_regs_caller
    	struct pt_regs *regs = ftrace_get_regs(fregs);
    	if (!regs)
        	return;

    	/* Verify that the address parent_ip (saved by ftrace_regs_caller) falls within the range of virtual addresses allocated in memory for the module code, this is useful ro prevent the recursion*/
    	if (within_module(parent_ip, THIS_MODULE))
        	return;

    	/* Devia l'Instruction Pointer verso il wrapper */
    	regs->ip = (unsigned long)syscall_wrapper;
}

#endif


/*
*@brief:this function is executed when a programm pass through the entry in the syscall table which has been replaced by this one.
*@note: everywhere is executed the the atomic_dec, we should check if we are in a state where the monitor_unloading is enable or not, cause if this is enable we must awake the rmmod thread that sleep in the shutdown_wq. If we do not do this => we can enter in a state where a thread can decrement to 0 the "active_thread" variable and the rmmod thread will not be awaken and the module will be never removed
*/
asmlinkage long syscall_wrapper(const struct pt_regs * regs){
	
	char program_name[TASK_COMM_LEN];
	int ret;
	const struct cred *cred;
	unsigned int syscall_number;
	sys_call_ptr_t original_syscall;
	
	//incremento of active thread variable, now the module wouldn't be removed while a thread pass in through this function
	atomic_inc(&active_threads);
	
	//extract the syscall number og the original syscall that the user thinks has called
	syscall_number=regs->orig_ax;
	/* save the pointer to the original dispatcher, this will be used if the user or the program are not registered and will be 
	used when the throrrling logic has successfully executed for this thread*/
	original_syscall=original_syscall_table[syscall_number];

	/*the new thread arrive here verify if module is in unloading phase => call the original dispatcher and bypass the throttle logic.*/
	if(READ_ONCE(module_unloading)){
		if(atomic_dec_and_test(&active_threads)){
		//no more active thread, awake the rmmod thread from the waitqueue
			wake_up(&shutdown_wq);
		}
		return original_syscall(regs);
	}
	
	//extract euid 
	cred= current_cred();
	uid_t euid = __kuid_val(cred->euid);
	
	//extract the program name 
	get_task_comm(program_name,current);
	
	
	//pr_info("Syscall Throttling Manager: I'm the syscall wrapper function and the %s pass throught me\n", program_name);
	
	//Verify if the user or the program name are registered (or if the monitor is enable)
	bool registered=false;
	registered=monitor_is_target(euid,program_name);
	
	if(!registered){
		if(atomic_dec_and_test(&active_threads) && READ_ONCE(module_unloading)){
			wake_up(&shutdown_wq);
		}
		//pass the control at the original function pointer
		return original_syscall(regs);
	}
	
	//call the throttle logic 
	ret=monitor_throttle(syscall_number,euid,program_name);
	
	if(atomic_dec_and_test(&active_threads) && READ_ONCE(module_unloading)){
			wake_up(&shutdown_wq);
	}
	
	return (ret == 0) ? original_syscall(regs) : ret;
}

/*
@brief: this function allow to add the wrapper to the entry of the system call table specified by number 
*/
int add_wrapper_to_this_syscall_number(unsigned int number){
	unsigned long entry_addr;
	
	/*store in entry_addr variable the address of the syscall table's entry at offset "number"*/
	entry_addr=find_entry_address_and_save_the_dispatcher_in_the_array(number);
	if(!entry_addr){
		pr_warn("Syscall Throttling Manager: failed to retrive the address of the system call number\n");
		return -EFAULT;
	}
	
#if ENABLE_FTRACE
    {
        int err;
        struct ftrace_ops *ops = &ftrace_ops_table[number];
   
        unsigned long target_func = (unsigned long)original_syscall_table[number];

        memset(ops, 0, sizeof(*ops));
        //save the callback function that will be executed when the target function will be invoked
        ops->func = ftrace_thunk;
        //bit mask that determinate the power of the callback
        ops->flags = FTRACE_OPS_FL_SAVE_REGS //save the hardware context of the cpu in a struct pt regs before call our thunk
                   | FTRACE_OPS_FL_RECURSION //protect against the recursion
                   | FTRACE_OPS_FL_IPMODIFY;//authorize our thunk callback to change the instruction pointer
	
	//tell ftrace which function to intercept (.i.e __x64_sys_mkdir)
        err = ftrace_set_filter_ip(ops, target_func, 0, 0);
        if (err) {
            pr_warn("Syscall Throttling: ftrace_set_filter_ip failed for syscall %u (err: %d)\n", number, err);
            return err;
        }
	//enable the interception mechanism. At run time the kernel modify the machine code inserting the diversion into our ftrace_thunk callback
        err = register_ftrace_function(ops);
        if (err) {
            pr_warn("Syscall Throttling: register_ftrace_function failed for syscall %u (err: %d)\n", number, err);
            //remove the address of the target function
            ftrace_set_filter_ip(ops, target_func, 1, 0);
            return err;
        }
	
        ftrace_hooked[number] = true;
        pr_info("Syscall Throttling: Hooked syscall %u via Ftrace at 0x%lx\n", number, target_func);
        return 0;
    }
#else
	unprotect_memory();
	/*assign the custom function to the selected entry*/
	*((sys_call_ptr_t*)entry_addr)=syscall_wrapper;
	protect_memory();
	return 0;
#endif
	
}

/*
*@brief: this function restore the original pointer in the target entry present in the syscall table 
*/
int restore_original_dispatcher(unsigned int target){
#if ENABLE_FTRACE
    if (ftrace_hooked[target]) {
        struct ftrace_ops *ops = &ftrace_ops_table[target];
        unsigned long target_func = (unsigned long)original_syscall_table[target];

        unregister_ftrace_function(ops);
        ftrace_set_filter_ip(ops, target_func, 1, 0);
        ftrace_hooked[target] = false;
        original_syscall_table[target] = NULL;
        pr_info("Syscall Throttling: Restored syscall %u (Ftrace unhooked)\n", target);
        return 0;
    }
    return 0;
#else
	//retrive the entry address to restore
	unsigned long entry_address=syscall_table_address+(target*8);
	
	unprotect_memory();
	/*assign the original function to the entry selected*/
	*((sys_call_ptr_t*)entry_address) = original_syscall_table[target];
	protect_memory();
	original_syscall_table[target]=NULL;
	return 0;
#endif
}

/*
*@brief: restore the original dispatcher in all the syscall table entries modified
*/
void restore_all_registered_dispatchers(void) {
    unsigned int i;
    for (i = 0; i < NR_syscalls; i++) {
        if (original_syscall_table[i]) {
            restore_original_dispatcher(i);
        }
    }
}

bool is_module_unloading(){
	return READ_ONCE(module_unloading);
}

/*
*@brief: this function is executed by the rmmod thread
*/
void wait_for_active_threads(){
	//rmmod thread wait in the shutdown_wq until the active_threads variable reach the 0 
	pr_info("Syscall Throttling: Waiting for all active wrapper threads to drain...\n");
    	wait_event_interruptible(shutdown_wq, atomic_read(&active_threads) == 0);
    	pr_info("Syscall Throttling: All threads completed execution safely.\n");
}

void notify_module_unloading(void) {
    WRITE_ONCE(module_unloading, true);
    //all write operation before this should be completed for every cpu core
    smp_mb();
}

