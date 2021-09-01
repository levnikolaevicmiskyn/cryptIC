ifneq ($(KERNELRELEASE),)
	obj-m := cryptotest.o
else
	KERNELDIR ?= /usr/src/linux-headers-$(shell uname -r)/
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
