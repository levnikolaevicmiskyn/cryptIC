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
#include "interface.h"


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("cryptic");

int cryptic_init_module(void){
  // Register algorithm
  int ret;
  ret = cryptic_sha256_register();
  printk(KERN_ALERT "cryptic: driver init function.\n");
  return 0;
}

void cryptic_cleanup(void){
  // Unregister algorithm
  cryptic_sha256_unregister();
}

module_init(cryptic_init_module);
module_exit(cryptic_cleanup);
