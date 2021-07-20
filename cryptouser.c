#include <linux/module.h>
#include <crypto/hash.h>
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("cryptouser");
struct sdesc {
    struct shash_desc shash;
    char ctx[];
};

static struct sdesc *init_sdesc(struct crypto_shash *alg)
{
    struct sdesc *sdesc;
    int size;

    size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
    sdesc = kmalloc(size, GFP_KERNEL);
    if (!sdesc)
        return ERR_PTR(-ENOMEM);
    sdesc->shash.tfm = alg;
    return sdesc;
}

static int calc_hash(struct crypto_shash *alg,
             const unsigned char *data, unsigned int datalen,
             unsigned char *digest)
{
    struct sdesc *sdesc;
    int ret;

    sdesc = init_sdesc(alg);
    if (IS_ERR(sdesc)) {
        printk(KERN_ALERT "can't alloc sdesc\n");
        return PTR_ERR(sdesc);
    }

    ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
    kfree(sdesc);
    return ret;
}

static int do_sha256(const unsigned char *data, unsigned char *out_digest)
{
    struct crypto_shash *alg;
    char *hash_alg_name = "msha256";
    unsigned int datalen = 11; // remove the null byte

    alg = crypto_alloc_shash(hash_alg_name, 0, 0);
    if(IS_ERR(alg)){
        pr_info("can't alloc alg %s\n", hash_alg_name);
        return PTR_ERR(alg);
    }
    calc_hash(alg, data, datalen, out_digest);

    // Very dirty print of 8 first bytes for comparaison with sha256sum
    printk(KERN_ALERT "HASH(%s, %i): %02x%02x%02x%02x%02x%02x%02x%02x\n",
          data, datalen, out_digest[0], out_digest[1], out_digest[2], out_digest[3], out_digest[4],
          out_digest[5], out_digest[6], out_digest[7]);

    crypto_free_shash(alg);
    return 0;
}

static int __init module_start(void)
{
    unsigned char *digest;
    digest = kmalloc(256, GFP_KERNEL);
    if(digest < 0)
        return -ENOMEM;

    do_sha256("Test string", digest);
    return -EPERM;

    return 0;
}

module_init(module_start);
