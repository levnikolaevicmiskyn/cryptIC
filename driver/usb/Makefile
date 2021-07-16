# Useful variables
CRYPTODEV_ID_VENDOR = 0x1a86
CRYPTODEV_ID_PRODUCT = 0x7523

# Compiler definitions
COMPILER_DEFINITIONS := -DCRYPTODEV_ID_VENDOR=$(CRYPTODEV_ID_VENDOR) -DCRYPTODEV_ID_PRODUCT=$(CRYPTODEV_ID_PRODUCT)
CFLAGS := -Werror -Wall $(COMPILER_DEFINITIONS)
# Modules
obj-m += usbdriver.o
CFLAGS_usbdriver.o := $(CFLAGS)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules $(C_COMPILER_FLAGS)

clean:
	$(RM) *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.mod *.symvers *.order