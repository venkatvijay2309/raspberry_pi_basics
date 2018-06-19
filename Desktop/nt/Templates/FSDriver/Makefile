# If called directly from the command line, invoke the kernel build system.
ifeq ($(KERNELRELEASE),)

	KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNEL_SOURCE) SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SOURCE) SUBDIRS=$(PWD) clean

# Otherwise KERNELRELEASE is defined; we've been invoked from the
# kernel build system and can use its language.
else

	obj-m := akp_simple.o akp.o sfs_bb.o sfs_first.o sfs_final.o
	akp-src-m := $(notdir $(wildcard ${SUBDIRS}/AKPFS/*.c))
	akp-y := $(addprefix AKPFS/,$(akp-src-m:.c=.o))
	sfs_bb-y := $(addprefix SFS/,real_sfs_bb.o)
	sfs_first-y := $(addprefix SFS/,real_sfs_minimal.o real_sfs_ops.o)
	sfs_final-y := $(addprefix SFS/,real_sfs.o real_sfs_ops.o)

endif
