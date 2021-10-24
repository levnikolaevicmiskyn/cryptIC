# Project tree
SUBDIRS := . crypto usb
SUBDIRSCLEAN := $(addsuffix clean,$(SUBDIRS))

# Determine kernel version
KERNEL_VERSION := $(shell uname -r)
KERNEL_VMAJ    := $(shell echo ${KERNEL_VERSION} | sed -e 's/^\([0-9][0-9]*\)\.[0-9][0-9]*\.[0-9][0-9]*.*/\1/')
KERNEL_VMIN    := $(shell echo ${KERNEL_VERSION} | sed -e 's/^[0-9][0-9]*\.\([0-9][0-9]*\)\.[0-9][0-9]*.*/\1/')
KERNEL_VPATCH  := $(shell echo ${KERNEL_VERSION} | sed -e 's/^[0-9][0-9]*\.[0-9][0-9]*\.\([0-9][0-9]*\).*/\1/')

# Compiler definitions
CRYPTIC_DEV_VENDOR_ID := 0x1a86
CRYPTIC_DEV_PRODUCT_ID := 0x7523

# Compiler options
COMPILER_DEFINITIONS := \
	-DCRYPTIC_DEV_VENDOR_ID=${CRYPTIC_DEV_VENDOR_ID} \
	-DCRYPTIC_DEV_PRODUCT_ID=${CRYPTIC_DEV_PRODUCT_ID}

ifeq ($(shell expr ${KERNEL_VMIN} \> 10), 1)
	COMPILER_DEFINITIONS += -DSPLIT_SHA_HEADER
endif

COMPILER_FLAGS += -Werror -Wall ${COMPILER_DEFINITIONS}

CFLAGS_usb/crypticusb.o := ${COMPILER_FLAGS}
CFLAGS_crypto/crypticintf.o := ${COMPILER_FLAGS}
CFLAGS_cryptic.o := ${COMPILER_FLAGS}

ifdef FAKE_HARDWARE
	CFLAGS_softwareHash.o := ${COMPILER_FLAGS}
	obj-m += crypto/softwareHash.o
endif

# Modules
obj-m += crypto/crypticintf.o usb/crypticusb.o cryptic.o

# Targets
all:
	make -C "/lib/modules/${KERNEL_VERSION}/build" "M=$(PWD)" modules

clean: $(SUBDIRSCLEAN)

clean_current_dir:
	$(RM) *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.mod *.symvers *.order .*.d

%clean: %
	$(MAKE) -C $< -f $(PWD)/Makefile clean_current_dir
