/**
   esp32 firmware OTA

   Purpose: Perform an OTA update to both firmware and filesystem from binaries located
            on a webserver (HTTPS) without checking for certificate validity

   Usage: If the ESP32 had a previous successful WiFi connection, then no need to set the ssid/password
          to run this sketch, the credentials are still cached :-)
          Sketch 1 will FOTA to Sketch 2, then Sketch 3, and so on until all versions in firmware.json are
          exhausted.


*/

#include <esp32fota.h>

// esp32fota settings
const int firmware_version  = 1;
#if defined FOTA_URL
  const char* fota_url        = FOTA_URL;
#else
  const char* fota_url        = "https://github.com/chrisjoyce911/esp32FOTA/raw/tests/examples/anyFS/test/stage1/firmware.json";
#endif
const char* firmware_name   = "esp32-fota-http";
const bool check_signature  = false;
const bool disable_security = true;
// for debug only
const char* description     = "Basic example with no security and no filesystem";

const char* fota_debug_fmt = R"DBG_FMT(

***************** STAGE %i *****************

  Description      : %s
  Firmware type    : %s
  Firmware version : %i
  Signature check  : %s
  TLS Cert check   : %s

********************************************

)DBG_FMT";


// esp32fota esp32fota("<Type of Firmware for this device>", <this version>, <validate signature>, <allow insecure TLS>);
esp32FOTA esp32FOTA( String(firmware_name), firmware_version, check_signature, disable_security );

void setup_wifi()
{
  delay(10);
  //Serial.print("Connecting to WiFi ");
  //Serial.println( ssid );
  Serial.print("MAC Address ");
  Serial.println( WiFi.macAddress() );

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
  Serial.printf( fota_debug_fmt, firmware_version, description, firmware_name, firmware_version, check_signature?"Enabled":"Disabled", disable_security?"Disabled":"Enabled" );

  esp32FOTA.checkURL = fota_url;

  setup_wifi();
}

void loop()
{

  bool updatedNeeded = esp32FOTA.execHTTPcheck();
  if (updatedNeeded)
  {
    esp32FOTA.execOTA();
  }

  delay(20000);
}
