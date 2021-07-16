#include "usbdriver.h"

/* Global variables **************************************************************************************************/
/* Device table */
static const struct usb_device_id cryptodev_table[] = {
        {USB_DEVICE(CRYPTODEV_ID_VENDOR, CRYPTODEV_ID_PRODUCT)},
        {/* Terminating entry */}
};

/* File operations */
static const struct file_operations cryptodev_fops = {
        .owner = THIS_MODULE,
        .read = cryptodev_read,
        .write = cryptodev_write,
        .open = cryptodev_open,
        .release = cryptodev_release,
        .flush = cryptodev_flush
};

/* Usb class */
static struct usb_class_driver cryptodev_class = {
        .name = "cryptIC%d",
        .fops = &cryptodev_fops,
        .minor_base = CRYPTODEV_MINOR_BASE
};

/* Usb driver */
static struct usb_driver cryptodev_driver = {
        .name = "cryptIC",
        .probe = cryptodev_probe,
        .disconnect = cryptodev_disconnect,
        .suspend = cryptodev_suspend,
        .resume = cryptodev_resume,
        .pre_reset = cryptodev_pre_reset,
        .post_reset = cryptodev_post_reset,
        .id_table = cryptodev_table,
        .supports_autosuspend = 1
};

/* Module setup ******************************************************************************************************/
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(usb, cryptodev_table);
module_init(cryptodev_init);
module_exit(cryptodev_exit);

int cryptodev_init(void) {
    int status;
    /* Register this driver with the USB subsystem */
    status = usb_register(&cryptodev_driver);
    if (status != 0) {
        pr_err("cryptodev: could not register driver: error %d\n", status);
        return -1;
    }
    pr_info("cryptodev: succesfully registered driver!\n");
    return 0;
}

void cryptodev_exit(void) {
    /* Deregister driver from USB subsystem */
    usb_deregister(&cryptodev_driver);
    pr_info("cryptodev: deregistered cryptodev driver\n");
}

/* Module helpers ****************************************************************************************************/
/* Free allocated resources */
static void cryptodev_delete(struct kref *kref) {
    struct usb_cryptodev *dev = to_crypto_dev(kref);

    usb_free_urb(dev->bulk_in_urb);
    usb_put_intf(dev->interface);
    usb_put_dev(dev->udev);
    kfree(dev->bulk_in_buffer);
    kfree(dev);
}

static void cryptodev_draw_down(struct usb_cryptodev *dev) {
    int time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
    if (time == 0)
        usb_kill_anchored_urbs(&dev->submitted);
    usb_kill_urb(dev->bulk_in_urb);
}

