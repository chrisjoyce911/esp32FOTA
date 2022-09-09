/*
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://github.com/chrisjoyce911/esp32FOTA/esp32FOTA>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)

   Date: 2021-12-21
   Author: Moritz Meintker <https://thinksilicon.de>
   Remarks: Re-written/removed a bunch of functions around HTTPS. The library is
            now URL-agnostic. This means if you provide an https://-URL it will
            use the root_ca.pem (needs to be provided via SPIFFS) to verify the
            server certificate and then download the ressource through an encrypted
            connection.
            Otherwise it will just use plain HTTP which will still offer to sign
            your firmware image.
*/

#ifndef esp32fota_h
#define esp32fota_h

extern "C" {
  #include "semver/semver.h"
}

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <FS.h>

// inherit includes from sketch
#if __has_include(<SD.h>) || defined _SD_H_
  #pragma message "Using SD for certificate validation"
  #include <SD.h>
  #define FOTA_FS &SD
#elif __has_include(<SD_MMC.h>) || defined _SD_MMC_H_
  #pragma message "Using SD_MMC for certificate validation"
  #include <SD_MMC.h>
  #define FOTA_FS &SD_MMC
#elif __has_include(<SPIFFS.h>) || defined _SPIFFS_H_
  #pragma message "Using SPIFFS for certificate validation"
  #include <SPIFFS.h>
  #define FOTA_FS &SPIFFS
#elif __has_include(<LittleFS.h>) || defined _LiffleFS_H_
  #pragma message "Using LittleFS for certificate validation"
  #include <LittleFS.h>
  #define FOTA_FS &LittleFS
#elif defined _LIFFLEFS_H_
  // probably platformio too dumb to realize LittleFS is now part of esp32 package
  #pragma message "this version of LittleFS is unsupported, use #include <LittleFS.h> instead, if using platformio add LittleFS(esp32)@^2.0.0 to lib_deps"
#elif defined _PSRAMFS_H_
  #pragma message "Using PSRamFS for certificate validation"
  #include <PSRamFS.h>
  #define FOTA_FS &PSRamFS
#else
  #pragma message "No filesystem provided, certificate validation will be unavailable (hint: include SD, SPIFFS or LittleFS before including this library)"
  #define FOTA_FS nullptr
#endif



class esp32FOTA
{
public:
  esp32FOTA(String firwmareType, int firwmareVersion,            boolean validate = false, boolean allow_insecure_https = false );
  esp32FOTA(String firwmareType, String firmwareSemanticVersion, boolean validate = false, boolean allow_insecure_https = false );
  ~esp32FOTA();
  void setCertFileSystem( fs::FS *cert_filesystem = nullptr );
  void forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, boolean validate );
  void forceUpdate(String firmwareURL, boolean validate );
  void forceUpdate(boolean validate );
  void execOTA();
  bool execHTTPcheck();
  int getPayloadVersion();
  void getPayloadVersion(char * version_string);
  bool useDeviceID;
  String checkURL;
  bool validate_sig( unsigned char *signature, uint32_t firmware_size );

private:
  String getDeviceID();
  String _firmwareType;
  semver_t _firmwareVersion = semver_t();
  semver_t _payloadVersion = semver_t();
  String _firmwareUrl;
  boolean _check_sig;
  boolean _allow_insecure_https;
  fs::FS *_fs = FOTA_FS; // filesystem for certificate validation
  bool checkJSONManifest(JsonVariant JSONDocument);
  void debugPayloadVersion( const char* label, semver_t* version );

};

#endif
