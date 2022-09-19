/**
   esp32 firmware OTA

   Purpose: Perform an OTA update from a bin located on a webserver (HTTPS)

*/

// declare filesystem first !

//#include <SD.h>
//#include <SD_MMC.h>
//#include <SPIFFS.h>
#include <LittleFS.h>
//#include <PSRamFS.h>

#include <esp32fota.h> // fota pulls WiFi library


// esp32fota settings
const int firmware_version  = 1;
#if !defined FOTA_URL
  #define FOTA_URL "http://server/fota/fota.json"
#endif
const char* firmware_name   = "esp32-fota-http";
const bool check_signature  = false;
const bool disable_security = false;
// for debug only
const char* description     = "Basic example with any filesystem";

CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &LittleFS );
// CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &SPIFFS );
// CryptoMemAsset *MyRootCA = new CryptoMemAsset("Certificates Chain", root_ca,     strlen(root_ca)+1 );

// CryptoFileAsset *MyRSAKey = new CryptoFileAsset( "/rsa_key.pub", &SPIFFS );
CryptoFileAsset *MyRSAKey = new CryptoFileAsset( "/rsa_key.pub", &LittleFS );
// CryptoMemAsset *MyRSAKey = new CryptoMemAsset("RSA Public Key",     rsa_key_pub, strlen(rsa_key_pub)+1 );


esp32FOTA FOTA;
//esp32FOTA esp32FOTA( String(firmware_name), firmware_version, check_signature, disable_security );


//esp32FOTA esp32FOTA("esp32-fota-http", 1, false );

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
  // Provide filesystem with root_ca.pem to validate server certificate
  if( ! LittleFS.begin( false ) ) {
    Serial.println("LittleFS Mounting failed, aborting!");
    while(1) vTaskDelay(1);
  }


  {
    auto cfg = FOTA.getConfig();

    cfg.name         = firmware_name;
    cfg.manifest_url = FOTA_URL;
    cfg.version      = semver_t{ firmware_version };
    cfg.check_sig    = check_signature;
    cfg.unsafe       = disable_security;
    cfg.root_ca      = MyRootCA;
    cfg.pub_key      = MyRSAKey;

    FOTA.setConfig( cfg );
  }

  // /!\ FOTA.checkURL is deprecated, use setManifestURL( String ) instead
  //FOTA.setManifestURL( FOTA_URL );
  //FOTA.setRootCA( MyRootCA );
  //FOTA.setPubKey( MyRSAKey );
  // use this when more than one filesystem is used in the sketch
  // FOTA.setCertFileSystem( &SD );

  // show progress when an update occurs (e.g. on a TFT display)
  FOTA.setProgressCb( [](size_t progress, size_t size) {
      if( progress == size || progress == 0 ) Serial.println();
      Serial.print(".");
  });

  // add some custom headers to the http queries
  FOTA.setExtraHTTPHeader("Authorization", "Basic <credentials>");

  setup_wifi();
}

void loop()
{

  bool updatedNeeded = FOTA.execHTTPcheck();
  if (updatedNeeded)
  {
    FOTA.execOTA();
  }

  delay(20000);
}

