
/*
  esp32 firmware OTA

  This example code is in the public domain.

  Chris Joyce

  https://github.com/chrisjoyce911/esp32-fota
*/


#include "local.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "ArduinoJson.h"


// local.h
// Change to your WiFi credentials
// const char* ssid = "";
// const char* password = "";
// const String firwmareupdate = "http://192.168.0.100/fota/fota.json";

const String firwmaretype = "esp32-fota-http";
const int firwmareversion = 1;


WiFiClient espClient;
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


void httpget() {

   Serial.println("Getting HTTP");
   Serial.println("----------------------");
  if ((WiFi.status() == WL_CONNECTED)) { //Check the current connection status
 
    HTTPClient http;
 
    http.begin(firwmareupdate); //Specify the URL
    int httpCode = http.GET();                                        //Make the request
 
    if (httpCode == 200 ) { //Check is a file was returned
 
        String payload = http.getString();

        int str_len = payload.length() + 1; 
        char JSONMessage[str_len];
        payload.toCharArray(JSONMessage, str_len);
        
        StaticJsonBuffer<300> JSONBuffer;                         //Memory pool
        JsonObject& parsed = JSONBuffer.parseObject(JSONMessage); //Parse message
        
        if (!parsed.success()) {   //Check for errors in parsing
          Serial.println("Parsing failed");
          delay(5000);
          return;
        }
        
        const char * pltype = parsed["type"];
        int plversion = parsed["version"];
        const char * plurl = parsed["bin"];

        String fwtype(pltype);
        
        if ( plversion > firwmareversion && fwtype == firwmaretype ) {
          Serial.println("update needed");
          Serial.print("Firmware type: ");
          Serial.println(pltype);
          Serial.print("version: ");
          Serial.println(plversion);
          Serial.print("bin url: ");
          Serial.println(plurl);
        } else {
          Serial.println("no update needed");
        }
        
      }
 
    else {
      Serial.println("Error on HTTP request");
    }
 
    http.end(); //Free the resources
  }
 
  Serial.println("----------------------");
 
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
  httpget();
  digitalWrite(led, LOW);
  delay(2000);
}
