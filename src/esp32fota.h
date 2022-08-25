/*
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://github.com/chrisjoyce911/esp32FOTA/esp32FOTA>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)

   Date: 2021-12-21
   Author: Moritz Meintker <https://thinksilicon.de>
   Remarks: Re-written/removed a bunch of functions around HTTPS. The library is
            now URL-agnostic. This means if you provide an https://-URL it will
            use the root_ca.pem (needs to be provided via LittleFS) to verify the
            server certificate and then download the ressource through an encrypted
            connection.
            Otherwise it will just use plain HTTP which will still offer to sign
            your firmware image.
    Date: 2022-08-24
    Author: Nguyen Anh Tuan <https://github.com/tuan-karma>
    Feature added: use the rsa_pub_key.h to embed the key into the fimrware. This feature helps:
    - Preventing someone with physical access to your esp32-board altering the rsa_pub_key in the SPI flash memory
    hence compromising the next firmware update.
    - Being able to update the rsa_key for the next version of your firmware conveniently.
*/

#ifndef esp32fota_h
#define esp32fota_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include "semver/semver.h"

class esp32FOTA
{
public:
  esp32FOTA(String firwmareType, int firwmareVersion, boolean validate = false, boolean allow_insecure_https = false);
  esp32FOTA(String firwmareType, String firmwareSemanticVersion, boolean validate = false, boolean allow_insecure_https = false);
  esp32FOTA(String firwmareType, String firmwareSemanticVersion, const unsigned char *public_key, size_t public_key_length, boolean allow_insecure_https = true);
  ~esp32FOTA();
  void forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, boolean validate);
  void forceUpdate(String firmwareURL, boolean validate);
  void forceUpdate(boolean validate);
  void execOTA();
  void execOTA_internal();
  bool execHTTPcheck();
  int getPayloadVersion();
  void getPayloadVersion(char *version_string);
  bool useDeviceID;
  String checkURL;
  bool validate_sig(unsigned char *signature, uint32_t firmware_size);

private:
  String getDeviceID();
  String _firmwareType;
  semver_t _firmwareVersion = {0};
  semver_t _payloadVersion = {0};
  String _firmwareUrl;
  boolean _check_sig;
  const unsigned char *_public_key = nullptr;
  size_t _public_key_length = 0;
  boolean _allow_insecure_https;
  bool checkJSONManifest(JsonVariant JSONDocument);
  bool validate_sig_internal(const unsigned char *signature, const uint32_t firmware_size);
};

#endif
