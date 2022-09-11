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

#include "esp32fota.h"

#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/md_internal.h"
#include "esp_ota_ops.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// Filesystem helper for signature check and pem validation
// This is abstracted away to allow storage alternatives such
// as PROGMEM, SD, SPIFFS, LittleFS or FatFS

bool CryptoFileAsset::fs_read_file()
{
    File file = fs->open( path );
    size_t fsize = file.size();
    // if( file->size() > ESP.getFreeHeap() ) return false;
    if( !file ) {
        log_e( "Failed to open %s for reading", path );
        return false;
    }
    contents = ""; // make sure the output bucket is empty
    while( file.available() ) {
        contents.push_back( file.read() );
    }
    file.close();
    return contents.size()>0 && fsize==contents.size();
}


size_t CryptoFileAsset::size()
{
    if( len > 0 ) { // already stored, no need to access filesystem
        return len;
    }
    if( fs ) { // fetch file contents
        if( ! fs_read_file() ) {
          log_w("Invalid contents!");
          return 0;
        }
        len = contents.size();
    } else {
        log_e("No filesystem was set for %s!", path);
        return 0;
    }
    return len;
}




esp32FOTA::esp32FOTA(String firmwareType, int firmwareVersion, bool validate, bool allow_insecure_https)
{
    _firmwareType = firmwareType;
    _firmwareVersion = semver_t{firmwareVersion};
    _check_sig = validate;
    _allow_insecure_https = allow_insecure_https;
    useDeviceID = false;
    setupCryptoAssets();
    debugSemVer("Current firmware version", &_firmwareVersion );
}


esp32FOTA::esp32FOTA(String firmwareType, String firmwareSemanticVersion, bool validate, bool allow_insecure_https)
{
    if (semver_parse(firmwareSemanticVersion.c_str(), &_firmwareVersion)) {
        log_e( "Invalid semver string %s passed to constructor. Defaulting to 0", firmwareSemanticVersion.c_str() );
        _firmwareVersion = semver_t {0};
    }
    _firmwareType = firmwareType;
    _check_sig = validate;
    _allow_insecure_https = allow_insecure_https;
    useDeviceID = false;
    setupCryptoAssets();
    debugSemVer("Current firmware version", &_firmwareVersion );
}


esp32FOTA::~esp32FOTA()
{
    semver_free(&_firmwareVersion);
    semver_free(&_payloadVersion);
}


void esp32FOTA::setCertFileSystem( fs::FS *cert_filesystem )
{
    _fs = cert_filesystem;
    setupCryptoAssets();
}


// Used for legacy behaviour when SPIFFS and RootCa/PubKey had default values
// New recommended method is to use setPubKey() and setRootCA() with CryptoMemAsset ot CryptoFileAsset objects.
void esp32FOTA::setupCryptoAssets()
{
    if( _fs ) {
        PubKey = (CryptoAsset*)(new CryptoFileAsset( rsa_key_pub_default_path, _fs  ));
        RootCA = (CryptoAsset*)(new CryptoFileAsset( root_ca_pem_default_path, _fs  ));
    }
}



