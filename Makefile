KGCC=gcc

ifneq ($(KERNELRELEASE),)
obj-m := lsi6.o
lsi6-objs +=  lsi6_lib.o lsi6_main.o
else
KERNELDIR ?=/lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) CC=$(KGCC) modules

endif

camt:	camt.c
	cc camt.c -o camt -lreadline -lncurses

clean:
	rm -f *.o *.ko camt

install:
	rm -f $(KERNELDIR)/misc/lsi6.ko
	install -D -m 644 lsi6.ko $(KERNELDIR)/misc/lsi6.ko
	@depmod
