/**
   esp32 firmware OTA

   Purpose: Perform an OTA update firmware from a bin located on a webserver (HTTPS)
            while using filesystem to check for certificate validity

*/

#include <SPIFFS.h> // include filesystem **before** esp32fota !!
#include <esp32fota.h>
#include <debug/test_fota_common.h>

// esp32fota settings
int firmware_version_major  = 2;
int firmware_version_minor  = 0;
int firmware_version_patch  = 0;

//  #define FOTA_URL "http://server/fota/fota.json"

const char* firmware_name   = "esp32-fota-http";
const bool check_signature  = false;
const bool disable_security = false;
// for debug only
const char* title           = "2";
const char* description     = "SPIFFS example with security";


esp32FOTA FOTA;


// CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &LittleFS );
CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &SPIFFS );
// CryptoMemAsset *MyRootCA = new CryptoMemAsset("Certificates Chain", root_ca, strlen(root_ca)+1 );

// CryptoFileAsset *MyRSAKey = new CryptoFileAsset( "/rsa_key.pub", &SPIFFS );
// CryptoFileAsset *MyRSAKey = new CryptoFileAsset( "/rsa_key.pub", &LittleFS );
// CryptoMemAsset *MyRSAKey = new CryptoMemAsset("RSA Public Key", pub_key, strlen(pub_key)+1 );


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

  PrintFOTAInfo();

  // Provide filesystem with root_ca.pem to validate server certificate
  if( ! SPIFFS.begin( false ) ) {
    Serial.println("SPIFFS Mounting failed, aborting!");
    while(1) vTaskDelay(1);
  }

  {
    auto cfg = FOTA.getConfig();
    cfg.name         = (char*)firmware_name;
    cfg.manifest_url = (char*)FOTA_URL;
    cfg.sem          = SemverClass( firmware_version_major, firmware_version_minor, firmware_version_patch );
    cfg.check_sig    = check_signature;
    cfg.unsafe       = disable_security;
    cfg.root_ca      = MyRootCA;
    //cfg.pub_key      = MyRSAKey;
    FOTA.setConfig( cfg );
  }
  esp32FOTA.printConfig();

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



