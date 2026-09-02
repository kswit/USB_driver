EXTRA_CFLAGS += -Wall -Wno-unused-variable
obj-m += gspca_aveo.o

# KDIR: najpierw KERNELRELEASE z DKMS (docelowe jądro podczas aktualizacji
# jądra); w innym wypadku bieżące jądro przy ręcznym `make`.
# KDIR: first the DKMS-provided KERNELRELEASE (the target kernel during a
# kernel update); otherwise the currently running kernel for a manual `make`.
KDIR ?= /lib/modules/$(if $(KERNELRELEASE),$(KERNELRELEASE),$(shell uname -r))/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
