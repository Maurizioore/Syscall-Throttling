# Name of the resulting kernel module
MODULE_NAME := monitor_module
# Path to the running kernel's build directory
KDIR ?= /lib/modules/$(shell uname -r)/build
# Current working directory
PWD := $(shell pwd)
USER_DIR := user

# Let's tell Kbuild which object files (compiled from .c files) make up the module.
$(MODULE_NAME)-y := kmod/main.o kmod/setup_device.o kmod/monitor.o kmod/hooking.o

# Define our loadable module (final .ko)
obj-m := $(MODULE_NAME).o

# Add our "include" directory to the paths where the compiler looks for .h files
ccflags-y := -I$(PWD)/include

# Back to back
ifeq ($(ALLOW_BACK_TO_BACK), 1)
    ccflags-y += -DALLOW_BACK_TO_BACK
    $(info [CONFIG] Back-to-Back : Sporadic Server Refill Logic DISABLED)
else
    $(info [CONFIG] Back-to-Back : Sporadic Server Refill Logic ENABLED)
endif

.PHONY: all module user clean load unload

# Compiles both the kernel module and the user binaries
all: module user

module:
	@echo "=> Compiling Kernel module..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules

user:
	@echo "=> Compiling user space programs..."
	gcc $(USER_DIR)/test_all_module.c -o $(USER_DIR)/1_test_all_module
	gcc $(USER_DIR)/register_for_module_use.c -o $(USER_DIR)/2_register_for_module_use
	gcc $(USER_DIR)/test_get_stats_and_read.c -o $(USER_DIR)/3_test_get_stats_and_read
	gcc $(USER_DIR)/test_multithread.c -o $(USER_DIR)/4_launch_thread
	gcc $(USER_DIR)/test_multithread.c -o $(USER_DIR)/5_launch_this_as_non_registered_user

clean:
	@echo "=> Cleaning up build files..."
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(USER_DIR)/1_test_all_module $(USER_DIR)/2_register_for_module_use $(USER_DIR)/3_test_get_stats_and_read $(USER_DIR)/4_launch_thread $(USER_DIR)/5_launch_this_as_non_registered_user

load:
	@echo "=> Loading the module into the kernel..."
	sudo insmod $(MODULE_NAME).ko

unload:
	@echo "=> Removal of the module from the kernel..."
	sudo rmmod $(MODULE_NAME)
