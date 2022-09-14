/**
   esp32 firmware OTA

   Purpose: Perform an OTA update firmware from a bin located on a webserver (HTTPS)
            while using filesystem to check for certificate validity

*/

#include <SPIFFS.h> // include filesystem **before** esp32fota !!
#include <esp32fota.h>


// esp32fota settings
const int firmware_version  = 2;
#if defined FOTA_URL
  const char* fota_url        = FOTA_URL;
#else
  const char* fota_url        = "https://github.com/chrisjoyce911/esp32FOTA/raw/tests/examples/anyFS/test/stage1/firmware.json";
#endif
const char* firmware_name   = "esp32-fota-http";
const bool check_signature  = false;
const bool disable_security = false;
// for debug only
const char* description     = "SPIFFS example with security";

const char* fota_debug_fmt = R"DBG_FMT(

***************** STAGE %i *****************

  Description      : %s
  Firmware type    : %s
  Firmware version : %i
  Signature check  : %s
  TLS Cert check   : %s

********************************************

)DBG_FMT";

// esp32fota esp32fota("<Type of Firme for this device>", <this version>, <validate signature>, <allow insecure TLS>);
esp32FOTA esp32FOTA( String(firmware_name), firmware_version, check_signature, disable_security );

// create an abstraction of the root_ca file
CryptoFileAsset *GithubRootCA = new CryptoFileAsset( "/github-com.cert.pem", &SPIFFS );

void setup_wifi()
{
  delay(10);
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
  // Provide filesystem with root_ca.pem to validate server certificate
  if( ! SPIFFS.begin( false ) ) {
    Serial.println("SPIFFS Mounting failed, aborting!");
    while(1) vTaskDelay(1);
  }

  esp32FOTA.checkURL = fota_url;
  esp32FOTA.setRootCA( GithubRootCA );

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



