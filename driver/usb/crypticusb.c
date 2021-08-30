#include "crypticusb.h"

#ifndef UNUSED
#define UNUSED(x) (void) (x)
#endif

#define MAX_TRANSFER 512
#define WRITES_IN_FLIGHT 1

/* Types *************************************************************************************************************/
/* Device information */
struct crypticusb_dev {
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
#define to_crypticusb_dev(d) container_of(d, struct crypticusb_dev, kref)

/* Functions headers */
static int crypticusb_probe(struct usb_interface *intf, const struct usb_device_id *id);
void crypticusb_disconnect(struct usb_interface *intf);

/* Globals */
static const struct usb_device_id crypticusb_devs_table[] = {
        {USB_DEVICE(CRYPTIC_DEV_VENDOR_ID, CRYPTIC_DEV_PRODUCT_ID)},
        {/* Terminating entry */}
};

static struct usb_driver crypticusb_driver = {
        .name = CRYPTIC_DEV_NAME,
        .probe = crypticusb_probe,
        .disconnect = crypticusb_disconnect,
        .id_table = crypticusb_devs_table
};

static struct crypticusb_dev *gdev = NULL;

/* Helpers */
static void crypticusb_delete(struct kref *kref) {
    struct crypticusb_dev *dev = to_crypticusb_dev(kref);
    if (dev != gdev) {
        pr_err(CRYPTIC_DEV_NAME ": discrepancy in pointers for freeing memory in %s\n", __PRETTY_FUNCTION__);
    }
    usb_free_urb(dev->bulk_in_urb);
    usb_put_intf(dev->interface);
    usb_put_dev(dev->udev);
    kfree(dev->bulk_in_buffer);
    kfree(dev);
    gdev = NULL;
}

/* Module functions */
int crypticusb_init(void) {
    int status;
    /* Register driver within USB subsystem */
    status = usb_register(&crypticusb_driver);
    if (status != 0) {
        pr_err(CRYPTIC_DEV_NAME ": could not register USB driver: error %d\n", status);
        return -1;
    }
    pr_info(CRYPTIC_DEV_NAME ": succesfully registered USB driver!\n");
    return 0;
}

void crypticusb_exit(void) {
    usb_deregister(&crypticusb_driver);
    pr_info(CRYPTIC_DEV_NAME ": deregistered USB driver\n");
}

static int crypticusb_probe(struct usb_interface *intf, const struct usb_device_id *id) {
    struct crypticusb_dev *dev;
    struct usb_endpoint_descriptor *bulk_in, *bulk_out;
    int status;
    /* Allocate memory for device state and initialize it */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    kref_init(&dev->kref);
    sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
    mutex_init(&dev->io_mutex);
    spin_lock_init(&dev->err_lock);
    init_usb_anchor(&dev->submitted);
    init_waitqueue_head(&dev->bulk_in_wait);

    dev->udev = usb_get_dev(interface_to_usbdev(intf));
    dev->interface = usb_get_intf(intf);

    /* Setup endpoints */
    status = usb_find_common_endpoints(intf->cur_altsetting, &bulk_in, &bulk_out, NULL, NULL);
    if (status != 0) {
        dev_err(&intf->dev, CRYPTIC_DEV_NAME ": could not find bulk-in and bulk-out endpoints\n");
        /* Free memory and return */
        kref_put(&dev->kref, crypticusb_delete);
        return status;
    }
    dev->bulk_in_size = usb_endpoint_maxp(bulk_in);
    dev->bulk_in_endpointAddr = bulk_in->bEndpointAddress;
    dev->bulk_in_buffer = kmalloc(dev->bulk_in_size, GFP_KERNEL);
    if (!dev->bulk_in_buffer) {
        /* Free memory and return */
        kref_put(&dev->kref, crypticusb_delete);
        return -ENOMEM;
    }
    dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_in_urb) {
        /* Free memory and return */
        kref_put(&dev->kref, crypticusb_delete);
        return -ENOMEM;
    }
    dev->bulk_out_endpointAddr = bulk_out->bEndpointAddress;
    /* Save data pointer in interface device and global pointer */
    usb_set_intfdata(intf, dev);
    gdev = dev;
    return 0;
}

void crypticusb_disconnect(struct usb_interface *intf) {
    struct crypticusb_dev *dev;
    dev = usb_get_intfdata(intf);
    if (dev != gdev) {
        pr_err(CRYPTIC_DEV_NAME ": discrepancy in pointers in %s\n", __PRETTY_FUNCTION__);
    }
    /* Empty interface */
    usb_set_intfdata(intf, NULL);

    /* Temporarily block IO */
    mutex_lock(&dev->io_mutex);
    dev->disconnected = 1;
    mutex_unlock(&dev->io_mutex);

    usb_kill_urb(dev->bulk_in_urb);
    usb_kill_anchored_urbs(&dev->submitted);

    /* Decrement usage count */
    kref_put(&dev->kref, crypticusb_delete);
    dev_info(&intf->dev, CRYPTIC_DEV_NAME " now disconnected");
    gdev = NULL;
}

ssize_t crypticusb_send(const char *buffer, size_t count) {
    UNUSED(buffer);
    UNUSED(count);
    return 0;
}

ssize_t crypticusb_read(char *buffer, size_t count) {
    UNUSED(buffer);
    UNUSED(count);
    return 0;
}

int crypticusb_isConnected(void) {
    return !gdev->disconnected;
}

MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(usb, crypticusb_devs_table);