// SHA-Verify the OTA partition after it's been written
// https://techtutorialsx.com/2018/05/10/esp32-arduino-mbed-tls-using-the-sha-256-algorithm/
// https://github.com/ARMmbed/mbedtls/blob/development/programs/pkey/rsa_verify.c
bool esp32FOTA::validate_sig( unsigned char *signature, uint32_t firmware_size )
{
    int ret = 1;
    size_t pubkeylen = PubKey ? PubKey->size()+1 : 0;
    const char* pubkeystr = PubKey->get();

    if( pubkeylen <= 1 ) {
        return false;
    }

    mbedtls_pk_context pk;
    mbedtls_md_context_t rsa;
    mbedtls_pk_init( &pk );

    if( ( ret = mbedtls_pk_parse_public_key( &pk, (const unsigned char*)pubkeystr, pubkeylen ) ) != 0 ) {
        log_e( "Reading public key failed\n  ! mbedtls_pk_parse_public_key %d\n\n", ret );
        return false;
    }

    if( !mbedtls_pk_can_do( &pk, MBEDTLS_PK_RSA ) ) {
        log_e( "Public key is not an rsa key -0x%x\n\n", -ret );
        return false;
    }

    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);

    if( !partition ) {
        log_e( "Could not find update partition!" );
        return false;
    }

    const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type( MBEDTLS_MD_SHA256 );
    mbedtls_md_init( &rsa );
    mbedtls_md_setup( &rsa, mdinfo, 0 );
    mbedtls_md_starts( &rsa );

    int bytestoread = SPI_FLASH_SEC_SIZE;
    int bytesread = 0;
    int size = firmware_size;

    uint8_t *_buffer = (uint8_t*)malloc(SPI_FLASH_SEC_SIZE);
    if(!_buffer){
        log_e( "malloc failed" );
        return false;
    }

    //Serial.printf( "Reading partition (%i sectors, sec_size: %i)\r\n", size, bytestoread );
    while( bytestoread > 0 ) {
        //Serial.printf( "Left: %i (%i)               \r", size, bytestoread );

        if( ESP.partitionRead( partition, bytesread, (uint32_t*)_buffer, bytestoread ) ) {
            // Debug output for the purpose of comparing with file
            /*for( int i = 0; i < bytestoread; i++ ) {
              if( ( i % 16 ) == 0 ) {
                Serial.printf( "\r\n0x%08x\t", i + bytesread );
              }
              Serial.printf( "%02x ", (uint8_t*)_buffer[i] );
            }*/

            mbedtls_md_update( &rsa, (uint8_t*)_buffer, bytestoread );

            bytesread = bytesread + bytestoread;
            size = size - bytestoread;

            if( size <= SPI_FLASH_SEC_SIZE ) {
                bytestoread = size;
            }
        } else {
            log_e( "partitionRead failed!" );
            return false;
        }
    }

    free( _buffer );

    unsigned char *hash = (unsigned char*)malloc( mdinfo->size );
    if(!hash){
        log_e( "malloc failed" );
        return false;
    }
    mbedtls_md_finish( &rsa, hash );

    ret = mbedtls_pk_verify( &pk, MBEDTLS_MD_SHA256, hash, mdinfo->size, (unsigned char*)signature, 512 );

    free( hash );
    mbedtls_md_free( &rsa );
    mbedtls_pk_free( &pk );
    if( ret == 0 ) {
        return true;
    }

    // validation failed, overwrite the first few bytes so this partition won't boot!

    ESP.partitionEraseRange( partition, 0, ENCRYPTED_BLOCK_SIZE);

    return false;
}


// OTA Logic
void esp32FOTA::execOTA()
{
    if( _flashFileSystemUrl != "" ) { // handle the spiffs partition first
        if( _fs ) { // Possible risk of overwriting certs and signatures, cancel flashing!
            log_e("Cowardly refusing to overwrite U_SPIFFS. Use setCertFileSystem(nullptr) along with setPubKey()/setCAPem() to enable this feature.");
        } else {
            execOTA( U_SPIFFS, false );
        }
    }
    // handle the application partition and restart on success
    execOTA( U_FLASH, true );
}


