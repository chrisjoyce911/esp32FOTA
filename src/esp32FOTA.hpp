/*
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://github.com/chrisjoyce911/esp32FOTA/esp32FOTA>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)

   Date: 2021-12-21
   Author: Moritz Meintker <https://thinksilicon.de>
   Remarks: Re-written/removed a bunch of functions around HTTPS. The library is
            now URL-agnostic. This means if you provide an https://-URL it will
            use the root_ca.pem (needs to be provided via PROGMEM/SPIFFS/LittleFS or SD)
            to verify the server certificate and then download the ressource through an
            encrypted connection unless you set the allow_insecure_https option.
            Otherwise it will just use plain HTTP which will still offer to sign
            your firmware image.

   Date: 2022-09-12
   Author: tobozo <https://github.com/tobozo>
   Changes:
     - Abstracted away filesystem
     - Refactored some code blocks
     - Added spiffs/littlefs/fatfs updatability
     - Made crypto assets (pub key, rootca) loadable from multiple sources
   Roadmap:
     - Firmware/FlashFS update order (SPIFFS/LittleFS first or last?)
     - Archive support for gz/targz formats
       - firmware.gz + spiffs.gz in manifest
       - bundle.tar.gz [ firmware + filesystem ] in manifest
     - Update from Stream (e.g deported update via SD, http or gzupdater)
*/

#pragma once

#define esp32fota_h

extern "C" {
  #include "semver/semver.h"
}

#include <map>
#include <WiFi.h>

// arduino-esp32 core 2.x => 3.x migration
#if __has_include(<NetworkClientSecure.h>)
  #include <NetworkClientSecure.h>
  #define ClientSecure NetworkClientSecure
#else
  #include <WiFiClientSecure.h>
  #define ClientSecure WiFiClientSecure
#endif

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>

// inherit includes from sketch, detect SPIFFS first for legacy support
#if __has_include(<SPIFFS.h>) || defined _SPIFFS_H_
  #if !defined(DISABLE_ALL_LIBRARY_WARNINGS)
  #pragma message "Using SPIFFS for certificate validation"
  #endif
  #include <SPIFFS.h>
  #define FOTA_FS &SPIFFS
#elif __has_include(<LittleFS.h>) || defined _LiffleFS_H_
  #if !defined(DISABLE_ALL_LIBRARY_WARNINGS)
  #pragma message "Using LittleFS for certificate validation"
  #endif
  #include <LittleFS.h>
  #define FOTA_FS &LittleFS
#elif __has_include(<SD.h>) || defined _SD_H_
  #if !defined(DISABLE_ALL_LIBRARY_WARNINGS)
  #pragma message "Using SD for certificate validation"
  #endif
  #include <SD.h>
  #define FOTA_FS &SD
#elif __has_include(<SD_MMC.h>) || defined _SD_MMC_H_
  #if !defined(DISABLE_ALL_LIBRARY_WARNINGS)
  #pragma message "Using SD_MMC for certificate validation"
  #endif
  #include <SD_MMC.h>
  #define FOTA_FS &SD_MMC
#elif defined _LIFFLEFS_H_ // older externally linked, hard to identify and unsupported versions of SPIFFS
  #if !defined(DISABLE_ALL_LIBRARY_WARNINGS)
  #pragma message "this version of LittleFS is unsupported, use #include <LittleFS.h> instead, if using platformio add LittleFS(esp32)@^2.0.0 to lib_deps"
  #endif
#elif __has_include(<PSRamFS.h>) || defined _PSRAMFS_H_
  #if !defined(DISABLE_ALL_LIBRARY_WARNINGS)
  #pragma message "Using PSRamFS for certificate validation"
  #endif
  #include <PSRamFS.h>
  #define FOTA_FS &PSRamFS
#else
  //#if !defined(DISABLE_ALL_LIBRARY_WARNINGS)
  // #pragma message "No filesystem provided, certificate validation will be unavailable (hint: include SD, SPIFFS or LittleFS before including this library)"
  //#endif
  #define FOTA_FS nullptr
#endif


