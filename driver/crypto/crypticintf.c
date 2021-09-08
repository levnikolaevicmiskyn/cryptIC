#include "crypticintf.h"

/**
 * cryptic_ctx_init: initialization function for a Crypto API context
 **/
static int cryptic_cra_sha256_init(struct crypto_shash *tfm){
  struct cryptic_sha256_ctx* ctx = crypto_shash_ctx(tfm);
  struct crypto_shash* fallback_tfm = NULL;
  /* Check device state */
  if (!crypticusb_isConnected()){
      /* Setup a software callback */
      const char* fallback_alg_name = crypto_shash_alg_name(tfm);
      pr_info("cryptIC: device not detected, registering fallback algorithm %s", fallback_alg_name);
      /* Allocate a fallback */
      fallback_tfm = crypto_alloc_shash(fallback_alg_name, 0, CRYPTO_ALG_NEED_FALLBACK);
      if (IS_ERR(fallback_tfm)){
          pr_err("cryptIC: cannot allocate a fallback algorithm");
          return PTR_ERR(fallback_tfm);
      }

      ctx->fallback = fallback_tfm;
      tfm->descsize += crypto_shash_descsize(fallback_tfm);
    }
    else {
      pr_info("cryptIC: device detected, using it as accelerator");
      ctx->fallback = NULL;
    }

  /* Initialize spinlock to protect access to the context */
  spin_lock_init(&ctx->lock);
  ctx->cryptic_data = kmalloc(sizeof (struct cryptpb), GFP_KERNEL);

  if (ctx->cryptic_data == NULL)
    return -ENOMEM;
  return 0;
}

static void cryptic_cra_sha256_exit(struct crypto_shash* tfm){
  struct cryptic_sha256_ctx* ctx = crypto_shash_ctx(tfm);
  unsigned long irqflags;

  spin_lock_irqsave(&ctx->lock, irqflags);
  if (ctx->cryptic_data != NULL)
    kfree(ctx->cryptic_data);

  ctx->cryptic_data = NULL;

  if (ctx->fallback != NULL)
    crypto_free_shash(ctx->fallback);

  spin_unlock_irqrestore(&ctx->lock, irqflags);
}

static ssize_t cryptic_submit_request(struct cryptic_desc_ctx* desc, struct cryptpb* cryptdata){
  ssize_t status = 0;
  if (desc->use_fallback){
    /* Device was unavailable at context creation time, resort to the software fallback */
    crypto_shash_update(&(desc->fallback), cryptdata->message, cryptdata->len);
  }
  else{
    /* Try to communicate with device */
    status = crypticusb_send((char*)cryptdata, offsetof(struct cryptpb,digest));
    if (status >= 0){
      /* Read response */
      status = crypticusb_read(cryptdata->digest, SHA256_DIGEST_SIZE);
    }
  }

  return status;
}

static int cryptic_sha_update(struct shash_desc* desc, const u8* data, unsigned int len){
  struct cryptic_desc_ctx* ctx = shash_desc_ctx(desc);
  struct cryptic_sha256_ctx* crctx = crypto_tfm_ctx(&(desc->tfm->base));
  struct cryptpb* cryptdata = (struct cryptpb*) crctx->cryptic_data;
  //int ret;
  unsigned int total, start = 0, nbytes;
  unsigned long irqflags;
  unsigned int buflen = ctx->count % ((unsigned int)CRYPTIC_BUF_LEN);

  spin_lock_irqsave(&crctx->lock, irqflags);

  total = buflen + len;
  if (total <= CRYPTIC_BUF_LEN){
    /*
      In this case the total message length is still lower than the minimum block size,
      append the new data to the buffer
    */
    memcpy(ctx->buf+buflen, data, len);
    ctx->count += len;
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
    start = buflen;
  }

  total -= buflen;

  do{
    /* Copy the output digest into the device's partial digest */
    memcpy(cryptdata->in_partial_digest, cryptdata->digest, SHA256_DIGEST_SIZE);

    /* The number of bytes of the current transfer is equal to the least between the
       remaining */
    nbytes = min_t(unsigned int , (unsigned int)CRYPTIC_BUF_LEN, start + total);
    start = 0;

    total -= nbytes;

    /* Copy curr bytes to the device request */
    memcpy(cryptdata->message, data, nbytes);
    cryptdata->len = CRYPTIC_BUF_LEN;

    data += nbytes;

    /*  FIXME: check the return value */
    cryptic_submit_request(ctx, cryptdata);
  } while(total >= (unsigned int)CRYPTIC_BUF_LEN);

  if (total){
    /* Now copy the leftover into the buffer */
    memcpy(ctx->buf, data, total);
  }
  ctx->count += len;

  memcpy(ctx->state, cryptdata->digest, SHA256_DIGEST_SIZE);

  spin_unlock_irqrestore(&crctx->lock, irqflags);
  return 0;
}


