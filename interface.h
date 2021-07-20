#include <linux/crypto.h>
/*
  This header collects function that allow integration with the Linux Crypto api
  Based on stm32_hash
*/
#include <linux/crypto.h>
#include <crypto/sha.h>

/* Defines */
#define HASH_MAX_KEY_SIZE (SHA256_BLOCK_SIZE * 8)
#define CRYPTIC_N_BLOCKS 2
#define CRYPTIC_BUF_LEN SHA256_BLOCK_SIZE*CRYPTIC_N_BLOCKS

/* Initial state constants */
#define SHA256_H 0

struct cryptpb{
  u8 message[CRYPTIC_BUF_LEN];
  u8 in_partial_digest[SHA256_DIGEST_SIZE];
  u8 digest[SHA256_DIGEST_SIZE*CRYPTIC_N_BLOCKS];
  u32 len;
};

/* Hash context structure */
struct cryptic_sha256_ctx {
  /* Device structure */
  /* ... */
  spinlock_t lock;

  struct cryptpb* cryptic_data;
  /* key */
  u8 key[HASH_MAX_KEY_SIZE];
  int keylen;
};

/** Context
* state: current state (partial digest)
* count: total data length
* buf: temporary buffer to hold the message. When it is full, it is sent to the device for a partial digest
*      before being updated.
**/
struct cryptic_desc_ctx {
  __u32 state[SHA256_DIGEST_SIZE / 4];
  u64 count;
  u8 buf[CRYPTIC_BUF_LEN];

//  u64 shash[2];
//  u8 buffer[SHA256_DIGEST_SIZE];
//  int bytes;
};


/**
* cryptic_ctx_init: initialization function for a Crypto API context
**/
static int cryptic_cra_sha256_init(struct crypto_tfm *tfm){
  struct cryptic_sha256_ctx* ctx = crypto_tfm_ctx(tfm);
  /* Check device state */
//  if (cryptic_driver.of.status != CRYPTIC_OK){
//    printk(KERN_ALERT "Cannot initialize a crypto context until the device is ready\n");
//    return -ENODEV;
//
  /* Maybe setup a software callback */

  /* Initialize spinlock to protect access to the context */
  spin_lock_init(&ctx->lock);
  ctx->cryptic_data = kmalloc(sizeof (struct cryptpb), GFP_KERNEL);

  if (ctx->cryptic_data == NULL)
    return -ENOMEM;
  return 0;
}

static void cryptic_cra_sha256_exit(struct crypto_tfm* tfm){
  struct cryptic_sha256_ctx* ctx = crypto_tfm_ctx(tfm);
  if (ctx->cryptic_data != NULL)
    kfree(ctx->cryptic_data);

  ctx->cryptic_data = NULL;
}

static int cryptic_sha_update(struct shash_desc* desc, const u8* data, unsigned int len){
  printk(KERN_ALERT "cryptic: entering sha256_update.\n");
  struct cryptic_desc_ctx* ctx = shash_desc_ctx(desc);
  struct cryptic_sha256_ctx* crctx = crypto_tfm_ctx(&(desc->tfm->base));
  struct cryptpb* cryptdata = (struct cryptpb*) crctx->cryptic_data;
  int ret;
  u64 total, start, end, nbytes;
  unsigned long irqflags;
  u64 buflen = ctx->count % CRYPTIC_BUF_LEN;

  /* DEBUG */
  ctx->buf[CRYPTIC_BUF_LEN-1] = '\0';
  printk(KERN_ALERT "cryptic: current buffer is %s\n", ctx->buf);
  /* END DEBUG */
  spin_lock_irqsave(&crctx->lock, irqflags);

  total = buflen + len;
  if (total < CRYPTIC_BUF_LEN){
    /*
      In this case the total message length is still lower than the minimum block size,
      append the new data to the buffer
    */
    memcpy(ctx->buf+buflen, data, len);
    ctx->count += len;
    /* DEBUG */
    ctx->buf[CRYPTIC_BUF_LEN-1] = '\0';
    printk(KERN_ALERT "cryptic: finished update: current buffer is(%d) %s\n", ctx->count, ctx->buf);
    /* END DEBUG */
    spin_unlock_irqrestore(&crctx->lock, irqflags);
    return 0;
  }

  /*
    In this case the total length is larger than the block size, we will process
    N blocks and leave the leftover in the buffer.
  */
  /* Copy what was already present in the buffer */
  if (buflen > 0){
    memcpy(cryptdata->message, ctx->buf, buflen);
    cryptdata->len = buflen;
  }
  else{
    cryptdata->len = 0;
  }
  total -= buflen;
  start = cryptdata->len;
  do{
  /* Copy the output digest into the device's partial digest (!!!CHECK!!!)*/
  memcpy(cryptdata->in_partial_digest, cryptdata->digest, SHA256_DIGEST_SIZE);

  /* The number of bytes of the current transfer is equal to the least between the
  remaining */
  end = min_t(u64, CRYPTIC_BUF_LEN, start+total);

  /* Truncate to a multiple of block size and update the leftover */
  end &= ~(SHA256_BLOCK_SIZE-1);
  nbytes = end-start; // end excluded
  total -= nbytes;

  /* Copy curr bytes to the device request */
  memcpy(cryptdata->message + cryptdata->len, data, nbytes);
  cryptdata->len += nbytes; // = end

  data += nbytes;
  start = end;

  /*  SEND REQUEST THROUGH USB */
} while(total >= CRYPTIC_BUF_LEN);

if (total){
  /* Now copy the leftover into the buffer */
  memcpy(ctx->buf, data, total);
}
ctx->count += len;

memcpy(ctx->state, cryptdata->digest, SHA256_DIGEST_SIZE);
/* DEBUG */
ctx->buf[CRYPTIC_BUF_LEN-1] = '\0';
printk(KERN_ALERT "cryptic: finished update: current buffer is(%d) %s\n", ctx->count, ctx->buf);
/* END DEBUG */
spin_unlock_irqrestore(&crctx->lock, irqflags);
return 0;
}


