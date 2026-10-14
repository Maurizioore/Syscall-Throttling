#ifndef MONITOR_INTERNAL_H
#define MONITOR_INTERNAL_H

#include <linux/fs.h>

#define TIME_TO_WAIT_UNITIL_TIMER_ELAPSE 1000

extern struct file_operations fops;

void monitor_cleanup_lists(void);
void setup_timer(void);
void cleanup_timer(void);

bool monitor_is_target(unsigned int uid_to_check, char* program_name_to_check);
int monitor_throttle(unsigned int syscall_number, unsigned int euid, char* program_name ); 

#endif
