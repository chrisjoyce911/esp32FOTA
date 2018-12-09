/*
  esp32fota.h - Library for flashing Morse code.
  Created by David A. Mellis, November 2, 2007.
  Released into the public domain.
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
