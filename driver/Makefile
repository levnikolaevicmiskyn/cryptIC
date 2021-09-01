# Compiler definitions
CRYPTIC_DEV_VENDOR_ID := 0x1a86
CRYPTIC_DEV_PRODUCT_ID := 0x7523

# Compiler options
COMPILER_DEFINITIONS := \
	-DCRYPTIC_DEV_VENDOR_ID=${CRYPTIC_DEV_VENDOR_ID} \
	-DCRYPTIC_DEV_PRODUCT_ID=${CRYPTIC_DEV_PRODUCT_ID}

COMPILER_FLAGS += -Werror -Wall ${COMPILER_DEFINITIONS}

CFLAGS_usb/crypticusb.o := ${COMPILER_FLAGS}
CFLAGS_test/cryptotest.o := ${COMPILER_FLAGS}
CFLAGS_crypto/interface.o := ${COMPILER_FLAGS}

# Modules
#ifneq ($(KERNELRELEASE),)
#	obj-m := test/cryptotest.o
#else
#	KERNELDIR ?= /usr/src/linux-headers-$(shell uname -r)/
#	PWD := $(shell pwd)
#	
#default:
#	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
#endif

obj-m += crypto/interface.o usb/crypticusb.o test/cryptotest.o
all:
	make -C "/lib/modules/$(shell uname -r)/build" "M=$(PWD)" modules

clean:
	$(RM) *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.mod *.symvers *.order
