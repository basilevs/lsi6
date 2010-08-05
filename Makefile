
UNAME := $(shell uname -r)
KERNEL26 := 2.6
KERNELVERSION := $(findstring $(KERNEL26),$(UNAME))
PWD := $(shell pwd)

obj-m := lsi6.o
lsi6-objs +=  lsi6_lib.o lsi6_main.o


KDIR	 := /lib/modules/$(shell uname -r)/build
KINCLUDE := 

all::
	$(MAKE) -C $(KDIR) EXTRA_CFLAGS='$(KINCLUDE) -DEXPORT_SYMTAB -O6' SUBDIRS=$(PWD) modules 

modules_install::
	$(MAKE) -C $(KDIR) EXTRA_CFLAGS='$(KINCLUDE) -DEXPORT_SYMTAB -O6' SUBDIRS=$(PWD) modules_install 
	depmod

camt:	camt.c
	cc camt.c -o camt -lreadline -lncurses

clean:
	rm -f *.o *.ko *.cmd camt

