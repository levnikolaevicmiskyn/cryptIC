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


int cryptic_init_module(void){
  // Register algorithm
  ret = cryptic_sha256_register();
  printk(KERN_ALERT "cryptic: driver init function.\n");
  return 0;
}

void cryptic_cleanup(void){
  // Unregister algorithm
  crypto_unregister_skcipher(&caesar_alg);
}

module_init(cryptic_init_module);
module_exit(cryptic_cleanup);
