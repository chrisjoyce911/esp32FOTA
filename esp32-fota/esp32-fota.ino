/**
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://chrisjoyce911/esp32-fota>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)

   Setup:
   Step 1 : add a 'local.h' to your sketch
   Step 2 : const char* ssid = "";
   Step 3 : const char* password = "";
   Step 4 : const String firwmareupdate = "http://server/fota/fota.json";
   
   Upload:
   Step 1 : Menu > Sketch > Export Compiled Library. The bin file will be saved in the sketch folder (Menu > Sketch > Show Sketch folder)
   Step 2 : Upload it to your webserver
   Step 3 : Update your firmware JSON file ( see firwmareupdate )

*/
 

#include "local.h"
#include "execOTA.h"
#include "execHTTPcheck.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "ArduinoJson.h"


// Update your local.h
// Change to your WiFi credentials
// const char* ssid = "";
// const char* password = "";
// const String firwmareupdate = "URL for fota.json";

const String firwmaretype = "esp32-fota-http";
const int firwmareversion = 1;


const int led = 21;
uint64_t chipid;  

void setup() {
	Serial.begin(115200);
  pinMode(led, OUTPUT);

  setup_wifi();
}


void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}



void loop() {
  digitalWrite(led, HIGH);

  Serial.print("Firwmare Type : ");
  Serial.println(firwmaretype);
  
  Serial.print("Version : ");
  Serial.println(firwmareversion);


	chipid=ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
	Serial.printf("ESP32 Chip ID = %04X",(uint16_t)(chipid>>32));//print High 2 bytes
	Serial.printf("%08X\n",(uint32_t)chipid);//print Low 4bytes.

	delay(1500);
  execHTTPcheck(firwmaretype , firwmareversion);
  digitalWrite(led, LOW);
  delay(2000);
}
