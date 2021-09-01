# Compiler definitions
CRYPTIC_DEV_VENDOR_ID := 0x1a86
CRYPTIC_DEV_PRODUCT_ID := 0x7523

# Compiler options
COMPILER_DEFINITIONS := \
	-DCRYPTIC_DEV_VENDOR_ID=${CRYPTIC_DEV_VENDOR_ID} \
	-DCRYPTIC_DEV_PRODUCT_ID=${CRYPTIC_DEV_PRODUCT_ID}

COMPILER_FLAGS += -Werror -Wall ${COMPILER_DEFINITIONS}
CFLAGS_usb/cryptotest.o := ${COMPILER_FLAGS}

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