void esp32FOTA::execOTA( int partition, bool restart_after )
{
    String UpdateURL = "";

    switch( partition ) {
        case U_SPIFFS: // spiffs/littlefs/fatfs partition
            if( _flashFileSystemUrl == "" ) {
                log_i("[SKIP] No spiffs/littlefs/fatfs partition was speficied");
                return;
            }
            UpdateURL = _flashFileSystemUrl;
        break;
        case U_FLASH: // app partition (default)
        default:
            partition = U_FLASH;
            UpdateURL = _firmwareUrl;
        break;
    }

    int contentLength = 0;
    bool isValidContentType = false;
    const char* rootcastr = nullptr;

    HTTPClient http;
    WiFiClientSecure client;
    //http.setConnectTimeout( 1000 );
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    log_i("Connecting to: %s\r\n", UpdateURL.c_str() );
    if( UpdateURL.substring( 0, 5 ) == "https" ) {
        if (!_allow_insecure_https) {
            log_i( "Loading root_ca.pem" );
            if( !RootCA || RootCA->size() == 0 ) {
                log_e("A strict security context has been set but no RootCA was provided");
                return;
            }
            rootcastr = RootCA->get();
            if( !rootcastr ) {
                log_e("Unable to get RootCA, aborting");
                return;
            }
            client.setCACert( rootcastr );
        } else {
            // We're downloading from a secure URL, but we don't want to validate the root cert.
            client.setInsecure();
        }
        http.begin( client, UpdateURL );
    } else {
        http.begin( UpdateURL );
    }

    // TODO: add more watched headers e.g. Authorization: Signature keyId="rsa-key-1",algorithm="rsa-sha256",signature="Base64(RSA-SHA256(signing string))"
    const char* get_headers[] = { "Content-Length", "Content-type" };
    http.collectHeaders( get_headers, 2 );

    int httpCode = http.GET();

    if( httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY ) {
        contentLength = http.header( "Content-Length" ).toInt();
        String contentType = http.header( "Content-type" );
        if( contentType == "application/octet-stream" ) {
            isValidContentType = true;
        } else if( contentType == "application/gzip" ) {
            // was gzipped by the server, needs decompression
            // TODO: use gzStreamUpdater
        } else if( contentType == "application/tar+gz" ) {
            // was packaged and compressed, may contain more than one file
            // TODO: use tarGzStreamUpdater
        }
    } else {
        // Connect to webserver failed
        // May be try?
        // Probably a choppy network?
        log_i( "Connection to %s failed with httpCode %i. Please check your setup", UpdateURL, httpCode );
        // retry??
        // execOTA();
    }

       // Check what is the contentLength and if content type is `application/octet-stream`
    log_i("contentLength : %i, isValidContentType : %s", contentLength, String(isValidContentType));

    // check contentLength and content type
    if( !contentLength || !isValidContentType ) {
        Serial.println("There was no content in the http response");
        http.end();
        return;
    }

    Stream& stream = http.getStream();

    if( _check_sig ) {
        // If firmware is signed, extract signature and decrease content-length by 512 bytes for signature
        contentLength = contentLength - 512;
    }
    // Check if there is enough available space on the partition to perform the Update
    bool canBegin = Update.begin( contentLength, partition );

    if( !canBegin ) {
        Serial.println("Not enough space to begin OTA");
        http.end();
        return;
    }

    if( _ota_progress_callback ) {
        Update.onProgress( _ota_progress_callback );
    } else {
        Update.onProgress( [](size_t progress, size_t size) {
            if( progress == size || progress == 0 ) Serial.println();
            Serial.print(".");
        });
    }

    unsigned char signature[512];
    if( _check_sig ) {
        stream.readBytes( signature, 512 );
    }
    Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quiet for a while.. Patience!");
    // No activity would appear on the Serial monitor
    // So be patient. This may take 2 - 5mins to complete
    size_t written = Update.writeStream( stream );

    if (written == contentLength) {
        Serial.println("Written : " + String(written) + " successfully");
    } else {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
        // retry??
        // execOTA();
    }

    if (!Update.end()) {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      return;
    }

    if( _check_sig ) { // check signature
        if( !validate_sig( signature, contentLength ) ) {
            if( partition == U_FLASH ) { // partition was marked as bootable, but signature validation failed, undo!
                const esp_partition_t* partition = esp_ota_get_running_partition();
                esp_ota_set_boot_partition( partition );
            } else if( partition == U_SPIFFS ) { // bummer!
                // SPIFFS/LittleFS partition was already overwritten and unlike U_FLASH (has OTA0/OTA1) this can't be rolled back.
                // TODO: onValidationFail decision tree with [erase-partition, mark-unsafe, keep-as-is]
            }
            Serial.println( "Signature check failed!" );
            http.end();
            if( restart_after ) {
                Serial.println("Rebooting.");
                ESP.restart();
            }
            return;
        } else {
            log_i( "Signature OK" );
        }
    }
    Serial.println("OTA done!");
    if (Update.isFinished()) {
        Serial.println("Update successfully completed.");
        http.end();
        if( restart_after ) {
            Serial.println("Rebooting.");
            ESP.restart();
        }
        return;
    } else {
        Serial.println("Update not finished? Something went wrong!");
    }
}


