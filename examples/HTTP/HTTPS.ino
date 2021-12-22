/**
   esp32 firmware OTA
   
   Purpose: Perform an OTA update from a bin located on a webserver (HTTPS)

   Setup:
   Step 1 : Set your WiFi (ssid & password)
   Step 2 : set esp32fota()
   Step 3 : Provide SPIFFS filesystem with root_ca.pem of your webserver
   
   Upload:
   Step 1 : Menu > Sketch > Export Compiled Library. The bin file will be saved in the sketch folder (Menu > Sketch > Show Sketch folder)
   Step 2 : Upload it to your webserver
   Step 3 : Update your firmware JSON file ( see firwmareupdate )

*/

#include <Arduino.h>

#include <WiFi.h>

#include <FS.h>
#include <SPIFFS.h>
#include <esp32fota.h>


// Change to your WiFi credentials
const char *ssid = "";
const char *password = "";

// esp32fota esp32fota("<Type of Firme for this device>", <this version>, <validate signature>);
esp32FOTA esp32FOTA("esp32-fota-http", 1, false);

void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println(WiFi.localIP());
}

void setup()
{
  // Provide spiffs with root_ca.pem to validate server certificate
  SPIFFS.begin(true);
  
  esp32FOTA.checkURL = "https://server/fota/fota.json";
  Serial.begin(115200);
  setup_wifi();
}

void loop()
{

  bool updatedNeeded = esp32FOTA.execHTTPcheck();
  if (updatedNeeded)
  {
    esp32FOTA.execOTA();
  }

  delay(2000);
}
