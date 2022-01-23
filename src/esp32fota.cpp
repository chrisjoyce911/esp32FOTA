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
            connection unless you set the allow_insecure_https option.
            Otherwise it will just use plain HTTP which will still offer to sign
            your firmware image.
*/

#include "esp32fota.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "ArduinoJson.h"
#include <FS.h>
#include <SPIFFS.h>


#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/md_internal.h"
#include "esp_ota_ops.h"

#include <WiFiClientSecure.h>

esp32FOTA::esp32FOTA(String firmwareType, int firmwareVersion, boolean validate, boolean allow_insecure_https)
{
    _firmwareType = firmwareType;
    _firmwareVersion = semver_t{firmwareVersion};
    _check_sig = validate;
    _allow_insecure_https = allow_insecure_https;
    useDeviceID = false;

    char version_no[256] = {'\0'};     // If we are passed firmwareVersion as an int, we're assuming it's a major version
    semver_render(&_firmwareVersion, version_no);
    log_i("Current firmware version: %s", version_no );

}

esp32FOTA::esp32FOTA(String firmwareType, String firmwareSemanticVersion, boolean validate, boolean allow_insecure_https)
{
    if (semver_parse(firmwareSemanticVersion.c_str(), &_firmwareVersion)) {
        log_e( "Invalid semver string %s passed to constructor. Defaulting to 0", firmwareSemanticVersion.c_str() );
        _firmwareVersion = semver_t {0};
    }

    _firmwareType = firmwareType;
    _check_sig = validate;
    _allow_insecure_https = allow_insecure_https;
    useDeviceID = false;

    char version_no[256] = {'\0'};
    semver_render(&_firmwareVersion, version_no);
    log_i("Current firmware version: %s", version_no );

}


esp32FOTA::~esp32FOTA() {
    semver_free(&_firmwareVersion);
    semver_free(&_payloadVersion);
}

