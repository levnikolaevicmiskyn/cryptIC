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
        .release = cryptodev_release
};

/* Usb class */
static struct usb_class_driver cryptodev_class = {
        .name = "cryptodev%d",
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
module_usb_driver(cryptodev_driver);

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
    dev_info(&intf->dev, "USB cryptIC device now attached to USBcryptodev-%d", intf->minor);
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