#if __has_include(<flashz.hpp>)
  #pragma message "Using FlashZ as Update agent"
  #include <flashz.hpp>
  #define F_Compression "zlib"
  #define F_hasZlib() true
  #define F_Update FlashZ::getInstance()
  #define F_UpdateEnd() (mode_z ? F_Update.endz() : F_Update.end())
  #define F_abort() if (mode_z) F_Update.abortz(); else F_Update.abort()
  #define F_writeStream() (mode_z ? F_Update.writezStream(*_stream, updateSize) : F_Update.writeStream(*_stream))
  // #define DEBUG_ESP32_FLASHZ
  #if !defined DEBUG_ESP32_FLASHZ
    #define F_isZlibStream() (_stream->peek() == ZLIB_HEADER && ((partition == U_SPIFFS && _flashFileSystemUrl.indexOf("zz")>-1) || (partition == U_FLASH && _firmwareUrl.indexOf("zz")>-1)))
    #define F_canBegin() (mode_z ? F_Update.beginz(UPDATE_SIZE_UNKNOWN, partition) : F_Update.begin(updateSize, partition))
  #else
    __attribute__((unused)) static bool F_canBegin_cb(bool mode_z, int updateSize, int partition) { // implement debug here
      return (mode_z ? F_Update.beginz(UPDATE_SIZE_UNKNOWN, partition) : F_Update.begin(updateSize, partition));
    }
    __attribute__((unused)) static bool F_isZlibStream_cb( Stream* stream, int partition, String flashFileSystemUrl, String firmwareUrl ) { // implement debug here
      return (stream->peek() == ZLIB_HEADER && ((partition == U_SPIFFS && flashFileSystemUrl.indexOf("zz")>-1) || (partition == U_FLASH && firmwareUrl.indexOf("zz")>-1)));
    }
    #define F_isZlibStream() F_isZlibStream_cb( _stream, partition, _flashFileSystemUrl, _firmwareUrl )
    #define F_canBegin() F_canBegin_cb(mode_z, updateSize, partition)
  #endif

#elif __has_include("ESP32-targz.h")
  #pragma message "Using GzUpdateClass as Update agent"
  #include <ESP32-targz.h>
  #define F_Compression "gzip"
  #define F_hasZlib() true
  #define F_Update GzUpdateClass::getInstance()
  #define F_UpdateEnd() (mode_z ? F_Update.endgz() : F_Update.end())
  #define F_abort() if (mode_z) F_Update.abortgz(); else F_Update.abort()
  #define F_writeStream() (mode_z ? F_Update.writeGzStream(*_stream, updateSize) : F_Update.writeStream(*_stream))
  // #define DEBUG_ESP32_TARGZ
  #if !defined DEBUG_ESP32_TARGZ
    #define F_isZlibStream() (_stream->peek() == 0x1f && ((partition == U_SPIFFS && _flashFileSystemUrl.indexOf("gz")>-1) || (partition == U_FLASH && _firmwareUrl.indexOf("gz")>-1)) )
    #define F_canBegin() (mode_z ? F_Update.begingz(UPDATE_SIZE_UNKNOWN, partition) : F_Update.begin(updateSize, partition))
  #else
    __attribute__((unused)) static bool F_canBegin_cb(bool mode_z, int updateSize, int partition) { // implement debug here
      return (mode_z ? F_Update.begingz(UPDATE_SIZE_UNKNOWN, partition) : F_Update.begin(updateSize, partition));
    }
    __attribute__((unused)) static bool F_isZlibStream_cb( Stream* stream, int partition, String flashFileSystemUrl, String firmwareUrl ) { // implement debug here
      return (stream->peek() == 0x1f && ((partition == U_SPIFFS && flashFileSystemUrl.indexOf("gz")>-1) || (partition == U_FLASH && firmwareUrl.indexOf("gz")>-1)) );
    }
    #define F_isZlibStream() F_isZlibStream_cb( _stream, partition, _flashFileSystemUrl, _firmwareUrl )
    #define F_canBegin() F_canBegin_cb(mode_z, updateSize, partition)
  #endif
#else
  #include <Update.h>
  #define F_Compression "none"
  #define F_Update Update
  #define F_hasZlib() false
  #define F_isZlibStream() false
  #define F_canBegin() F_Update.begin(updateSize, partition)
  #define F_UpdateEnd() F_Update.end()
  #define F_abort() F_Update.abort()
  #define F_writeStream() F_Update.writeStream(*_stream);
#endif

#define FW_SIGNATURE_LENGTH     512

struct SemverClass
{
public:
  SemverClass( const char* version );
  SemverClass( int major, int minor=0, int patch=0 );
  ~SemverClass() { semver_free(&_ver); }
  semver_t* ver();
private:
  semver_t _ver = semver_t();
};




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
  bool fs_read_file();
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


