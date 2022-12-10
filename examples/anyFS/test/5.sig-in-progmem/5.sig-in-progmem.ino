/**
   esp32 firmware OTA

   Purpose: Perform an OTA update to both firmware and filesystem from binaries located
            on a webserver (HTTPS) while using progmem to check for certificate validity


*/

#include <esp32fota.h>
#include <debug/test_fota_common.h>

#include "root_ca.h"
#include "pub_key.h"

// esp32fota settings
int firmware_version_major  = 5;
int firmware_version_minor  = 0;
int firmware_version_patch  = 0;

//  #define FOTA_URL "http://server/fota/fota.json"

const char* firmware_name   = "esp32-fota-http";
const bool check_signature  = true;
const bool disable_security = false;
// for debug only
const char* title           = "5";
const char* description     = "PROGMEM example with enforced security";


esp32FOTA FOTA;

// CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &LittleFS );
// CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &SPIFFS );
CryptoMemAsset *MyRootCA = new CryptoMemAsset("Certificates Chain", root_ca, strlen(root_ca)+1 );

// CryptoFileAsset *MyRSAKey = new CryptoFileAsset( "/rsa_key.pub", &SPIFFS );
// CryptoFileAsset *MyRSAKey = new CryptoFileAsset( "/rsa_key.pub", &LittleFS );
CryptoMemAsset *MyRSAKey = new CryptoMemAsset("RSA Public Key", pub_key, strlen(pub_key)+1 );


void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to WiFi ");
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

  PrintFOTAInfo();

  {
    auto cfg = FOTA.getConfig();
    cfg.name         = (char*)firmware_name;
    cfg.manifest_url = (char*)FOTA_URL;
    cfg.sem          = SemverClass( firmware_version_major, firmware_version_minor, firmware_version_patch );
    cfg.check_sig    = check_signature;
    cfg.unsafe       = disable_security;
    cfg.root_ca      = MyRootCA;
    cfg.pub_key      = MyRSAKey;
    FOTA.setConfig( cfg );
  }

  setup_wifi();
}

void loop()
{
  bool updatedNeeded = FOTA.execHTTPcheck();
  if (updatedNeeded) {
    FOTA.execOTA();
  }

  delay(20000);
}


