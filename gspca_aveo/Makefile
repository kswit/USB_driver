EXTRA_CFLAGS += -Wall -Wno-unused-variable
ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := gspca_aveo.o

else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD  LLVM=1 modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

endif
