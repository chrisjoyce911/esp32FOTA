/*
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://chrisjoyce911/esp32FOTA>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)
*/

#ifndef esp32fota_h
#define esp32fota_h

#include "Arduino.h"

class esp32fota
{
public:
  esp32fota(String firwmareType, int firwmareVersion, String checkURL);
  void execOTA();
  bool execHTTPcheck();
  bool useDeviceID;

private:
  String getHeaderValue(String header, String headerName);
  String getDeviceID();
  String _firwmareType;
  int _firwmareVersion;
  String _checkURL;
  String _host;
  String _bin;
  int _port;
};

#endif
