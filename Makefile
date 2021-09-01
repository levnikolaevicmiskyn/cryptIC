# Compiler definitions
CRYPTIC_DEV_VENDOR_ID := 0x1a86
CRYPTIC_DEV_PRODUCT_ID := 0x7523

# Compiler options
COMPILER_DEFINITIONS := \
	-DCRYPTIC_DEV_VENDOR_ID=${CRYPTIC_DEV_VENDOR_ID} \
	-DCRYPTIC_DEV_PRODUCT_ID=${CRYPTIC_DEV_PRODUCT_ID}

COMPILER_FLAGS += -Werror -Wall ${COMPILER_DEFINITIONS}

# Modules
obj-m += usb/crypticusb.o test/cryptotest.o
CFLAGS_usb/crypticusb.o := ${COMPILER_FLAGS}
all:
	make -C "/lib/modules/$(shell uname -r)/build" "M=$(PWD)" modules

clean:
	$(RM) *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.mod *.symvers *.order
