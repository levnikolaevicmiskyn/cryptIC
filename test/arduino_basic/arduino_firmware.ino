/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "sha256.h"

String message;
const unsigned digest_size = 32;

void setup() {
	Serial.begin(9600);
}

void loop() {
	// Wait until serial data
	while (Serial.available() <= 0);

	message = "";
	while (Serial.available() > 0) {
	message += Serial.readString();
	}

	// Copy the message into message_byte
	BYTE message_byte [];
	for (int i = 0; i < message.size(); i++)
	{
	  message_byte[i] = message[i];
	}
	
	//Compute the sha256 of the received string
	BYTE final_hash[SHA256_BLOCK_SIZE];
	SHA256_CTX ctx;
    sha256(&ctx, text1, strlen((char*)text1), final_hash);  
    
	//Write the result on USB
    Serial.write(final_hash); 
}