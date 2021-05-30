#include <crypto.h>

int main(){
  struct crypto_skcipher *tfm = NULL;
  struct skcipher_request *req = NULL;
  struct scatterlist sg;
  u8 *data = NULL;
  const size_t datasize = 512;

  DECLARE_CRYPTO_WAIT(wait);

  u8 iv[16];
  u8 key[64];

  int err;

  tfm = crypto_alloc_skcihper("xts(aes)", 0, 0);

  if (IS_ERR(tfm)){
    pr_err("Error allocating xts(aes) handle: %ld\n", PTR_ERR(tfm));
    return PTR_ERR(tfm);
  }

  get_random_bytes(key, sizeof(key));

  err = crypto_skcipher_setkey(tfm, key, sizeof(key));
  if(err){
    pr_err("Error setting key: %d\n", err);
    goto out;
  }

  req = skcipher_request_alloc(tfm, GFP_KERNEL);
  if(!req){
    err = -ENOMEM;
    goto out;
  }

  data = kmalloc(datasize, GFP_KERNEL);
  if(!data){
    err = -ENOMEM;
    goto out;
  }

  get_random_bytes(data, datasize);

  get_random_bytes(iv, sizeof(iv));

  /* Encrypt */
  sg_init_one(&sg, data, datasize);
  skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
				crypto_req_done, &wait);

  skcipher_request_set_crypt(req, &sg, &sg, datasize, iv);
  err = crypto_wait_req(crypto_skcipher_encrypt(req));
  if(err){
    pr_err("Error encrypting data: %d\n", err);
    goto out;
  }

  pr_debug("Encryption was successful\n");
 out:
  crypto_free_skcipher(tfm);
  skcipher_request_free(req);
  kfree(data);
  return err;
}