static int cryptic_sha_final(struct shash_desc* desc, u8* out){
  struct cryptic_desc_ctx* ctx = shash_desc_ctx(desc);
  struct cryptic_sha256_ctx* crctx = crypto_tfm_ctx(&(desc->tfm->base));
  struct cryptpb* cryptdata = (struct cryptpb*) crctx->cryptic_data;
  //int i;
  unsigned long irqflags;
  unsigned int buflen = (ctx->count) % ((unsigned int)CRYPTIC_BUF_LEN);
  ssize_t status = 0;

  spin_lock_irqsave(&crctx->lock, irqflags);
  if (ctx->count > CRYPTIC_BUF_LEN){
    /* In this case there is a partial digest to be copied to the device*/
    memcpy(cryptdata->in_partial_digest, ctx->state, SHA256_DIGEST_SIZE);
  }
  /* Now copy buffer and finalize */
  if (buflen){
    memcpy(cryptdata->message, ctx->buf, buflen);
    cryptdata->len = buflen;

    /* SEND REQUEST THROUGH USB */
    status = cryptic_submit_request(ctx, cryptdata);
  }

  //memcpy(cryptdata->digest, ctx->buf, SHA256_DIGEST_SIZE);

  /* Compute result using fallback if applicable*/
  if (ctx->use_fallback)
    crypto_shash_final(&(ctx->fallback), cryptdata->digest);
  /* Copy result out */
  memcpy(out, cryptdata->digest, SHA256_DIGEST_SIZE);

  spin_unlock_irqrestore(&crctx->lock, irqflags);
  printk(KERN_ALERT "cryptIC: it's me");
  return (status>=0 ? 0 : -1);
}

static int cryptic_sha_init(struct shash_desc* desc){
  struct cryptic_desc_ctx* ctx = shash_desc_ctx(desc);
  struct cryptic_sha256_ctx* crctx = crypto_tfm_ctx(&(desc->tfm->base));
  memset(ctx, 0, sizeof(struct cryptic_desc_ctx));

  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;

  ctx->count = 0;

  /* Initialize fallback algorithm if applicable */
  if (crctx->fallback != NULL){
    ctx->fallback.tfm = crctx->fallback;
    crypto_shash_init(&ctx->fallback);
    ctx->use_fallback = 1;
  } else{
    ctx->use_fallback = 0;
  }
  return 0;
}

/*
  struct shash_alg
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
				      .init_tfm = cryptic_cra_sha256_init,
				      .exit_tfm = cryptic_cra_sha256_exit,
				      .base = {
					       .cra_name = "sha256",
					       .cra_driver_name = "cryptic-sha256",
					       .cra_priority = 300,
					       .cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_NEED_FALLBACK, // hardware-accelerated but not in the ISA
					       .cra_blocksize = SHA256_BLOCK_SIZE, // = 64
					       .cra_ctxsize = sizeof(struct cryptic_sha256_ctx),
					       /* cra_init: initialize the transformation object, this is called right after the
						  transformation object is allocated */
					       //.cra_init = cryptic_cra_sha256_init,
					       //.cra_exit = cryptic_cra_sha256_exit,
					       .cra_module = THIS_MODULE

					       }
};

int cryptic_sha256_register(void){
  int ret = crypto_register_shash(&alg_sha256);
  if (ret < 0){
    pr_err("cryptIC: failed to register sha256.\n");
  }
  else{
    pr_info("cryptIC: sha256 registered successfully.\n");
  }
  return ret;
}

int cryptic_sha256_unregister(void){
  crypto_unregister_shash(&alg_sha256);
  return 0;
}


EXPORT_SYMBOL(cryptic_sha256_register);
EXPORT_SYMBOL(cryptic_sha256_unregister);