struct FOTAConfig_t
{
  char*        name { nullptr };
  char*        manifest_url { nullptr };
  SemverClass  sem {0};
  bool         check_sig { false };
  bool         unsafe { false };
  bool         use_device_id { false };
  CryptoAsset* root_ca { nullptr };
  CryptoAsset* pub_key { nullptr };
  size_t       signature_len {FW_SIGNATURE_LENGTH};
  bool         allow_reuse { true };
  FOTAConfig_t() = default;
};


enum FOTAStreamType_t
{
  FOTA_HTTP_STREAM,
  FOTA_FILE_STREAM,
  FOTA_SERIAL_STREAM
};


// Main Class
class esp32FOTA
{
public:

  esp32FOTA();
  ~esp32FOTA();

  esp32FOTA( FOTAConfig_t cfg );
  esp32FOTA(const char* firwmareType, int firwmareVersion, bool validate = false, bool allow_insecure_https = false );
  esp32FOTA(const String &firwmareType, int firwmareVersion, bool validate = false, bool allow_insecure_https = false )
    : esp32FOTA(firwmareType.c_str(), firwmareVersion, validate, allow_insecure_https){};
  esp32FOTA(const char* firwmareType, const char* firmwareSemanticVersion, bool validate = false, bool allow_insecure_https = false );
  esp32FOTA(const String &firwmareType, const String &firmwareSemanticVersion, bool validate = false, bool allow_insecure_https = false )
    : esp32FOTA(firwmareType.c_str(), firmwareSemanticVersion.c_str(), validate, allow_insecure_https){};

  template <typename T> void setPubKey( T* asset ) { _cfg.pub_key = (CryptoAsset*)asset; _cfg.check_sig = true; }
  template <typename T> void setRootCA( T* asset ) { _cfg.root_ca = (CryptoAsset*)asset; _cfg.unsafe = false; }

  bool forceUpdate(const char* firmwareHost, uint16_t firmwarePort, const char* firmwarePath, bool validate );
  bool forceUpdate(const char* firmwareURL, bool validate );
  bool forceUpdate(bool validate );

  bool forceUpdateSPIFFS(const char* firmwareURL, bool validate );

  void handle();

  bool execOTA();
  bool execSPIFFSOTA();
  bool execOTA( int partition, bool restart_after = true );
  bool execHTTPcheck();

  void useDeviceId( bool use=true ) { _cfg.use_device_id = use; }

  // config setter
  void setConfig( FOTAConfig_t cfg );
  void printConfig( FOTAConfig_t *cfg=nullptr );

  // Manually specify the manifest url, this is provided as a transition between legagy and new config system
  void setManifestURL( const char* manifest_url ) { setString( &_cfg.manifest_url, manifest_url ); }
  void setManifestURL( const String &manifest_url ) { setManifestURL( manifest_url.c_str() ); }

  // use this to set "Authorization: Basic" or other specific headers to be sent with the queries
  void setExtraHTTPHeader( String name, String value ) { extraHTTPHeaders[name] = value; }

  // set the signature len
  void setSignatureLen( size_t len );

  // /!\ Only use this to change filesystem for **default** RootCA and PubKey paths.
  // Otherwise use setPubKey() and setRootCA()
  void setCertFileSystem( fs::FS *cert_filesystem = nullptr );

  // this is passed to Update.onProgress()
  typedef std::function<void(size_t,size_t)> ProgressCallback_cb; // size_t progress, size_t size
  void setProgressCb(ProgressCallback_cb fn) { onOTAProgress = fn; } // callback setter

  // when Update.begin() returned false
  typedef std::function<void(int)> UpdateBeginFail_cb; // int partition (U_FLASH or U_SPIFFS)
  void setUpdateBeginFailCb(UpdateBeginFail_cb fn) { onUpdateBeginFail = fn; } // callback setter

  // after Update.end() and before validate_sig()
  typedef std::function<void(int)> UpdateEnd_cb; // int partition (U_FLASH or U_SPIFFS)
  void setUpdateEndCb(UpdateEnd_cb fn) { onUpdateEnd = fn; } // callback setter

  // validate_sig() error handling, mixed situations
  typedef std::function<void(int,int)> UpdateCheckFail_cb; // int partition (U_FLASH or U_SPIFFS), int error_code
  void setUpdateCheckFailCb(UpdateCheckFail_cb fn) { onUpdateCheckFail = fn; } // callback setter

  // update successful
  typedef std::function<void(int,bool)> UpdateFinished_cb; // int partition (U_FLASH or U_SPIFFS), bool restart_after
  void setUpdateFinishedCb(UpdateFinished_cb fn) { onUpdateFinished = fn; } // callback setter

