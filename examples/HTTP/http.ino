/**
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://chrisjoyce911/esp32FOTA>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)

   Setup:
   Step 1 : Set your WiFi (ssid & password)
   Step 2 : set esp32fota()
   
   Upload:
   Step 1 : Menu > Sketch > Export Compiled Library. The bin file will be saved in the sketch folder (Menu > Sketch > Show Sketch folder)
   Step 2 : Upload it to your webserver
   Step 3 : Update your firmware JSON file ( see firwmareupdate )

*/

#include <esp32fota.h>
#include <WiFi.h>

// Change to your WiFi credentials
const char *ssid = "";
const char *password = "";

// esp32fota esp32fota("<Type of Firme for this device>", <this version>, "<URL of update json>");
esp32fota esp32fota("esp32-fota-http", 1, "http://server/fota/fota.json");

void setup()
{
  Serial.begin(115200);
  setup_wifi();
}

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

void loop()
{

  bool updatedNeeded = esp32fota.execHTTPcheck();
  if (updatedNeeded)
  {
    esp32fota.execOTA();
  }

  delay(2000);
}