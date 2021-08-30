#ifndef CRYPTIC_CRYPTICUSB_H
#define CRYPTIC_CRYPTICUSB_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#ifndef CRYPTIC_DEV_VENDOR_ID
#error Undefined CryptIC device vendor ID
#endif

#ifndef CRYPTIC_DEV_PRODUCT_ID
#error Undefined CryptIC device product ID
#endif

#define CRYPTIC_DEV_NAME "cryptIC"

/* USB module setup */
int crypticusb_init(void);
void crypticusb_exit(void);

/* USB module interface */
ssize_t crypticusb_send(const char *buffer, size_t count);
ssize_t crypticusb_read(char *buffer, size_t count);
int crypticusb_isConnected(void);



#endif //CRYPTIC_CRYPTICUSB_H
