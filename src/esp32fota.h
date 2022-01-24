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

#include <Arduino.h>
#include "semver/semver.h"

class esp32FOTA
{
public:
  esp32FOTA(String firwmareType, int firwmareVersion, boolean validate = false, boolean allow_insecure_https = false );
  esp32FOTA(String firwmareType, String firmwareSemanticVersion, boolean validate = false, boolean allow_insecure_https = false );
  ~esp32FOTA();
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
  semver_t _firmwareVersion = {0};
  semver_t _payloadVersion = {0};
  String _firmwareUrl;
  boolean _check_sig;
  boolean _allow_insecure_https;

};

#endif
