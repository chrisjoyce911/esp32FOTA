#include "ArduinoJson.h"

/*
 * 
 * https://engineerworkshop.com/2019/03/02/esp32-compiler-error-in-arduino-ide-heltec-esp32-tools-esptool-esptool-py-no-module-named-serial-tools-list_ports/
 * 
 * This solves the import serial issue
 * 
 * https://esp32.com/viewtopic.php?t=9289 this shows library to slow down processor
 * 
 * https://www.youtube.com/watch?v=-QIcUTBB7Ww   spiess video for coprocessor programming.
 * ideona: ulp può leggere adc->tensione batteria per svegliare se è veramente il caso
 * 
 * 
 * https://www.youtube.com/watch?v=Ck55tY7mm1c questo video spiega OTA updates usando il framework di espressif
 * 
 * https://github.com/me-no-dev/EspExceptionDecoder exception decoder
*/

// 



#include <WiFiMulti.h>


#include "esp32fota.h"
#include <WiFiClientSecure.h>


const char* ssid     = "";     // your network SSID (name of wifi network)
const char* password = ""; // your network password



char* test_root_ca= \
     "-----BEGIN CERTIFICATE-----\n"  
  "MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBsMQswCQYDVQQG\n"  
  "EwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3d3cuZGlnaWNlcnQuY29tMSsw\n"  
  "KQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5jZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAw\n"  
  "MFoXDTMxMTExMDAwMDAwMFowbDELMAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZ\n"  
  "MBcGA1UECxMQd3d3LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFu\n"  
  "Y2UgRVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm+9S75S0t\n"  
  "Mqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTWPNt0OKRKzE0lgvdKpVMS\n"  
  "OO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEMxChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3\n"  
  "MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFBIk5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQ\n"  
  "NAQTXKFx01p8VdteZOE3hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUe\n"  
  "h10aUAsgEsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQFMAMB\n"  
  "Af8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaAFLE+w2kD+L9HAdSY\n"  
  "JhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3NecnzyIZgYIVyHbIUf4KmeqvxgydkAQ\n"  
  "V8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6zeM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFp\n"  
  "myPInngiK3BD41VHMWEZ71jFhS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkK\n"  
  "mNEVX58Svnw2Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe\n"  
  "vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep+OkuE6N36B9K\n" 
  "-----END CERTIFICATE-----\n" ;
  

WiFiClientSecure clientForOta;
secureEsp32FOTA secureEsp32FOTA("esp32-fota-https", 1);

void setup(){
  Serial.begin(115200);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // attempt to connect to Wifi network:
  byte retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    // wait 1 second for re-trying
    delay(1000);
    retries++;
    if((retries % 5) == 4) WiFi.begin(ssid, password); //solves AUTH_FAIL bug on some routers
  }
  Serial.println("");
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
     
  secureEsp32FOTA._host="raw.githubusercontent.com"; //e.g. example.com
  secureEsp32FOTA._descriptionOfFirmwareURL="/chrisjoyce911/esp32FOTA/master/examples/HTTPS/fota/firmware.json"; //e.g. /my-fw-versions/firmware.json
  secureEsp32FOTA._certificate=test_root_ca;
  secureEsp32FOTA.clientForOta=clientForOta;

  bool shouldExecuteFirmwareUpdate=secureEsp32FOTA.execHTTPSCheck();
  if(shouldExecuteFirmwareUpdate)
  {
    Serial.println("Firmware update available!");
    secureEsp32FOTA.executeOTA();
  }
}


void loop()
{

}
