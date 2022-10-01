#pragma once

// esp32fota settings
extern int firmware_version_major;
extern int firmware_version_minor;
extern int firmware_version_patch;

#if !defined FOTA_URL
  #define FOTA_URL "http://server/fota/fota.json"
#endif

extern const char* firmware_name;
extern const bool check_signature;
extern const bool disable_security;
// for debug only
extern const char* description;
extern const char* title;

extern esp32FOTA FOTA;

const char* fota_debug_fmt = R"DBG_FMT(

***************** STAGE %s *****************

  Description      : %s
  Firmware type    : %s
  Firmware version : %i.%i.%i
  Signature check  : %s
  TLS Cert check   : %s
  Compression      : %s

********************************************

)DBG_FMT";


void PrintFOTAInfo()
{
  Serial.printf( fota_debug_fmt,
    title,
    description,
    firmware_name,
    firmware_version_major,
    firmware_version_minor,
    firmware_version_patch,
    check_signature  ?"Enabled":"Disabled",
    disable_security ?"Disabled":"Enabled",
    FOTA.zlibSupported() ?"Enabled":"Disabled"
  );
}
