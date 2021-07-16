#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#ifndef CRYPTIC_USBDRIVER_H
#define CRYPTIC_USBDRIVER_H

#ifndef CRYPTODEV_ID_VENDOR
#error Undefined device Vendor ID
#endif

#ifndef CRYPTODEV_ID_PRODUCT
#error Undefined device Product ID
#endif

#define CRYPTODEV_DEVICE_NAME "cryptIC"
#define CRYPTODEV_MINOR_BASE 100

#define MAX_TRANSFER 512
#define WRITES_IN_FLIGHT 1

/* Module setup ******************************************************************************************************/
int cryptodev_init(void);
void cryptodev_exit(void);

/* USB operations ****************************************************************************************************/
int cryptodev_probe(struct usb_interface *intf, const struct usb_device_id *id);
void cryptodev_disconnect(struct usb_interface *intf);
int cryptodev_suspend(struct usb_interface *intf, pm_message_t message);
int cryptodev_resume(struct usb_interface *intf);
int cryptodev_pre_reset(struct usb_interface *intf);
int cryptodev_post_reset(struct usb_interface *intf);

/* File operations ***************************************************************************************************/
int cryptodev_open(struct inode *inode, struct file *file);
int cryptodev_release(struct inode *inode, struct file *file);
ssize_t cryptodev_read(struct file *file, char *buffer, size_t count, loff_t *ppos);
ssize_t cryptodev_write(struct file *file, const char *buffer, size_t count, loff_t *ppos);
int cryptodev_flush(struct file *file, fl_owner_t id);

/* Types *************************************************************************************************************/
/* Device information */
struct usb_cryptodev {
    struct usb_device *udev;                   /* the usb device for this device */
    struct usb_interface *interface;           /* the interface for this device */
    struct semaphore limit_sem;                /* limiting the number of writes in progress */
    struct usb_anchor submitted;               /* in case we need to retract our submissions */
    struct urb *bulk_in_urb;                   /* the urb to read data with */
    unsigned char *bulk_in_buffer;             /* the buffer to receive data */
    size_t bulk_in_size;                       /* the size of the receive buffer */
    size_t bulk_in_filled;                     /* number of bytes in the buffer */
    size_t bulk_in_copied;                     /* already copied to user space */
    __u8 bulk_in_endpointAddr;                 /* the address of the bulk in endpoint */
    __u8 bulk_out_endpointAddr;                /* the address of the bulk out endpoint */
    int errors;                                /* the last request tanked */
    bool ongoing_read;                         /* a read is going on */
    spinlock_t err_lock;                       /* lock for errors */
    struct kref kref;
    struct mutex io_mutex;                     /* synchronize I/O with disconnect */
    unsigned long disconnected: 1;
    wait_queue_head_t bulk_in_wait;            /* to wait for an ongoing read */
};
#define to_crypto_dev(d) container_of(d, struct usb_cryptodev, kref)

#endif //CRYPTIC_USBDRIVER_H