static void cryptodev_write_bulk_callback(struct urb *urb) {
    struct usb_cryptodev *dev;
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

static void cryptodev_read_bulk_callback(struct urb *urb) {
    struct usb_cryptodev *dev;
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


static int cryptodev_do_read_io(struct usb_cryptodev *dev, size_t count) {
    int status;

    /* Prepare a read USB Request Block (URB) */
    usb_fill_bulk_urb(dev->bulk_in_urb,
                      dev->udev,
                      usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
                      dev->bulk_in_buffer,
                      min(dev->bulk_in_size, count),
                      cryptodev_read_bulk_callback,
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

/* USB operations ****************************************************************************************************/
int cryptodev_probe(struct usb_interface *intf, const struct usb_device_id *id) {
    struct usb_cryptodev *dev;
    struct usb_endpoint_descriptor *bulk_in, *bulk_out;
    int status;

    /* Allocate memory for our device state and initialize it*/
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

    /* Set up the endpoint information */
    /* Use only the first bulk-in and bulk-out endpoints */
    status = usb_find_common_endpoints(intf->cur_altsetting, &bulk_in, &bulk_out, NULL, NULL);
    if (status != 0) {
        dev_err(&intf->dev, "Could not find bulk-in and bulk-out endpoints\n");
        /* Free memory and return */
        kref_put(&dev->kref, cryptodev_delete);
        return status;
    }

    dev->bulk_in_size = usb_endpoint_maxp(bulk_in);
    dev->bulk_in_endpointAddr = bulk_in->bEndpointAddress;
    dev->bulk_in_buffer = kmalloc(dev->bulk_in_size, GFP_KERNEL);
    if (!dev->bulk_in_buffer) {
        /* Free memory and return */
        kref_put(&dev->kref, cryptodev_delete);
        return -ENOMEM;
    }
    dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_in_urb) {
        /* Free memory and return */
        kref_put(&dev->kref, cryptodev_delete);
        return -ENOMEM;
    }

    dev->bulk_out_endpointAddr = bulk_out->bEndpointAddress;

    /* Save data pointer in interface device */
    usb_set_intfdata(intf, dev);

    /* Register the device and its class */
    status = usb_register_dev(intf, &cryptodev_class);
    if (status != 0) {
        dev_err(&intf->dev, "Unable to register device\n");
        usb_set_intfdata(intf, NULL);
        return status;
    }

    /* Notify user of the connected node */
    dev_info(&intf->dev, "USB cryptIC device now attached to USBcryptIC%d", intf->minor);
    return 0;
}

void cryptodev_disconnect(struct usb_interface *intf) {
    struct usb_cryptodev *dev;
    int minor = intf->minor;

    dev = usb_get_intfdata(intf);
    usb_set_intfdata(intf, NULL);

    /* Deregister device and free up current minor */
    usb_deregister_dev(intf, &cryptodev_class);

    /* Temporarily block IO */
    mutex_lock(&dev->io_mutex);
    dev->disconnected = 1;
    mutex_unlock(&dev->io_mutex);

    usb_kill_urb(dev->bulk_in_urb);
    usb_kill_anchored_urbs(&dev->submitted);

    /* Decrement usage count */
    kref_put(&dev->kref, cryptodev_delete);

    dev_info(&intf->dev, "USB cryptIC %d now disconnected", minor);
}

int cryptodev_suspend(struct usb_interface *intf, pm_message_t message) {
    //FIXME: Actual implementation
    return 0;
}

int cryptodev_resume(struct usb_interface *intf) {
    //FIXME: Actual implementation
    return 0;
}

int cryptodev_pre_reset(struct usb_interface *intf) {
    //FIXME: Actual implementation
    return 0;
}

int cryptodev_post_reset(struct usb_interface *intf) {
    //FIXME: Actual implementation
    return 0;
}

/* File operations ***************************************************************************************************/
int cryptodev_open(struct inode *inode, struct file *file) {
    struct usb_cryptodev *dev;
    struct usb_interface *interface;
    int subminor;
    int status;

    /* Get the requested inode minor and retrieve its USB interface */
    subminor = iminor(inode);

    interface = usb_find_interface(&cryptodev_driver, subminor);
    if (!interface) {
        pr_err("%s - error: can't find "CRYPTODEV_DEVICE_NAME"%d\n", __func__, subminor);
        return -ENODEV;
    }
    /* Get USB interface data */
    dev = usb_get_intfdata(interface);
    if (!dev) {
        return -ENODEV;
    }
    status = usb_autopm_get_interface(interface);
    if (status != 0) {
        return status;
    }

    /* Increment usage count for the device */
    kref_get(&dev->kref);

    /* Save dev object as the associated file's private data */
    file->private_data = dev;
    return status;
}

int cryptodev_release(struct inode *inode, struct file *file) {
    struct usb_cryptodev *dev;
    return 0;
    /* Retrieve device data */
    dev = file->private_data;
    if (dev == NULL)
        return -ENODEV;
    /* Allow the device to be autosuspended */
    usb_autopm_put_interface(dev->interface);
    /* Decrement usage count */
    kref_put(&dev->kref, cryptodev_delete);
    return 0;
}

ssize_t cryptodev_read(struct file *file, char *buffer, size_t count, loff_t *ppos) {
    struct usb_cryptodev *dev;
    int status;
    bool ongoing_io;

    /* Check if the request actually needs data */
    if (!count)
        return 0;
    /* Retrieve device data */
    dev = file->private_data;
    
    /* Lock mutex to prevent others from reading */
    status = mutex_lock_interruptible(&dev->io_mutex);
    if (status < 0)
        return status;

    if (dev->disconnected) {		/* disconnect() was called */
        status = -ENODEV;
        goto exit;
    }

    /* if IO is under way, we must not touch things */
    retry:
    spin_lock_irq(&dev->err_lock);
    ongoing_io = dev->ongoing_read;
    spin_unlock_irq(&dev->err_lock);

    if (ongoing_io) {
        /* nonblocking IO shall not wait */
        if (file->f_flags & O_NONBLOCK) {
            status = -EAGAIN;
            goto exit;
        }
        /*
         * IO may take forever
         * hence wait in an interruptible state
         */
        status = wait_event_interruptible(dev->bulk_in_wait, (!dev->ongoing_read));
        if (status < 0)
            goto exit;
    }

    /* errors must be reported */
    status = dev->errors;
    if (status < 0) {
        /* any error is reported once */
        dev->errors = 0;
        /* to presestatuse notifications about reset */
        status = (status == -EPIPE) ? status : -EIO;
        /* report it */
        goto exit;
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
            /*
             * all data has been used
             * actual IO needs to be done
             */
            status = cryptodev_do_read_io(dev, count);
            if (status < 0)
                goto exit;
            else
                goto retry;
        }
        /*
         * data is available
         * chunk tells us how much shall be copied
         */

        if (copy_to_user(buffer, dev->bulk_in_buffer + dev->bulk_in_copied, chunk))
            status = -EFAULT;
        else
            status = chunk;

        dev->bulk_in_copied += chunk;

        /*
         * if we are asked for more than we have,
         * we start IO but don't wait
         */
        if (available < count)
            cryptodev_do_read_io(dev, count - chunk);
    } else {
        /* no data in the buffer */
        status = cryptodev_do_read_io(dev, count);
        if (status < 0)
            goto exit;
        else
            goto retry;
    }
    exit:
    mutex_unlock(&dev->io_mutex);
    return status;
}

ssize_t cryptodev_write(struct file *file, const char *buffer, size_t count, loff_t *ppos) {
    struct usb_cryptodev *dev;
    int status = 0;
    struct urb *urb = NULL;
    char *buf = NULL;
    size_t writesize = min(count, (size_t) MAX_TRANSFER);

    /* Check if buffer actually contains data */
    if (count == 0)
        return 0;
    
    /* Retrieve device data */
    dev = file->private_data;

    /* Limit the number of URBs in flight to stop a user from using up all RAM */
    if (!(file->f_flags & O_NONBLOCK)) {
        if (down_interruptible(&dev->limit_sem))
            return -ERESTARTSYS;
    } else {
        if (down_trylock(&dev->limit_sem))
            return -EAGAIN;
    }

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
    /* Copy data from user buffer to URB buffer */
    status = copy_from_user(buf, buffer, writesize);
    if (status != 0) {
        usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
        usb_free_urb(urb);
        up(&dev->limit_sem);
        return -EFAULT;
    }

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
                      cryptodev_write_bulk_callback, dev);
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

int cryptodev_flush(struct file *file, fl_owner_t id) {
    struct usb_cryptodev *dev;
    int status;

    /* Retrieve device data */
    dev = file->private_data;
    if (dev == NULL)
        return -ENODEV;

    /* Wait for possibly ongoing IO to stop and acquire mutex */
    mutex_lock(&dev->io_mutex);
    cryptodev_draw_down(dev);

    /* Read errors and leave clean them */
    spin_lock_irq(&dev->err_lock);
    if (dev->errors) {
        status = dev->errors == -EPIPE ? -EPIPE : -EIO;
    } else {
        status = 0;
    }
    dev->errors = 0;
    spin_unlock_irq(&dev->err_lock);

    /* Free IO mutex*/
    mutex_unlock(&dev->io_mutex);
    return status;
}