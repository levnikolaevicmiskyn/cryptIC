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
#include <linux/crypto.h>
#include <crypto/skcipher.h>
#include <crypto/internal/skcipher.h>

#define CAES_BLOCK_SIZE 16

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

int cryptic_remove(struct cryptic_dev* dev);
int cryptic_release(struct inode* inode, struct file* filp);
int cryptic_open(struct inode* ind, struct file* filp);
ssize_t cryptic_write(struct file* filp, const char __user *buf, size_t count, loff_t* f_pos);
ssize_t cryptic_read(struct file* filp, char __user *buf, size_t count, loff_t* pos);
void cryptic_cleanup(void);

struct cryptic_dev *cryptdev;

struct file_operations cryptic_fops = {
				       .owner = THIS_MODULE,
				       .read = cryptic_read,
				       .write = cryptic_write,
				       .open = cryptic_open,
				       .release = cryptic_release
};

struct caesar_ctx{
  u8 key;
};

static int caesar_encrypt(struct skcipher_request* req){
  printk(KERN_INFO "I was going to encrypt something but I do not remember what...");
  return 0;
}

static int caesar_decrypt(struct skcipher_request* req){
  return 0;
}

static int caesar_setkey(struct crypto_skcipher* cipher, const u8* key, unsigned int len){
  struct caesar_ctx* ctx = crypto_tfm_ctx(&(cipher->base));

  if (len == 1)
    ctx->key = *key;

  printk(KERN_INFO "Caesar cipher key is set to %d", ctx->key);

  return 0;
}

static int caesar_init(struct crypto_tfm *tfm){
  // Allocate 
  /*void* mstr = kmalloc(sizeof(struct caesar_ctx), GFP_KERNEL);
  if (mstr != NULL){
    ((struct caesar_ctx*)mstr)->key = 0;
    struct crypto_tfm* b = &(tfm->base);
    b->__crt_ctx = mstr;
    printk(KERN_INFO "Created Caesar private context");
    }*/
  return 0;
}

void caesar_exit(struct crypto_tfm* tfm){
  /*struct crypto_tfm* b = &(tfm->base);
  if(b->__crt_ctx != NULL)
  kfree(b->__crt_ctx);*/
}

struct skcipher_alg caesar_alg = {
				  .setkey = caesar_setkey,
				  .encrypt = caesar_encrypt,
				  .decrypt = caesar_decrypt,

				  .min_keysize = 1,
				  .max_keysize = 1,
				  .ivsize = CAES_BLOCK_SIZE,
				  .base = {
					   .cra_name = "caesar",
					   .cra_driver_name = "my_caesar",
					   .cra_priority=300,
					   .cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |CRYPTO_ALG_KERN_DRIVER_ONLY|CRYPTO_ALG_ASYNC,

					   .cra_blocksize = CAES_BLOCK_SIZE,
					   .cra_ctxsize = sizeof(struct caesar_ctx),

					   .cra_init = caesar_init,
					   .cra_exit = caesar_exit
					   }
};


/* Initialization function  */
static void cryptic_setup_cdev(struct cryptic_dev *dev, int index){
  int err;
  int devn = MKDEV(cryptic_major, cryptic_minor + index);
  
  /* Initialize cdev structure*/
  cdev_init(&(dev->mcdev), &cryptic_fops);
  dev->mcdev.owner = THIS_MODULE;
  dev->mcdev.ops = &cryptic_fops;

  /* Add cdev structure to kernel */
  err = cdev_add (&(dev->mcdev), devn, 1);
  if(err)
    printk(KERN_NOTICE "Error in adding cryptic\n");
  else
    printk(KERN_INFO "cryptic was added succesfully");
}


int cryptic_init_module(void){
  int i = 0;
  dev_t dev = 0;
  int ret;
  /* Ask for a dynamic major number */
  int result = alloc_chrdev_region(&dev, cryptic_minor, cryptic_dev_count, "cryptoecho");
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
  for (i=0; i < cryptic_dev_count; i++){
    cryptic_setup_cdev(&cryptdev[i], i);
  }


  // Register algorithm
  ret = crypto_register_skcipher(&caesar_alg);

  if (ret < 0){
    printk(KERN_ALERT "Registration failed %d", ret);
  }
  else
    printk(KERN_ALERT "Registration of caesar succeeded");
  
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
void cryptic_cleanup(void){
  int i;
  dev_t devn = MKDEV(cryptic_major, cryptic_minor);
  if (cryptdev != NULL){
    for (i = 0; i < cryptic_dev_count; i++){
      cryptic_remove(&(cryptdev[i]));
      cdev_del(&(cryptdev[i].mcdev));
    }
    kfree(cryptdev);
    cryptdev = NULL;
  }

  // Unregister algorithm
  crypto_unregister_skcipher(&caesar_alg);

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
  struct cryptic_dev* dev;
  dev = container_of(ind->i_cdev, struct cryptic_dev, mcdev);

  filp->private_data = dev;
  
  printk(KERN_DEBUG "Process %i (%s) opened minor %u", current->pid, current->comm, iminor(ind));

  return 0;
}

/* Read function  */
ssize_t cryptic_read(struct file* filp, char __user *buf, size_t count, loff_t* pos){
  struct cryptic_dev* dev = filp->private_data;

  if (dev->data == NULL){
    printk(KERN_INFO "Reading but data is NULL");
    return 0;
  }
  if(*pos >= dev->length){
    printk(KERN_INFO "Reading but pos is larger than length");
    *pos = 0;
  }
  if(*pos + count >= dev->length){
    count = dev->length-*pos;
    printk(KERN_INFO "Reading and count end is beyond length");
  }

  if (copy_to_user(buf, &(dev->data[*pos]), count)){
    printk(KERN_INFO "Reading failed");
    return -EFAULT;
  }

  *pos += count;
  printk(KERN_INFO "Reading succeded");
  return count;
}

/* Write function  */
ssize_t cryptic_write(struct file* filp, const char __user *buf, size_t count, loff_t* pos){
  struct cryptic_dev* dev = filp->private_data;
  char* data = dev->data;

  if (data != NULL){
    kfree(data);
    dev->data = NULL;
    dev->length = 0;
  }

  dev->data = (char*) kmalloc(count, GFP_KERNEL);

  if (dev->data == NULL)
    return -ENOMEM;
  
  /* Copy data from user space */
  if(copy_from_user(dev->data, buf, count)){
    return -ENOMEM;
  }

  printk(KERN_INFO "Received message: %10s",dev->data);
  *pos += count;
  dev->length = *pos;
  return count;
}

module_init(cryptic_init_module);
module_exit(cryptic_cleanup);
