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

// declare filesystem first !

//#include <SD.h>
//#include <SD_MMC.h>
//#include <SPIFFS.h>
#include <LittleFS.h>
//#include <PSRamFS.h>

#include <esp32fota.h> // fota pulls WiFi library

CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/github-com.cert.pem", &LittleFS );


//CryptoMemAsset *MyRSAKey = new CryptoMemAsset("RSA Public Key",     rsa_key_pub, strlen(rsa_key_pub)+1 );
//CryptoMemAsset *MyRootCA = new CryptoMemAsset("Certificates Chain", root_ca,     strlen(root_ca)+1 );


// Change to your WiFi credentials
const char *ssid = "";
const char *password = "";

// esp32fota esp32fota("<Type of Firme for this device>", <this version>, <validate signature>, <allow insecure TLS>);
esp32FOTA esp32FOTA("esp32-fota-http", 1, false );

void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to WiFi ");
  Serial.println( WiFi.macAddress() );
  //Serial.println(ssid);

  WiFi.begin(/*ssid, password*/);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println(WiFi.localIP());

  esp32FOTA.setRootCA( MyRootCA );

}


void setup()
{
  Serial.begin(115200);
  // Provide filesystem with root_ca.pem to validate server certificate
  if( ! LittleFS.begin( false ) ) {
    Serial.println("LittleFS Mounting failed, aborting!");
    while(1) vTaskDelay(1);
  }
  // use this when more than one filesystem is used in the sketch
  // esp32FOTA.setCertFileSystem( &SD );

  esp32FOTA.checkURL = "https://github.com/tobozo/esp32FOTA/raw/tests/examples/anyFS/test/stage1/firmware.json";


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

