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


WiFiClient client;
// Variables to validate
// response from webserver

String host ;
String bin ;
int port = 80;

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
        const char * plhost = parsed["host"];
        port = parsed["port"];
        const char * plbin = parsed["bin"];

        String jshost(plhost);
        String jsbin(plbin);

        host = jshost;
        bin = jsbin;
        
        String fwtype(pltype);
        
        if ( plversion > firwmareversion && fwtype == firwmaretype ) {
          Serial.println("update needed");
          Serial.print("Firmware type: ");
          Serial.println(pltype);
          Serial.print("version: ");
          Serial.println(plversion);
          
          Serial.print("host : ");
          Serial.println(host);
          Serial.print("port : ");
          Serial.println(port);
          Serial.print("bin : ");
          Serial.println(bin);

          execOTA(client, host ,bin ,port);
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
