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

class esp32FOTA
{
public:
  esp32FOTA(String firwmareType, int firwmareVersion, boolean validate = false );
  void forceUpdate(String firwmareHost, int firwmarePort, String firwmarePath, boolean validate = false );
  void execOTA();
  bool execHTTPcheck();
  int getPayloadVersion();
  bool useDeviceID;
  String checkURL;
  bool validate_sig( unsigned char *signature, uint32_t firmware_size );

private:
  String getDeviceID();
  String _firwmareType;
  int _firwmareVersion;
  int _payloadVersion;
  String _host;
  String _bin;
  int _port;
  boolean _check_sig;

};

#endif
