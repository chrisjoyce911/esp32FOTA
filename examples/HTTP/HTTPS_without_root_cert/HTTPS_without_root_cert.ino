/**
   esp32 firmware OTA

   Purpose: Perform an OTA update from a bin located on a webserver (HTTPS) without having a root cert

   Setup:
   Step 1 : Set your WiFi (ssid & password)
   Step 2 : set esp32fota()

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


// esp32fota esp32fota("<Type of Firmware for this device>", <this version>, <validate signature>, <allow insecure https>);
esp32FOTA esp32FOTA("esp32-fota-http", 1, false, true);
const char* manifest_url = "http://server/fota/fota.json";

void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to WiFi");

  WiFi.begin(); // no WiFi creds in this demo :-)

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
  Serial.begin(115200);
  esp32FOTA.setManifestURL( manifest_url );
  esp32FOTA.printConfig();
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
