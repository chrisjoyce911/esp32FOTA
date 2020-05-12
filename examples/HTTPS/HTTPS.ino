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
  "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"  
  "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"  
  "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"  
  "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"  
  "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"  
  "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"  
  "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"  
  "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"  
  "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"  
  "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"  
  "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"  
  "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"  
  "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"  
  "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"  
  "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"  
  "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"  
  "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"  
  "rqXRfboQnoZsG4q5WTP468SQvvG5\n"  
  "-----END CERTIFICATE-----\n" ;
  

void setup(){
  Serial.begin(115200);
  WiFiClientSecure clientForOta;

  WiFi.begin(ssid, password);

  // attempt to connect to Wifi network:
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    // wait 1 second for re-trying
    delay(1000);
  }
  
     secureEsp32FOTA secureEsp32FOTA("sensing-pot", 1);
     secureEsp32FOTA._host="firmwareHostWithoutPrependedHTTT.com"; //eg example.com
     secureEsp32FOTA._descriptionOfFirmwareURL="/path-to-firmware-description"; //e /my-fw-versions
     secureEsp32FOTA._certificate=test_root_ca;
     secureEsp32FOTA.clientForOta=clientForOta;
    bool shouldExecuteFirmwareUpdate=secureEsp32FOTA.execHTTPSCheck();
  if(shouldExecuteFirmwareUpdate)
  {
    secureEsp32FOTA._certificate=test_root_ca;
    secureEsp32FOTA.executeOTA();
  }
    
}


void loop()
{
  
  }