bool esp32FOTA::checkJSONManifest(JsonVariant doc)
{
    if(strcmp(doc["type"].as<const char *>(), _firmwareType.c_str()) != 0) {
        log_i("Payload type in manifest %s doesn't match current firmware %s", doc["type"].as<const char *>(), _firmwareType.c_str() );
        log_i("Doesn't match type: %s", _firmwareType.c_str() );
        return false;  // Move to the next entry in the manifest
    }
    log_i("Payload type in manifest %s matches current firmware %s", doc["type"].as<const char *>(), _firmwareType.c_str() );

    semver_free(&_payloadVersion);

    if(doc["version"].is<uint16_t>()) {
        log_i("JSON version: %d (int)", doc["version"].as<uint16_t>());
        _payloadVersion = semver_t {doc["version"].as<uint16_t>()};
    } else if (doc["version"].is<const char *>()) {
        log_i("JSON version: %s (semver)", doc["version"].as<const char *>() );
        if (semver_parse(doc["version"].as<const char *>(), &_payloadVersion)) {
            log_e( "Invalid semver string received in manifest. Defaulting to 0" );
            _payloadVersion = semver_t {0};
        }
    } else {
        log_e( "Invalid semver format received in manifest. Defaulting to 0" );
        _payloadVersion = semver_t {0};
    }

    debugSemVer("Payload firmware version", &_payloadVersion );

    // Memoize some values to help with the decision tree
    bool has_url        = doc["url"].is<String>();
    uint16_t portnum    = doc["port"].as<uint16_t>();
    bool has_firmware   = doc["bin"].is<String>();
    bool has_hostname   = doc["host"].is<String>();
    bool has_port       = doc["port"].is<uint16_t>();
    bool has_tls        = has_port ? (portnum  == 443 || portnum  == 4433) : false;
    bool has_spiffs     = doc["spiffs"].is<String>();
    bool has_littlefs   = doc["littlefs"].is<String>();
    bool has_fatfs      = doc["fatfs"].is<String>();
    bool has_filesystem = has_littlefs || has_spiffs || has_fatfs;

    String protocol     = has_tls ? "https" : "http";
    String flashFSPath  =
      has_filesystem
      ? (
        has_littlefs
        ? doc["littlefs"].as<String>()
        : has_spiffs
          ? doc["spiffs"].as<String>()
          : doc["fatfs"].as<String>()
        )
      : "";

    if( has_url ) { // Basic scenario: a complete URL was provided in the JSON manifest, all other keys will be ignored
        _firmwareUrl = doc["url"].as<String>();
        if( has_hostname ) { // If the manifest provides both, warn the user
            log_w("Manifest provides both url and host - Using URL");
        }
    } else if( has_firmware && has_hostname && has_port ) { // Precise scenario: Hostname, Port and Firmware Path were provided
        _firmwareUrl = protocol + "://" + doc["host"].as<String>() + ":" + String( portnum  ) + doc["bin"].as<String>();
        if( has_filesystem ) { // More complex scenario: the manifest also provides a [spiffs, littlefs or fatfs] partition
            _flashFileSystemUrl = protocol + "://" + doc["host"].as<String>() + ":" + String( portnum  ) + flashFSPath;
        }
    } else { // JSON was malformed - no firmware target was provided
        log_e("JSON manifest was missing both 'url' and 'host'/'port'/'bin' keys");
        return false;
    }

    if (semver_compare(_payloadVersion, _firmwareVersion) == 1) {
        return true;
    }
    return false;
}