  // stream getter
  typedef std::function<int64_t(esp32FOTA*,int)> getStream_cb; // esp32FOTA* this, int partition (U_FLASH or U_SPIFFS), returns stream size
  void setStreamGetter( getStream_cb fn ) { getStream = fn; } // callback setter

  // stream ender
  typedef std::function<void(esp32FOTA*)> endStream_cb; // esp32FOTA* this
  void setStreamEnder( endStream_cb fn ) { endStream = fn; } // callback setter

  // connection check
  typedef std::function<bool()> isConnected_cb; //
  void setStatusChecker( isConnected_cb fn ) { isConnected = fn; } // callback setter

  // updating from a File or from Serial?
  void setStreamType( FOTAStreamType_t stream_type ) { _stream_type = stream_type; }
  void setStreamTimeout( uint32_t timeout ) { _stream_timeout = timeout; }

  const char*       getManifestURL()   { return _manifestUrl.c_str(); }
  const char*       getFirmwareURL()   { return _firmwareUrl.c_str(); }
  const char*       getFlashFS_URL()   { return _flashFileSystemUrl.c_str(); }
  const char*       getPath(int part)  { return part==U_SPIFFS ? getFlashFS_URL() : getFirmwareURL(); }

  bool              zlibSupported()         { return mode_z; }

  int               getPayloadVersion();
  void              getPayloadVersion(char * version_string);

  FOTAConfig_t      getConfig()        { return _cfg; };
  FOTAStreamType_t  getStreamType()    { return _stream_type; }
  HTTPClient*       getHTTPCLient()    { return &_http; }
  ClientSecure*     getWiFiClient()    { return &_client; }
  fs::File*         getFotaFilePtr()   { return &_file; }
  Stream*           getFotaStreamPtr() { return _stream; }
  fs::FS*           getFotaFS()        { return _fs; }

  // internals but need to be exposed to the callbacks
  bool setupHTTP( const char* url );
  void setFotaStream( Stream* stream ) { _stream = stream; }

  //[[deprecated("Use setManifestURL( String ) or cfg.manifest_url with setConfig( FOTAConfig_t )")]] String checkURL = "";
  //[[deprecated("Use cfg.use_device_id with setConfig( FOTAConfig_t )")]] bool useDeviceID = false;


private:

  HTTPClient _http;
  ClientSecure _client;
  Stream *_stream;
  fs::File _file;

  bool mode_z  = F_hasZlib();

  FOTAStreamType_t _stream_type = FOTA_HTTP_STREAM; // defaults to HTTP
  uint32_t _stream_timeout = 10000; // max wait for stream->available()

  void setupStream();
  void stopStream();
  void setString( char **dest, const char* src ); // mem allocator

  FOTAConfig_t _cfg;

  SemverClass _payload_sem = SemverClass(0,0,0);

  String _manifestUrl;
  String _firmwareUrl;
  String _flashFileSystemUrl;

  fs::FS *_fs = FOTA_FS; // default filesystem for certificate validation

  // custom callbacks provided by user
  ProgressCallback_cb onOTAProgress; // this is passed to Update.onProgress()
  UpdateBeginFail_cb  onUpdateBeginFail; // when Update.begin() returned false
  UpdateEnd_cb        onUpdateEnd; // after Update.end() and before validate_sig()
  UpdateCheckFail_cb  onUpdateCheckFail; // validate_sig() error handling, mixed situations
  UpdateFinished_cb   onUpdateFinished; // update successful
  getStream_cb        getStream; // optional stream getter, defaults to http.getStreamPtr()
  endStream_cb        endStream; // optional stream closer, defaults to http.end()
  isConnected_cb      isConnected; // optional connection checker, defaults to WiFi.status()==WL_CONNECTED

  std::map<String,String> extraHTTPHeaders; // this holds the extra http headers defined by the user

  String getDeviceID();
  bool checkJSONManifest(JsonVariant JSONDocument);
  void debugSemVer( const char* label, semver_t* version );
  void getPartition( int update_partition );

  bool validate_sig( const esp_partition_t* partition, unsigned char *signature, uint32_t firmware_size );

  // temporary partition holder for signature check operations
  const esp_partition_t* _target_partition = nullptr;

  // This is kept for legacy behaviour, use setPubKey() and setRootCA() with
  // CryptoMemAsset ot CryptoFileAsset instead
  void setupCryptoAssets();
  const char* rsa_key_pub_default_path = "/rsa_key.pub";
  const char* root_ca_pem_default_path = "/root_ca.pem";

};
