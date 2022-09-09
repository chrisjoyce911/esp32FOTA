/**
   esp32 firmware OTA

   Purpose: Perform an OTA update from a bin located on a webserver

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

// esp32fota esp32fota("<Type of Firmware for this device>", <this version>, <validate signature>);
esp32FOTA esp32FOTA("esp32-fota-http", 1, false);

void setup()
{
  esp32FOTA.checkURL = "http://server/fota/fota.json";
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
  delay(2000);
  esp32FOTA.forceUpdate("192.168.0.100", 80, "/fota/esp32-fota-http-2.bin", true ); // check signature: true

  // Alternatively, forceUpdate can be called with a complete URL:
  //esp32FOTA.forceUpdate("http://192.168.0.100/fota/esp32-fota-http-2.bin", true ); // check signature: true

}