bool esp32FOTA::execHTTPcheck()
{
    String useURL;
    const char* rootcastr = nullptr;

    if (useDeviceID) {
        // String deviceID = getDeviceID() ;
        useURL = checkURL + "?id=" + getDeviceID();
    } else {
        useURL = checkURL;
    }

    log_i("Getting HTTP: %s",useURL.c_str());
    log_i("------");
    if ((WiFi.status() != WL_CONNECTED)) {  //Check the current connection status
        log_w("WiFi not connected - skipping HTTP check");
        return false;  // WiFi not connected
    }

    HTTPClient http;
    WiFiClientSecure client;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if( useURL.substring( 0, 5 ) == "https" ) {
        if (!_allow_insecure_https) {
            if( !RootCA || RootCA->size() == 0 ) {
                log_e("A strict security context has been set but no RootCA was provided");
                return false;
            }
            rootcastr = RootCA->get();
            if( !rootcastr ) {
                log_e("Unable to get RootCA, aborting");
                return false;
            }
            log_i( "Loading root_ca.pem" );
            client.setCACert( rootcastr );
        } else {
            // We're downloading from a secure port, but we don't want to validate the root cert.
            client.setInsecure();
        }
        http.begin(client, useURL);
    } else {
        http.begin(useURL);         //Specify the URL
    }

    int httpCode = http.GET();  //Make the request

    // only handle 200/301
    if( httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY ) {
        log_e("Error on HTTP request (httpCode=%i)", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();


    DynamicJsonDocument JSONResult(2048);
    DeserializationError err = deserializeJson(JSONResult, payload.c_str() );

    http.end();  // We're done with HTTP - free the resources

    if (err) {  //Check for errors in parsing
        log_e("JSON Parsing failed (err #%d):\n%s\n", err, payload.c_str() );
        return false;
    }

    if (JSONResult.is<JsonArray>()) {
        // Although improbable given the size on JSONResult buffer, we already received an array of multiple firmware types
        JsonArray arr = JSONResult.as<JsonArray>();
        for (JsonVariant JSONDocument : arr) {
            if(checkJSONManifest(JSONDocument)) {
                return true;
            }
        }
    } else if (JSONResult.is<JsonObject>()) {
        if(checkJSONManifest(JSONResult.as<JsonVariant>()))
            return true;
    }

    return false; // We didn't get a hit against the above, return false
}


String esp32FOTA::getDeviceID()
{
    char deviceid[21];
    uint64_t chipid;
    chipid = ESP.getEfuseMac();
    sprintf(deviceid, "%" PRIu64, chipid);
    String thisID(deviceid);
    return thisID;
}


// Force a firmware update regardless on current version
void esp32FOTA::forceUpdate(String firmwareURL, bool validate )
{
    _firmwareUrl = firmwareURL;
    _check_sig   = validate;
    execOTA();
}


void esp32FOTA::forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, bool validate )
{
    String firmwareURL;

    if( firmwarePort == 443 || firmwarePort == 4433 ) {
        firmwareURL = String( "https://");
    } else {
        firmwareURL = String( "http://" );
    }
    firmwareURL += firmwareHost + ":" + String( firmwarePort ) + firmwarePath;

    forceUpdate(firmwareURL, validate);
}


void esp32FOTA::forceUpdate(bool validate )
{
    // Forces an update from a manifest, ignoring the version check
    if(!execHTTPcheck()) {
        if (!_firmwareUrl) {
            // execHTTPcheck returns false if either the manifest is malformed or if the version isn't
            // an upgrade. If _firmwareUrl isn't set, however, we can't force an upgrade.
            log_e("forceUpdate called, but unable to get _firmwareUrl from manifest via execHTTPcheck.");
            return;
        }
    }
    _check_sig = validate;
    execOTA();
}


/**
 * This function return the new version of new firmware
 */
int esp32FOTA::getPayloadVersion(){
    log_w( "int esp32FOTA::getPayloadVersion() only returns the major version from semantic version strings. Use void esp32FOTA::getPayloadVersion(char * version_string) instead!" );
    return _payloadVersion.major;
}


void esp32FOTA::getPayloadVersion(char * version_string){
    semver_render(&_payloadVersion, version_string);
}


void esp32FOTA::debugSemVer( const char* label, semver_t* version ) {
   char version_no[256] = {'\0'};
   semver_render(version, version_no);
   log_i("%s: %s", label, version_no );
}

#pragma GCC diagnostic pop
