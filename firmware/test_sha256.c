
/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "sha256.h"


int main()
{
<<<<<<< HEAD
    BYTE text1[] = {"nelmezzodelcammindinostravitamiritrovaiperunaselvaoscuracheladirettaviaerasmartita"};
    BYTE final_hash[SHA256_BLOCK_SIZE];
	SHA256_CTX ctx;

    sha256(&ctx, text1, strlen((char*)text1), final_hash);  
    
    for ( int i = 0; i < SHA256_BLOCK_SIZE; i++){
        printf("%x", final_hash[i]);
    }    
    printf("\n");

	return(0);
=======
    sha256_t res;
    sha256 (res, (BYTE *)test_str, strlen (test_str));
    for (int i = 0; i < 32; i++)
    {
        printf ("%x", res[i]);
    }
    printf ("\n");
    return 0;
>>>>>>> 3e6fd1807de3467ad2595966ccbe2cf952ccf3a5
}

