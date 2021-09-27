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
static void crypticusb_write_bulk_callback(struct urb *urb) {
    struct crypticusb_dev *dev;
    unsigned long flags;

    dev = urb->context;

    /* Sync/async unlink faults aren't errors */
    if (urb->status != 0) {
        if (!(urb->status == -ENOENT ||
              urb->status == -ECONNRESET ||
              urb->status == -ESHUTDOWN)) {
            dev_err(&dev->interface->dev, "%s - nonzero write bulk status received: %d\n", __func__, urb->status);
        }
        spin_lock_irqsave(&dev->err_lock, flags);
        dev->errors = urb->status;
        spin_unlock_irqrestore(&dev->err_lock, flags);
    }

    /* Free up allocated buffer */
    usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
    up(&dev->limit_sem);
}


static void crypticusb_read_bulk_callback(struct urb *urb) {
    struct crypticusb_dev *dev;
    unsigned long flags;

    dev = urb->context;

    spin_lock_irqsave(&dev->err_lock, flags);
    /* sync/async unlink faults aren't errors */
    if (urb->status) {
        if (!(urb->status == -ENOENT ||
              urb->status == -ECONNRESET ||
              urb->status == -ESHUTDOWN)) {
            dev_err(&dev->interface->dev, "%s - nonzero write bulk status received: %d\n", __func__, urb->status);
        }
        dev->errors = urb->status;
    } else {
        dev->bulk_in_filled = urb->actual_length;
    }
    dev->ongoing_read = 0;
    spin_unlock_irqrestore(&dev->err_lock, flags);

    wake_up_interruptible(&dev->bulk_in_wait);
}


static int crypticusb_do_read_io(struct crypticusb_dev *dev, size_t count) {
    int status;

    /* Prepare a read USB Request Block (URB) */
    usb_fill_bulk_urb(dev->bulk_in_urb,
                      dev->udev,
                      usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
                      dev->bulk_in_buffer,
                      min(dev->bulk_in_size, count),
                      crypticusb_read_bulk_callback,
                      dev);
    /* Acquire lock on URB */
    spin_lock_irq(&dev->err_lock);
    dev->ongoing_read = 1;
    spin_unlock_irq(&dev->err_lock);

    /* Submit bulk in urb */
    dev->bulk_in_filled = 0;
    dev->bulk_in_copied = 0;
    status = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);
    if (status < 0) {
        dev_err(&dev->interface->dev, "%s - failed submitting read urb, error %d\n", __func__, status);
        status = status == -ENOMEM ? status : -EIO;
        /* Signal read completion */
        spin_lock_irq(&dev->err_lock);
        dev->ongoing_read = 0;
        spin_unlock_irq(&dev->err_lock);
    }
    return status;
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
    /* Save data pointer in interface device */
    usb_set_intfdata(intf, dev);
    /* Increment usage count for device */
    kref_get(&dev->kref);
    /* Save to global pointer */
    dev_info(&intf->dev, CRYPTIC_DEV_NAME " connected");
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
    dev_info(&intf->dev, CRYPTIC_DEV_NAME " disconnected");
    gdev = NULL;
}

ssize_t crypticusb_send(const char *buffer, size_t count) {
    struct crypticusb_dev *dev;
    int status = 0;
    struct urb *urb = NULL;
    char *buf = NULL;
    size_t writesize = min(count, (size_t) MAX_TRANSFER);

    /* Check if buffer actually contains data */
    if (count == 0)
        return 0;

    /* Retrieve device pointer */
    if (!gdev) {
        pr_err(CRYPTIC_DEV_NAME ": cannot write, device is disconnected\n");
        return -ENODEV;
    }
    dev = gdev;

    /* Check for errors */
    spin_lock_irq(&dev->err_lock);
    status = dev->errors;
    if (status < 0) {
        /* Clear the error but preserve notifications about reset */
        dev->errors = 0;
        status = (status == -EPIPE) ? status : -EIO;
    }
    spin_unlock_irq(&dev->err_lock);

    if (status < 0) {
        up(&dev->limit_sem);
        return status;
    }

    /* Create a USB Request Block (URB) and its buffer */
    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb) {
        up(&dev->limit_sem);
        return -ENOMEM;
    }
    buf = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL, &urb->transfer_dma);
    if (!buf) {
        usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
        usb_free_urb(urb);
        up(&dev->limit_sem);
        return -ENOMEM;
    }
    /* Copy data from buffer to URB buffer */
    memcpy(buf, buffer, writesize);

    /* Check if device is still actually connected */
    mutex_lock(&dev->io_mutex);
    if (dev->disconnected) {
        mutex_unlock(&dev->io_mutex);
        usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
        usb_free_urb(urb);
        up(&dev->limit_sem);
        return -ENODEV;
    }

    /* Initialize URB's other fields */
    usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr), buf, writesize,
                      crypticusb_write_bulk_callback, dev);
    urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
    usb_anchor_urb(urb, &dev->submitted);

    /* Send the data out the bulk port */
    status = usb_submit_urb(urb, GFP_KERNEL);
    mutex_unlock(&dev->io_mutex);
    if (status != 0) {
        dev_err(&dev->interface->dev, "%s - failed submitting write urb, error %d\n", __func__, status);
        usb_unanchor_urb(urb);
        usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
        usb_free_urb(urb);
        up(&dev->limit_sem);
        return status;
    }

    /* Release current reference to this urb, the USB core will eventually free it entirely */
    usb_free_urb(urb);
    return writesize;
}

