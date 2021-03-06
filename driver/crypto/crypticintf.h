/*
  This header collects function that allow integration with the Linux Crypto api
  Based on nx_hash driver and via_padlock driver
*/
#ifndef INTERFACE_H
#define INTERFACE_H

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
#include <linux/crypto.h>
#include <linux/stddef.h>

#ifdef SPLIT_SHA_HEADER
#include <crypto/sha2.h>
#else
#include <crypto/sha.h>
#endif

#ifdef FAKE_HARDWARE
#include "softwareHash.h"
#endif

#include "../usb/crypticusb.h"

MODULE_LICENSE("Dual BSD/GPL");

/* Defines */
#define HASH_MAX_KEY_SIZE (SHA256_BLOCK_SIZE * 8)
#define CRYPTIC_N_BLOCKS 2
#define CRYPTIC_BUF_LEN SHA256_BLOCK_SIZE*CRYPTIC_N_BLOCKS

/* cryptic parameter block: this structure is the data sent to the hardware device */
struct cryptpb{
  u8 message[CRYPTIC_BUF_LEN];
  u8 in_partial_digest[SHA256_DIGEST_SIZE];
  u32 len;
  u32 finalize;
  u32 bitlen;
  u8 digest[SHA256_DIGEST_SIZE];
};

/* Hash context structure */
struct cryptic_sha256_ctx {
  /* Device structure */
  /* ... */
  spinlock_t lock;

  struct cryptpb* cryptic_data;

  struct crypto_shash* fallback;
};

/** Context
* state: current state (partial digest)
* count: total data length
* buf: temporary buffer to hold the message. When it is full, it is sent to the device for a partial digest
*      before being updated.
**/
struct cryptic_desc_ctx {
  __u32 state[SHA256_DIGEST_SIZE / 4];
  unsigned int count;
  u8 buf[CRYPTIC_BUF_LEN];
  unsigned int buflen;
  /* Fallback */
  
  unsigned int use_fallback;
  struct shash_desc fallback;
};

/* Function prototypes */
//static int cryptic_cra_sha256_init(struct crypto_shash *tfm);
//static void cryptic_cra_sha256_exit(struct crypto_shash* tfm);
//static int cryptic_submit_request(struct cryptic_desc_ctx* desc, struct cryptpb* cryptdata);
//static int cryptic_sha_update(struct shash_desc* desc, const u8* data, unsigned int len);
//static int cryptic_sha_final(struct shash_desc* desc, u8* out);
//static int cryptic_sha_init(struct shash_desc* desc);
int cryptic_sha256_register(void);
int cryptic_sha256_unregister(void);
#endif
