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

  // Respond with digest
  for (int i = 0; i < digest_size; i++) {
    if (message == "banana")
      Serial.write('Y');
    else
      Serial.write('N');
  }
}
