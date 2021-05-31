#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("cryptic");

/* Global variables */
int cryptic_major;
int cryptic_minor;

/* Parameters  */
int cryptic_dev_count = 2;

struct cryptic_dev {
  struct cdev mcdev;
  char* data;
  long length;
};

struct cryptic_dev *cryptdev;

struct file_operations cryptic_fops = {
				       .owner = THIS_MODULE,
				       .read = cryptic_read,
				       .write = cryptic_write,
				       .open = cryptic_open,
				       .release = cryptic_release
};

/* Initialization function  */
static void cryptic_setup_cdev(struct cryptic_dev *dev, int index){
  int devn = MKDEV(cryptdev_major, cryptdev_minor + index);

  /* Initialize cdev structure*/
  cdev_init(&(dev->mcdev), &cryptic_fops);
  dev->mcdev.owner = THIS_MODULE;
  dev->mcdev.ops = &cryptic_fops;

  /* Add cdev structure to kernel */
  int err = cdev_add (&(dev->cdev), devn, 1);
  if(err)
    printk(KERN_NOTICE "Error in adding cryptic\n", err, index);
  else
    printk(KERN_INFO "cryptic was added succesfully", index);
}


int cryptic_init_module(void){
  dev_t dev = 0;
  /* Ask for a dynamic major number */
  int result = alloc_chrdev_region(&dev, cryptic_minor, cryptic_dev_count, "cryptic");
  cryptic_major = MAJOR(dev);

  if (result < 0){
    printk(KERN_WARNING "Failed to get major number for cryptic\n");
    return result;
  }
  else
    printk(KERN_INFO "cryptic's major number is %d\n", cryptic_major);

  /* Allocate devices */
  cryptdev = kmalloc(cryptic_dev_count * sizeof(struct cryptic_dev), GFP_KERNEL);
  if(!cryptdev){
    result = -ENOMEM;
    goto fail;
  }

  /* Initialize memory to 0 */
  memset(cryptdev, 0, cryptic_dev_count*sizeof(struct cryptic_dev));

  /* Initialize device structures */

  int i;
  for (i=0; i < cryptic_dev_count; i++){
    cryptic_setup_cdev(&cryptdev[i], i);
  }

  return 0;

 fail:
  cryptic_cleanup();
  return result;
}

/* Release function */
int cryptic_release(struct inode* inode, struct file* filp){
  return 0;
}

/* Cleanup function */
int cryptic_cleanup(){
  int i;
  dev_t devn = MKDEV(cryptic_major, cryptic_minor);

  /*   */
  if (cryptdev != NULL){
    for (i = 0; i < cryptic_dev_count; i++){
      cryptic_remove(&(cryptdev[i]));
      cdev_del(&(cryptdev[i].cdev));
    }
    kfree(cryptdev);
    cryptdev = NULL;
  }

  unregister_chrdev_region(devn, cryptic_dev_count);
  printk(KERN_INFO "cryptic: Cleanup completed successfully\n");
}

/* Remove function */
int cryptic_remove(struct cryptic_dev* dev){
  /* Free data if allocated */
  if (dev->data != NULL){
    kfree(dev->data);
    dev->data = NULL;
  }

  dev->length = 0;
  return 0;
}

/* Open function  */
int cryptic_open(struct inode* ind, struct file* filp){
  struct cryptdev* dev;

  // Obtain pointer to cryptdev structure starting from the cdev structure that it contains
  dev = container_of(inode->i_cdev, struct cryptic_dev, mcdev);

  filp->private_data = dev;
  
  printk(KERN_DEBUG "Process %i (%s) opened minor %u", current->pid, current->comm, iminor(inode));

  return 0;
}

/* Read function  */
ssize_t cryptic_read(struct file* filp, char __user *buf, size_t count, loff_t* pos){
  struct cryptic_dev* dev = filp->private_data;

  if (dev->data == NULL)
    return 0;
  
  if(*f_pos >= dev->length)
    return 0;

  if(*f_pos + count >= dev->length)
    count = dev->length-*f_pos;

  if (copy_to_user(buf, &(dev->data[*f_pos]), count)){
    return -EFAULT;
  }

  *f_pos += count;
  return count;
}

/* Write function  */
ssize_t cryptic_write(struct file* filp, const char __user *buf, size_t count, loff_t* f_pos){
  struct scull_dev* dev = filp->private_data;
  char* data = dev->data;

  if (data != NULL){
    kfree(data);
    dev->data = NULL;
    dev->size = 0;
  }

  dev->data = (char*) kmalloc(count, GFP_KERNEL);

  if (dev->data == NULL)
    return -ENOMEM;
  
  /* Copy data from user space */
  if(copy_from_user(dev->data, buf, count)){
    return -ENOMEM;
  }

  *f_pos += count;
  dev->length = *f_pos;
  return count;
}

module_init(cryptic_init_module);
module_exit(cryptic_cleanup);
