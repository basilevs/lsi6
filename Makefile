ifneq ($(KERNELRELEASE),)

obj-m := lsi6.o
lsi6-objs += lsi6_lib.o lsi6_main.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build                                                                                                     
PWD       := $(shell pwd)

modules modules_install clean:                                                                                                                        
	$(MAKE) -C $(KERNELDIR) M=$(PWD) $@

endif

camt:	camt.c
	cc camt.c -o camt -lreadline -lncurses

