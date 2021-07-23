
/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "sha256.h"


int main()
{
    BYTE text1[] = {"nelmezzodelcammindinostravitamiritrovaiperunaselvaoscuracheladirettaviaerasmartita"};
    BYTE final_hash[SHA256_BLOCK_SIZE];
	SHA256_CTX ctx;

    sha256(&ctx, text1, strlen((char*)text1), final_hash);  
    
    for ( int i = 0; i < SHA256_BLOCK_SIZE; i++){
        printf("%x", final_hash[i]);
    }    
    printf("\n");

	return(0);
}

