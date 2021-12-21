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

esp32FOTA::esp32FOTA(String firwmareType, int firwmareVersion, boolean validate )
{
    _firwmareType = firwmareType;
    _firwmareVersion = firwmareVersion;
    _check_sig = validate;
    useDeviceID = false;
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
    bool gotHTTPStatus = false;

    HTTPClient http;
    //http.setConnectTimeout( 1000 );
    
    log_i("Connecting to: %s:%i%s\r\n", _host.c_str(), _port, _bin.c_str() );
    if( _port == 443 || _port == 4433 ) {
        // If we're downloading from secure port use WifiClientSecure instead
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
            //http.begin( String( "https://" ) + _host + _bin, root_ca.c_str() );
            http.begin( String( "https://") + _host + ":" + String( _port ) + _bin, root_ca.c_str() );
        }
    } else {
        http.begin( String( "http://" ) + _host + ":" + String( _port ) + _bin );
    }

    const char* get_headers[] = { "Content-Length", "Content-type" };
    http.collectHeaders( get_headers, 2 );

    int httpCode = http.GET();
   
    if( httpCode == 200 ) {
        contentLength = http.header( "Content-Length" ).toInt();
        String contentType = http.header( "Content-type" );
        if( contentType == "application/octet-stream" ) {
            isValidContentType = true;
            
        }
    } else {
        // Connect to webserver failed
        // May be try?
        // Probably a choppy network?
        log_i( "Connection to %s failed. Please check your setup", _host );
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

    _port = 80;

    log_i("Getting HTTP: %s",useURL.c_str());
    log_i("------");
    if ((WiFi.status() == WL_CONNECTED)) {  //Check the current connection status

        HTTPClient http;

        if( useURL.substring( 0, 5 ) == "https" ) {
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
            http.begin(useURL);         //Specify the URL
        }
        int httpCode = http.GET();  //Make the request

        if (httpCode == 200) {  //Check is a file was returned

            String payload = http.getString();

            int str_len = payload.length() + 1;
            char JSONMessage[str_len];
            payload.toCharArray(JSONMessage, str_len);

            StaticJsonDocument<300> JSONDocument;  //Memory pool
            DeserializationError err = deserializeJson(JSONDocument, JSONMessage);

            if (err) {  //Check for errors in parsing
                log_e("Parsing failed");
                delay(5000);
                return false;
            }

            const char *pltype = JSONDocument["type"];
            int plversion = JSONDocument["version"];
            const char *plhost = JSONDocument["host"];
            _port = JSONDocument["port"];
            const char *plbin = JSONDocument["bin"];
            _payloadVersion = plversion;

            String jshost(plhost);
            String jsbin(plbin);

            _host = jshost;
            _bin = jsbin;

            String fwtype(pltype);

            if (plversion > _firwmareVersion && fwtype == _firwmareType) {
                http.end();
                return true;
            }
        }
        else {
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

// Force a firmware update regartless on current version
void esp32FOTA::forceUpdate(String firmwareHost, int firmwarePort, String firmwarePath, boolean validate )
{
    _host = firmwareHost;
    _bin = firmwarePath;
    _port = firmwarePort;
    _check_sig = validate;
    execOTA();
}

/**
 * This function return the new version of new firmware
 */
int esp32FOTA::getPayloadVersion(){
    return _payloadVersion;
}
