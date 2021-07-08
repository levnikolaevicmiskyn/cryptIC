#include "sha256.h"

void sha256_init (hash_t *hash_val)
{
    
	hash_val->h[0] = 0x6a09e667;
	hash_val->h[1] = 0xbb67ae85;
	hash_val->h[2] = 0x3c6ef372;
	hash_val->h[3] = 0xa54ff53a;
	hash_val->h[4] = 0x510e527f;
	hash_val->h[5] = 0x9b05688c;
	hash_val->h[6] = 0x1f83d9ab;
	hash_val->h[7] = 0x5be0cd19;
}

void sha256_update_h (hash_t *res, const hash_t partial)
{
	res->h[0] += partial.h[0];
	res->h[1] += partial.h[1];
	res->h[2] += partial.h[2];
	res->h[3] += partial.h[3];
	res->h[4] += partial.h[4];
	res->h[5] += partial.h[5];
	res->h[6] += partial.h[6];
	res->h[7] += partial.h[7];	
}

void sha256_chunk (hash_t *res, const BYTE data[64])
{
	int32_t w[64]; //Message schedule array
	int32_t s0, s1, t1, t2, maj, ch;
	hash_t h_loc;
	
	//Copy data into first 16 locations of message schedule array
	for (int i = 0, j = 0; i < 16; i++, j+=4)
	{
		w[i] = (data[j + 3] << 24) | (data[j + 2] << 16) | (data[j + 1] << 8) | (data[j]);
	}
	
	//Extend the first 16 words into the remaining 48
	for (int i = 16; i < 64; i++)
	{
		s0 = (ROTRIGHT (w[i - 15], 7) ^ ROTRIGHT (w[i - 15], 18) ^ (w[i - 15] >> 3));
		s1 = (ROTRIGHT (w[i - 2], 17) ^ ROTRIGHT (w[i - 2], 19) ^ (w[i - 2] >> 10));
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	
	//Initialize h to current hash value
	for (int i = 0; i < 8; i++)
	{
		h_loc.h[i] = res->h[i];
	}
	
	//Compression main loop
	for (int i = 0; i < 64; i++)
	{
		s1 = (ROTRIGHT (h_loc.h[4], 6)) ^ (ROTRIGHT (h_loc.h[4], 11)) ^ (ROTRIGHT (h_loc.h[4], 25));
		ch = (h_loc.h[4] && h_loc.h[5]) ^ ( !(h_loc.h[4]) && h_loc.h[6]);
		t1 = h_loc.h[7] + s1 + ch + k[i] + w[i];
		
		s1 = (ROTRIGHT (h_loc.h[0], 2)) ^ (ROTRIGHT (h_loc.h[0], 13)) ^ (ROTRIGHT (h_loc.h[0], 22));
		maj = (h_loc.h[0] && h_loc.h[1]) ^ (h_loc.h[0] && h_loc.h[2]) ^ (h_loc.h[1] && h_loc.h[2]);
		t2 = s0 + maj;
		
		h_loc.h[7] = h_loc.h[6];
		h_loc.h[6] = h_loc.h[5];
		h_loc.h[5] = h_loc.h[4];
		h_loc.h[4] = h_loc.h[3] + t1;
		h_loc.h[3] = h_loc.h[2];
		h_loc.h[2] = h_loc.h[1];
		h_loc.h[1] = h_loc.h[0];
		h_loc.h[0] = t1 + t2;
	}
	
	//Update res
	sha256_update_h (res, h_loc);
}

void sha256 (sha256_t final_res, const BYTE data[], size_t len)
{
	
	BYTE chunk[64]; //512-bit chunk to send to hash function
	int chunk_len = 0;
	int i, j;
	hash_t res;



	sha256_init (&res); //Initialize the hash vector


	for (i = 0; i < len; i++)
	{
		chunk[chunk_len] = data[i];
		chunk_len++;
		if (chunk_len == 64)
		{
			sha256_chunk (&res, chunk);
			chunk_len = 0;
		}
	}
	
	i = chunk_len;
	//padding the remaining bits
	if (chunk_len < 56)
	{
		chunk[i] = 0x80; //Append 1 at the end of the message
		i++;
		while (i < 56)
		{
			chunk[i] = 0x00;
			i++;
		}
	}
	else
	{
		chunk[i] = 0x80;
		i++;
		while (i < 64)
		{
			chunk[i] = 0x00;
			i++;
		}
		sha256_chunk (&res, chunk);
		//Initialize a new chunk to 0
		for (i = 0; i < 56; i++)
		{
			chunk[i] = 0x00;
		}
	}
	
	uint64_t bitlen = len * 8; 
	//Put the lenght of the data in last 64 bits
	for (i = 63, j = 0; i > 55; i--, j += 8)
	{
		chunk[i] = bitlen >> j;
	}
	sha256_chunk (&res, chunk);
	
	
	//Compute the final hash value (Big Endian)
    for (int i = 0; i < 8; i++)
    {
        printf ("%x", res.h[i]);
    }
	
	
	hash_t swapped;
	for (int i = 0; i < 8; i++)
	{		
		swapped.h[i] = 0;
	}
	int32_t mask;
	for (int i = 0; i < 256; i++)
	{
		int pos = i%32;
		mask = 0x1 << i%32;
		swapped[i/32] += mask & (res.h[7-i/32] >> 31 - i%32);
	}
	for (int i = 0; i < 8; i++)
	{		
		printf("%x", swapped.h[i]);
	}

/*     for (i = 0; i < 1; i++){
    
        //swap bit i with bit 255 - i
        //calculate row col
        int row1, row2, col1, col2, first_bit, second_bit;
        row1 = i/32;
        col1 = 31-i%32;
        row2 = (255 -i)/32;
        col2 = (255 -i)%32 -31;
        
        printf(" row1 %x row2 %x\n", res.h[row1], res.h[row2]);
        first_bit = res.h[row1] >> col1 & 0x1;
        second_bit = res.h[row2] >> col2 & 0x1;
        printf("first bit  = %d second bit = %d\n", first_bit, second_bit);
        first_bit ^= second_bit;
        second_bit ^= first_bit;
        first_bit ^= second_bit;
        printf("first bit  = %d second bit = %d\n", first_bit, second_bit);
        res.h[row1] = (res.h[row1] & ~(0x1 << col1)) | (first_bit << col1 ); 
        res.h[row2] = (res.h[row2] & ~(0x1 << col2)) | (second_bit << col2 );
    } */
	for (i = 0; i < 8; i++)
	{
		final_res[i] = res.h[i];
	}
}

/*void sha256 (sha256_t final_res, const BYTE data[], size_t len)
{
	
	BYTE chunk[64]; //512-bit chunk to send to hash function
	int chunk_len = 63;
	int i, j;
	hash_t res;



	sha256_init (&res); //Initialize the hash vector


	for (i = 0; i < len; i++)
	{
		chunk[chunk_len] = data[i];
		chunk_len--;
		if (chunk_len < 0)
		{
			sha256_chunk (&res, chunk);
			chunk_len = 63;
		}
	}
	
	i = chunk_len;
	//padding the remaining bits
	if (chunk_len < 56)
	{
		chunk[i] = 0x80; //Append 1 at the end of the message
		i--;
		while (i >= 8)
		{
			chunk[i] = 0x00;
			i--;
		}
	}
	else
	{
		chunk[i] = 0x80;
		i--;
		while (i > 0)
		{
			chunk[i] = 0x00;
			i--;
		}
		sha256_chunk (&res, chunk);
		//Initialize a new chunk to 0
		for (i = 0; i < 64; i++)
		{
			chunk[i] = 0x00;
		}
	}
	
	uint64_t bitlen = len * 8; 
	//Put the lenght of the data in last 64 bits
	for (i = 7, j = 56; i > 0; i--, j -= 8)
	{
		chunk[i] = bitlen >> j;
	}
	sha256_chunk (&res, chunk);
	
	
	//Compute the final hash value (Big Endian)
	for (i = 0; i < 8; i++)
	{
		final_res[7-i] = res.h[i];
	}
}*/
