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

// inherit includes from sketch, detect SPIFFS first for legacy support
#if __has_include(<SPIFFS.h>) || defined _SPIFFS_H_
  #pragma message "Using SPIFFS for certificate validation"
  #include <SPIFFS.h>
  #define FOTA_FS &SPIFFS
#elif __has_include(<LittleFS.h>) || defined _LiffleFS_H_
  #pragma message "Using LittleFS for certificate validation"
  #include <LittleFS.h>
  #define FOTA_FS &LittleFS
#elif __has_include(<SD.h>) || defined _SD_H_
  #pragma message "Using SD for certificate validation"
  #include <SD.h>
  #define FOTA_FS &SD
#elif __has_include(<SD_MMC.h>) || defined _SD_MMC_H_
  #pragma message "Using SD_MMC for certificate validation"
  #include <SD_MMC.h>
  #define FOTA_FS &SD_MMC
#elif defined _LIFFLEFS_H_ // older externally linked, hard to identify and unsupported versions of SPIFFS
  #pragma message "this version of LittleFS is unsupported, use #include <LittleFS.h> instead, if using platformio add LittleFS(esp32)@^2.0.0 to lib_deps"
#elif defined _PSRAMFS_H_
  #pragma message "Using PSRamFS for certificate validation"
  #include <PSRamFS.h>
  #define FOTA_FS &PSRamFS
#else
  // #pragma message "No filesystem provided, certificate validation will be unavailable (hint: include SD, SPIFFS or LittleFS before including this library)"
  #define FOTA_FS nullptr
#endif


// Filesystem/memory helper for signature check and pem validation.
// This is abstracted away to allow storage alternatives such as
// PROGMEM, SD, SPIFFS, LittleFS or FatFS
// Intended to be used by esp32FOTA.setPubKey() and esp32FOTA.setRootCA()
class CryptoAsset
{
public:
  virtual size_t size() = 0;
  virtual const char* get() = 0;
};

class CryptoFileAsset : public CryptoAsset
{
public:
  CryptoFileAsset( const char* _path, fs::FS* _fs ) : path(_path), fs(_fs), contents(""), len(0) { }
  size_t size();
  const char* get() { return contents.c_str(); }
private:
  const char* path;
  fs::FS* fs;
  std::string contents;
  size_t len;
  bool fs_read_file(/* fs::FS* fs, const char* path, std::string *out */);
};

class CryptoMemAsset : public CryptoAsset
{
public:
  CryptoMemAsset( const char* _name, const char* _bytes, size_t _len ) : name(_name), bytes(_bytes), len(_len) { }
  size_t size() { return len; };
  const char* get() { return bytes; }
private:
  const char* name;
  const char* bytes;
  size_t len;
};


// Main Class
class esp32FOTA
{
public:
  esp32FOTA(String firwmareType, int firwmareVersion,            bool validate = false, bool allow_insecure_https = false );
  esp32FOTA(String firwmareType, String firmwareSemanticVersion, bool validate = false, bool allow_insecure_https = false );
  ~esp32FOTA();

  void setCertFileSystem( fs::FS *cert_filesystem = nullptr );

  template <typename T> void setPubKey( T* asset ) { PubKey = (CryptoAsset*)asset; _check_sig = true; }
  template <typename T> void setRootCA( T* asset ) { RootCA = (CryptoAsset*)asset; _allow_insecure_https = false; }

  void forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, bool validate );
  void forceUpdate(String firmwareURL, bool validate );
  void forceUpdate(bool validate );
  void execOTA();
  void execOTA( int partition, bool restart_after = true );
  bool execHTTPcheck();
  int getPayloadVersion();
  void getPayloadVersion(char * version_string);
  bool useDeviceID;
  String checkURL;
  bool validate_sig( unsigned char *signature, uint32_t firmware_size );

  typedef std::function<void(size_t, size_t)> ProgressCallback_cb;
  void setProgressCb(ProgressCallback_cb fn) { _ota_progress_callback = fn; }

private:
  String getDeviceID();
  String _firmwareType;
  semver_t _firmwareVersion = semver_t();
  semver_t _payloadVersion = semver_t();
  String _firmwareUrl;
  String _flashFileSystemUrl;
  bool _check_sig;
  bool _allow_insecure_https;
  bool checkJSONManifest(JsonVariant JSONDocument);
  void debugSemVer( const char* label, semver_t* version );

  fs::FS *_fs = FOTA_FS; // default filesystem for certificate validation
  // This is kept for legacy behaviour, use setPubKey() and setRootCA() with
  // CryptoMemAsset ot CryptoFileAsset instead
  const char* rsa_key_pub_default_path = "/rsa_key.pub";
  const char* root_ca_pem_default_path = "/root_ca.pem";

  CryptoAsset *PubKey = nullptr;
  CryptoAsset *RootCA = nullptr;

  void setupCryptoAssets();

  ProgressCallback_cb _ota_progress_callback;

};

#endif
