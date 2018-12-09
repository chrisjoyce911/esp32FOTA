#include <WiFi.h>
#include <HTTPClient.h>
#include "ArduinoJson.h"

void execHTTPcheck(String firwmaretype , int firwmareversion) {
  
  WiFiClient client;
  String host ;
  String bin ;
  int port = 80;
  
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

          execOTA(host ,bin ,port);
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
