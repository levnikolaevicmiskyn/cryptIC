/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <string.h>
#include <sha256.h>

#define SHA256_BLOCK_SIZE 32

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
	BYTE message_byte [100];
	for (int i = 0; i < message.length(); i++)
	{
	  message_byte[i] = message[i];
	}
	
	//Compute the sha256 of the received string
	BYTE final_hash[SHA256_BLOCK_SIZE];
	SHA256_CTX ctx;
    sha256(&ctx, message_byte, strlen((char*)message_byte), final_hash);  
    
	//Write the result on USB
    Serial.write(*final_hash); 
}
