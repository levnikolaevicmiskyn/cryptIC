#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256/src/sha256.h"

char test_str[] = "";

int main ()
{
    sha256_t res;
    sha256 (res, (BYTE *)test_str, strlen (test_str));
    for (int i = 0; i < 8; i++)
    {
        printf ("%x", res[i]);
    }
    printf ("\n");
    return 0;
}

