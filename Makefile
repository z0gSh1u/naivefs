# naivefs Makefile
# by z0gSh1u

obj-m := naivefs.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build

default: # naivefs itself
	$(MAKE) -C ${KERNEL_DIR} M=$(PWD) modules

mkfs: # mkfs tool
	gcc mkfs.naive.c -o mkfs.naive

clean: # clean both
	rm -rf *.ko *.o *.mod.o *.mod.c *.symvers .*.cmd .tmp_versions mkfs.naive
