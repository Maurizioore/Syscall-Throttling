#ifndef HOOKING_H
#define HOOKING_H

#define SYSCALL_TABLE_NAME "sys_call_table"
#include <linux/version.h>

int add_wrapper_to_this_syscall_number(unsigned int number); 
int restore_original_dispatcher(unsigned int target); 
void restore_all_registered_dispatchers(void);

void wait_for_active_threads(void);
void notify_module_unloading(void);
bool is_module_unloading(void);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#define ENABLE_FTRACE 1
#else
#define ENABLE_FTRACE 0
#endif


#endif
