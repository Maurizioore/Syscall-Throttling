#ifndef SETUP_DEVICE_H
#define SETUP_DEVICE_H

#define DRIVER_NAME "syscall_throttling"
#define DRIVER_CLASS "syscall_throttling_class"

int setup_device(void);
void remove_device(void);

#endif
