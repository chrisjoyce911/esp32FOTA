
/*
  esp32 firmware OTA

  This example code is in the public domain.

  Chris Joyce

  https://github.com/chrisjoyce911/esp32-fota
*/


#include "local.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "ArduinoJson.h"


// Update your local.h
// Change to your WiFi credentials
// const char* ssid = "";
// const char* password = "";
// const String firwmareupdate = "http://192.168.0.100/fota/fota.json";

const String firwmaretype = "esp32-fota-http";
const int firwmareversion = 1;


WiFiClient client;
// Variables to validate
// response from webserver
int contentLength = 0;
bool isValidContentType = false;
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

          execOTA();
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


// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}


// OTA Logic 
void execOTA() {
  Serial.println("Connecting to: " + String(host));
  // Connect to Webserver
  if (client.connect(host.c_str(), port)) {
    // Connection Succeed.
    // Fecthing the bin
    Serial.println("Fetching Bin: " + String(bin));

    // Get the contents of the bin file
    client.print(String("GET ") + bin + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Cache-Control: no-cache\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println("Client Timeout !");
        client.stop();
        return;
      }
    }
    // Once the response is available,
    // check stuff

    /*
       Response Structure
        HTTP/1.1 200 OK
        x-amz-id-2: NVKxnU1aIQMmpGKhSwpCBh8y2JPbak18QLIfE+OiUDOos+7UftZKjtCFqrwsGOZRN5Zee0jpTd0=
        x-amz-request-id: 2D56B47560B764EC
        Date: Wed, 14 Jun 2017 03:33:59 GMT
        Last-Modified: Fri, 02 Jun 2017 14:50:11 GMT
        ETag: "d2afebbaaebc38cd669ce36727152af9"
        Accept-Ranges: bytes
        Content-Type: application/octet-stream
        Content-Length: 357280
        Server: AmazonS3
                                   
        {{BIN FILE CONTENTS}}
    */
    while (client.available()) {
      // read line till /n
      String line = client.readStringUntil('\n');
      // remove space, to check if the line is end of headers
      line.trim();

      // if the the line is empty,
      // this is end of headers
      // break the while and feed the
      // remaining `client` to the
      // Update.writeStream();
      if (!line.length()) {
        //headers ended
        break; // and get the OTA started
      }

      // Check if the HTTP Response is 200
      // else break and Exit Update
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
          break;
        }
      }

      // extract headers here
      // Start with content length
      if (line.startsWith("Content-Length: ")) {
        contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
        Serial.println("Got " + String(contentLength) + " bytes from server");
      }

      // Next, the content type
      if (line.startsWith("Content-Type: ")) {
        String contentType = getHeaderValue(line, "Content-Type: ");
        Serial.println("Got " + contentType + " payload.");
        if (contentType == "application/octet-stream") {
          isValidContentType = true;
        }
      }
    }
  } else {
    // Connect to webserver failed
    // May be try?
    // Probably a choppy network?
    Serial.println("Connection to " + String(host) + " failed. Please check your setup");
    // retry??
    // execOTA();
  }

  // Check what is the contentLength and if content type is `application/octet-stream`
  Serial.println("contentLength : " + String(contentLength) + ", isValidContentType : " + String(isValidContentType));

  // check contentLength and content type
  if (contentLength && isValidContentType) {
    // Check if there is enough to OTA Update
    bool canBegin = Update.begin(contentLength);

    // If yes, begin
    if (canBegin) {
      Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
      // No activity would appear on the Serial monitor
      // So be patient. This may take 2 - 5mins to complete
      size_t written = Update.writeStream(client);

      if (written == contentLength) {
        Serial.println("Written : " + String(written) + " successfully");
      } else {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?" );
        // retry??
        // execOTA();
      }

      if (Update.end()) {
        Serial.println("OTA done!");
        if (Update.isFinished()) {
          Serial.println("Update successfully completed. Rebooting.");
          ESP.restart();
        } else {
          Serial.println("Update not finished? Something went wrong!");
        }
      } else {
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      }
    } else {
      // not enough space to begin OTA
      // Understand the partitions and
      // space availability
      Serial.println("Not enough space to begin OTA");
      client.flush();
    }
  } else {
    Serial.println("There was no content in the response");
    client.flush();
  }
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
