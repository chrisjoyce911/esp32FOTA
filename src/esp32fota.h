/*
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://chrisjoyce911/esp32FOTA>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)
*/

#ifndef esp32fota_h
#define esp32fota_h

#include "Arduino.h"

class esp32FOTA
{
public:
  esp32FOTA(String firwmareType, int firwmareVersion);
  void execOTA();
  bool execHTTPcheck();
  bool useDeviceID;
  String checkURL;

private:
  String getHeaderValue(String header, String headerName);
  String getDeviceID();
  String _firwmareType;
  int _firwmareVersion;
  String _host;
  String _bin;
  int _port;
};

#endif