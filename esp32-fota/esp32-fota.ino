/*
  esp32 firmware OTA

  This example code is in the public domain.

  Chris Joyce

  https://github.com/chrisjoyce911/esp32-fota
*/


#include "local.h"


// local.h
// Change to your WiFi credentials
//const char* ssid = "";
//const char* password = "";

const int led  =21;
uint64_t chipid;  

void setup() {
	Serial.begin(115200);
  pinMode(led, OUTPUT);
}

void loop() {
  digitalWrite(led, HIGH);

	chipid=ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
	Serial.printf("ESP32 Chip ID = %04X",(uint16_t)(chipid>>32));//print High 2 bytes
	Serial.printf("%08X\n",(uint32_t)chipid);//print Low 4bytes.

	delay(1500);
  digitalWrite(led, LOW);
  delay(1500);
}
