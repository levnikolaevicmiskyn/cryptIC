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
#include <crypto/internal/hash.h>

#include "../crypto/interface.h"


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("cryptic");

int cryptic_init_module(void){
  // Register algorithm
  int status;
  /* Initialize usb driver */
  status = crypticusb_init();
  /* If successful, register crypto alg */
  if (status == 0)
    status = cryptic_sha256_register();

  printk(KERN_ALERT "cryptic: driver init function. status = %d\n", status);
  return status;
}

void cryptic_cleanup(void){
  // Unregister algorithm
  cryptic_sha256_unregister();

  // Unregister usb
  crypticusb_exit();
}

module_init(cryptic_init_module);
module_exit(cryptic_cleanup);