static int cryptic_sha_final(struct shash_desc* desc, u8* out){
  printk(KERN_ALERT "cryptic: entering sha256_final.\n");
  struct cryptic_desc_ctx* ctx = shash_desc_ctx(desc);
  struct cryptic_sha256_ctx* crctx = crypto_tfm_ctx(&(desc->tfm->base));
  struct cryptpb* cryptdata = (struct cryptpb*) crctx->cryptic_data;

  unsigned long irqflags;
  u64 buflen = ctx->count % CRYPTIC_BUF_LEN;

  spin_lock_irqsave(&crctx->lock, irqflags);
  if (ctx->count > CRYPTIC_BUF_LEN){
    /* In this case there is a partial digest to be copied to the device*/
    memcpy(cryptdata->in_partial_digest, ctx->state, SHA256_DIGEST_SIZE);
  }
  /* DEBUG */
  ctx->buf[CRYPTIC_BUF_LEN-1] = '\0';
  printk(KERN_ALERT "cryptic: final: current buffer is %s\n", ctx->buf);
  /* END DEBUG */
  /* Now copy buffer and finalize */
  if (buflen){
    memcpy(cryptdata->message, ctx->buf, buflen);
    cryptdata->len = buflen;

    /* SEND REQUEST THROUGH USB */
  }

  /*DEBUG*/
  memcpy(cryptdata->digest, ctx->buf, SHA256_DIGEST_SIZE);

  /* Copy result out */
  memcpy(out, cryptdata->digest, SHA256_DIGEST_SIZE);

  spin_unlock_irqrestore(&crctx->lock, irqflags);
  return 0;
}

static int cryptic_sha_init(struct shash_desc* desc){
  printk(KERN_ALERT "cryptic: entering sha256_init function.\n");
  struct cryptic_desc_ctx* ctx = shash_desc_ctx(desc);
  int i;
  memset(ctx, 0, sizeof(struct cryptic_desc_ctx));

  for (i=0; i <= 7; i++){
    ctx->state[i] = SHA256_H;
  }
  ctx->count = 0;
  return 0;
}

/*
struct ahash_alg
  .init: initalize the transformation context
  .update: push a chunk of data into the driver for transformation. The driver then
          passes the data to the driver as seen fit. The function must not finalize the the HASH transformation,
          it only adds data to the transformation.
  .final: Retrieve result from the driver.
  .finup: combination of update and final in sequence
  .digest: combination of init, update and final
  .setkey: Set an optional key used by the hashing algorithm
  .statesize
  .descsize
  .base: crypto_alg structure
*/
static struct shash_alg alg_sha256 = {
  .init   = cryptic_sha_init,
  .update = cryptic_sha_update,
  .final  = cryptic_sha_final,
//  .finup  = cryptic_sha_finup,
//  .digest = cryptic_sha_digest,
  .digestsize = SHA256_DIGEST_SIZE, // =32, defined in crypto/sha2.h
  .statesize = sizeof (struct cryptic_desc_ctx),
  .descsize = sizeof (struct cryptic_desc_ctx),
  .base = {
              .cra_name = "msha256",
              .cra_driver_name = "cryptic-sha256",
              .cra_priority = 300,
              .cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY, // hardware-accelerated but not in the ISA
              .cra_blocksize = SHA256_BLOCK_SIZE, // = 64
              .cra_ctxsize = sizeof(struct cryptic_sha256_ctx),
              //.cra_alignmask = 3, // !!! CHECK
              /* cra_init: initialize the transformation object, this is called right after the
              transformation object is allocated */
              .cra_init = cryptic_cra_sha256_init,
              .cra_exit = cryptic_cra_sha256_exit,
              .cra_module = THIS_MODULE

  }
};

static int cryptic_sha256_register(void){
  int ret = crypto_register_shash(&alg_sha256);
  if (ret < 0){
    printk(KERN_ALERT "cryptic: failed to register sha256.\n");
  }
  else{
    printk(KERN_ALERT "cryptic: sha256 registered successfully.\n");
  }
  return ret;
}

static int cryptic_sha256_unregister(void){
  crypto_unregister_shash(&alg_sha256);
  return 0;
}