ssize_t crypticusb_read(char *buffer, size_t count) {
    struct crypticusb_dev *dev;
    int status;
    bool ongoing_io;

    /* Check if the request actually needs data */
    if (!count)
        return 0;
    /* Retrieve device data */
    if (!gdev) {
        pr_err(CRYPTIC_DEV_NAME ": cannot read, device is disconnected\n");
        return -ENODEV;
    }
    dev = gdev;

    /* Lock mutex to prevent others from reading */
    status = mutex_lock_interruptible(&dev->io_mutex);
    if (status < 0)
        return status;

    if (dev->disconnected) {		/* disconnect() was called */
        mutex_unlock(&dev->io_mutex);
        return -ENODEV;
    }

    /* if IO is under way, we must not touch things */
    retry:
    spin_lock_irq(&dev->err_lock);
    ongoing_io = dev->ongoing_read;
    spin_unlock_irq(&dev->err_lock);

    if (ongoing_io) {
        /* IO may take forever, hence wait in an interruptible state */
        status = wait_event_interruptible(dev->bulk_in_wait, (!dev->ongoing_read));
        if (status < 0) {
            mutex_unlock(&dev->io_mutex);
            return status;
        }
    }

    /* Errors must be reported */
    status = dev->errors;
    if (status < 0) {
        /* Any error is reported once */
        dev->errors = 0;
        /* To check notifications about reset */
        status = (status == -EPIPE) ? status : -EIO;
        /* report it */
        mutex_unlock(&dev->io_mutex);
        return status;
    }

    /*
     * if the buffer is filled we may satisfy the read
     * else we need to start IO
     */

    if (dev->bulk_in_filled) {
        /* we had read data */
        size_t available = dev->bulk_in_filled - dev->bulk_in_copied;
        size_t chunk = min(available, count);

        if (!available) {
            /* All data has been used. Actual IO needs to be done */
            status = crypticusb_do_read_io(dev, count);
            if (status < 0) {
                mutex_unlock(&dev->io_mutex);
                return status;
            }
            else
                goto retry;
        }
        /* Data is available. Chunk tells us how much shall be copied */

        memcpy(buffer, dev->bulk_in_buffer + dev->bulk_in_copied, chunk);
        status = chunk;

        dev->bulk_in_copied += chunk;

        /* If we are asked for more than we have, we start IO but don't wait */
        if (available < count)
            crypticusb_do_read_io(dev, count - chunk);
    } else {
        /* no data in the buffer */
        status = crypticusb_do_read_io(dev, count);
        if (status < 0) {
            mutex_unlock(&dev->io_mutex);
            return status;
        }
        else
            goto retry;
    }
    mutex_unlock(&dev->io_mutex);
    return status;
}

int crypticusb_isConnected(void) {
    return gdev != NULL && !gdev->disconnected;
}

MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(usb, crypticusb_devs_table);

EXPORT_SYMBOL_GPL(crypticusb_send);
EXPORT_SYMBOL_GPL(crypticusb_read);
EXPORT_SYMBOL_GPL(crypticusb_init);
EXPORT_SYMBOL_GPL(crypticusb_exit);
EXPORT_SYMBOL_GPL(crypticusb_isConnected);
