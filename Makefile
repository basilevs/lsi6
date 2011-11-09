ifdef KERNELRELEASE

obj-m := lsi6.o
lsi6-objs += lsi6_lib.o lsi6_main.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build


none:
	test -d $(KERNELDIR) && $(MAKE) modules || echo To build module invoke: KERNELDIR=/path/to/your/kernel/headers make modules.

clean:
	-test -d $(KERNELDIR) && $(MAKE) -C $(KERNELDIR) M=$(CURDIR) $@

headers:
	test -d $(KERNELDIR)

modules modules_install: headers
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR) $@

camt:	camt.c
	cc camt.c -o camt -lreadline -lncurses

endif