// Check file signature
// https://techtutorialsx.com/2018/05/10/esp32-arduino-mbed-tls-using-the-sha-256-algorithm/
// https://github.com/ARMmbed/mbedtls/blob/development/programs/pkey/rsa_verify.c
bool esp32FOTA::validate_sig( unsigned char *signature, uint32_t firmware_size ) {
    int ret = 1;
    mbedtls_pk_context pk;
    mbedtls_md_context_t rsa;

    { // Open RSA public key:
        File public_key_file = SPIFFS.open( "/rsa_key.pub" );
        if( !public_key_file ) {
            log_e( "Failed to open rsa_key.pub for reading" );
            return false;
        }
        std::string public_key = "";
        while( public_key_file.available() ){
            public_key.push_back( public_key_file.read() );
        }
        public_key_file.close();

        mbedtls_pk_init( &pk );
        if( ( ret = mbedtls_pk_parse_public_key( &pk, (unsigned char *)public_key.c_str(), public_key.length() +1 ) ) != 0 ) {
            log_e( "Reading public key failed\n  ! mbedtls_pk_parse_public_key %d\n\n", ret );
            return false;
        }
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
    mbedtls_md_finish( &rsa, hash );

    ret = mbedtls_pk_verify( &pk, MBEDTLS_MD_SHA256,
        hash, mdinfo->size,
	(unsigned char*)signature, 512
    );

    free( hash );
    mbedtls_md_free( &rsa );
    mbedtls_pk_free( &pk );
    if( ret == 0 ) {
        return true;
    }
    // overwrite the frist few bytes so this partition won't boot!

    ESP.partitionEraseRange( partition, 0, ENCRYPTED_BLOCK_SIZE);

    return false;
}
// OTA Logic
void esp32FOTA::execOTA()
{
    int contentLength = 0;
    bool isValidContentType = false;

    HTTPClient http;
    WiFiClientSecure client;
    //http.setConnectTimeout( 1000 );
    
    log_i("Connecting to: %s\r\n", _firmwareUrl.c_str() );
    if( _firmwareUrl.substring( 0, 5 ) == "https" ) {
        if (!_allow_insecure_https) {
            // If we're downloading from secure URL use WifiClientSecure instead
            // and provide the root_ca.pem
            log_i( "Loading root_ca.pem" );
            //WiFiClientSecure client;
            File root_ca_file = SPIFFS.open( "/root_ca.pem" );
            if( !root_ca_file ) {
                log_e( "Could not open root_ca.pem" );
                return;
            }
            {
                std::string root_ca = "";
                while( root_ca_file.available() ){
                    root_ca.push_back( root_ca_file.read() );
                }
                root_ca_file.close();
                http.begin( _firmwareUrl, root_ca.c_str() );
            }
        } else {
            // We're downloading from a secure URL, but we don't want to validate the root cert.
            client.setInsecure();
            http.begin(client, _firmwareUrl);
        }
    } else {
        http.begin( _firmwareUrl );
    }

    const char* get_headers[] = { "Content-Length", "Content-type" };
    http.collectHeaders( get_headers, 2 );

    int httpCode = http.GET();
   
    if( httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY ) {
        contentLength = http.header( "Content-Length" ).toInt();
        String contentType = http.header( "Content-type" );
        if( contentType == "application/octet-stream" ) {
            isValidContentType = true;
            
        }
    } else {
        // Connect to webserver failed
        // May be try?
        // Probably a choppy network?
        log_i( "Connection to %s failed. Please check your setup", _firmwareUrl );
        // retry??
        // execOTA();
    }

       // Check what is the contentLength and if content type is `application/octet-stream`
    log_i("contentLength : %i, isValidContentType : %s", contentLength, String(isValidContentType));

    // check contentLength and content type
    if( contentLength && isValidContentType ) {
        WiFiClient& client = http.getStream();

        if( _check_sig ) {
           // If firmware is signed, extract signature and decrease content-length by 512 bytes for signature
           contentLength = contentLength - 512;
        }
        // Check if there is enough to OTA Update
        bool canBegin = Update.begin(contentLength);

        // If yes, begin
        if( canBegin ) {
            unsigned char signature[512];
            if( _check_sig ) {
               client.readBytes( signature, 512 );
            }
            Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quiet for a while.. Patience!");
            // No activity would appear on the Serial monitor
            // So be patient. This may take 2 - 5mins to complete
            size_t written = Update.writeStream(client);

            if (written == contentLength)
            {
                Serial.println("Written : " + String(written) + " successfully");
            }
            else
            {
                Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
                // retry??
                // execOTA();
            }

            if (Update.end())
            {
                if( _check_sig ) {
                   if( !validate_sig( signature, contentLength ) ) {
                       
                        const esp_partition_t* partition = esp_ota_get_running_partition();
                        esp_ota_set_boot_partition( partition );

                        log_e( "Signature check failed!" );
                        http.end();
                        ESP.restart();
                        return;
                   } else {
                        log_i( "Signature OK" );
                   }
                }
                Serial.println("OTA done!");
                if (Update.isFinished())
                {
                    Serial.println("Update successfully completed. Rebooting.");
                    http.end();
                    ESP.restart();
                }
                else
                {
                    Serial.println("Update not finished? Something went wrong!");
                }
            }
            else
            {
                Serial.println("Error Occurred. Error #: " + String(Update.getError()));
            }
        }
        else
        {
            // not enough space to begin OTA
            // Understand the partitions and
            // space availability
            Serial.println("Not enough space to begin OTA");
            http.end();
        }
    }
    else
    {
        log_e("There was no content in the response");
        http.end();
    }
}

bool esp32FOTA::execHTTPcheck()
{

    String useURL;

    if (useDeviceID)
    {
        // String deviceID = getDeviceID() ;
        useURL = checkURL + "?id=" + getDeviceID();
    }
    else
    {
        useURL = checkURL;
    }

    log_i("Getting HTTP: %s",useURL.c_str());
    log_i("------");
    if ((WiFi.status() == WL_CONNECTED)) {  //Check the current connection status

        HTTPClient http;
        WiFiClientSecure client;

        if( useURL.substring( 0, 5 ) == "https" ) {
            if (!_allow_insecure_https) {
                // If the checkURL is https load the root-CA and connect with that
                log_i( "Loading root_ca.pem" );
                File root_ca_file = SPIFFS.open( "/root_ca.pem" );
                if( !root_ca_file ) {
                    log_e( "Could not open root_ca.pem" );
                    return false;
                }
                {
                    std::string root_ca = "";
                    while( root_ca_file.available() ){
                        root_ca.push_back( root_ca_file.read() );
                    }
                    root_ca_file.close();
                    http.begin( useURL, root_ca.c_str() );
                }
            } else {
                // We're downloading from a secure port, but we don't want to validate the root cert.
                client.setInsecure();
                http.begin(client, useURL);
            }
        } else {
            http.begin(useURL);         //Specify the URL
        }
        int httpCode = http.GET();  //Make the request

        if( httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY ) {  //Check is a file was returned

            String payload = http.getString();

            int str_len = payload.length() + 1;
            char JSONMessage[str_len];
            payload.toCharArray(JSONMessage, str_len);

            StaticJsonDocument<300> JSONDocument;  //Memory pool
            DeserializationError err = deserializeJson(JSONDocument, JSONMessage);

            if (err) {  //Check for errors in parsing
                log_e("Parsing failed");
                http.end();
                return false;
            }

            const char *pltype = JSONDocument["type"];

            semver_free(&_payloadVersion);
            if(JSONDocument["version"].is<uint16_t>()) {
                log_i("JSON version: %d (int)", JSONDocument["version"].as<uint16_t>());
                _payloadVersion = semver_t {JSONDocument["version"].as<uint16_t>()};
            } else if (JSONDocument["version"].is<const char *>()) {
                log_i("JSON version: %s (semver)", JSONDocument["version"].as<const char *>() );
                if (semver_parse(JSONDocument["version"].as<const char *>(), &_payloadVersion)) {
                    log_e( "Invalid semver string received in manifest. Defaulting to 0" );
                    _payloadVersion = semver_t {0};
                }
            } else {
                log_e( "Invalid semver format received in manifest. Defaulting to 0" );
                _payloadVersion = semver_t {0};
            }

            char version_no[256] = {'\0'};
            semver_render(&_payloadVersion, version_no);
            log_i("Payload firmware version: %s", version_no );


            if(JSONDocument["url"].is<String>()) {
                // We were provided a complete URL in the JSON manifest - use it
                _firmwareUrl = JSONDocument["url"].as<String>();
                if(JSONDocument["host"].is<String>())  // If the manifest provides both, warn the user
                    log_w("Manifest provides both url and host - Using URL");
            } else if (JSONDocument["host"].is<String>() && JSONDocument["port"].is<uint16_t>() && JSONDocument["bin"].is<String>()){
                // We were provided host/port/bin format - Build the URL
                if( JSONDocument["port"].as<uint16_t>() == 443 || JSONDocument["port"].as<uint16_t>() == 4433 )
                    _firmwareUrl = String( "https://");
                else
                    _firmwareUrl = String( "http://" );

                _firmwareUrl += JSONDocument["host"].as<String>() + ":" + String( JSONDocument["port"].as<uint16_t>() ) + JSONDocument["bin"].as<String>();

            } else {
                // JSON was malformed - no firmware target was provided
                log_e("JSON manifest was missing both 'url' and 'host'/'port'/'bin' keys");
                http.end();
                return false;
            }


            String fwtype(pltype);

            if (semver_compare(_payloadVersion, _firmwareVersion) == 1 && fwtype == _firmwareType) {
                http.end();
                return true;
            }
        } else {
            log_e("Error on HTTP request");
        }
        http.end();  //Free the resources
        return false;
    }
    return false;
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
void esp32FOTA::forceUpdate(String firmwareURL, boolean validate )
{
    _firmwareUrl = firmwareURL;
    _check_sig = validate;
    execOTA();
}

void esp32FOTA::forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, boolean validate )
{
    String firmwareURL;

    if( firmwarePort == 443 || firmwarePort == 4433 )
        firmwareURL = String( "https://");
    else
        firmwareURL = String( "http://" );
    firmwareURL += firmwareHost + ":" + String( firmwarePort ) + firmwarePath;

    forceUpdate(firmwareURL, validate);
}

void esp32FOTA::forceUpdate(boolean validate )
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
