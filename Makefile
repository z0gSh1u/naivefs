# naivefs Makefile
# by z0gSh1u

ifneq (${KERNELRELEASE},)
obj-m += naivefs.o
naivefs-objs := bitmap.o inode.o file.o dir.o 
else
KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default: # naivefs itself
	$(MAKE) -C ${KERNEL_DIR} SUBDIRS=$(PWD) modules

mkfs: # mkfs tool
	gcc mkfs.naive.c -o mkfs.naive

clean: # clean both
	rm -rf *.ko *.o *.mod.o *.mod.c *.symvers .*.cmd .tmp_versions mkfs.naive

endif