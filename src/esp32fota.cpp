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
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"


SemverClass::SemverClass( const char* version )
{
  assert(version);
  if (semver_parse_version(version, &_ver)) {
      Serial.printf( "Invalid semver string '%s' passed to constructor. Defaulting to 0\n", version );
      _ver = semver_t{0,0,0};
  }
}

SemverClass::SemverClass( int major, int minor, int patch )
{
    _ver = semver_t{major, minor, patch};
}

semver_t* SemverClass::ver()
{
    return &_ver;

}




// Filesystem helper for signature check and pem validation
// This is abstracted away to allow storage alternatives such
// as PROGMEM, SD, SPIFFS, LittleFS or FatFS

bool CryptoFileAsset::fs_read_file()
{
    File file = fs->open( path );
    // if( file->size() > ESP.getFreeHeap() ) return false;
    if( !file ) {
        Serial.printf( "Failed to open %s for reading\n", path );
        return false;
    }
    contents = ""; // make sure the output bucket is empty
    len = file.size()+1;
    while( file.available() ) {
        contents.push_back( file.read() );
    }
    file.close();
    return len>0;
}


size_t CryptoFileAsset::size()
{
    if( len > 0 ) { // already stored, no need to access filesystem
        return len;
    }
    if( fs ) { // fetch file contents
        if( ! fs_read_file() ) {
            log_e("Invalid contents!");
            return 0;
        }
    } else {
        Serial.printf("No filesystem was set for %s!\n", path);
        return 0;
    }
    return len;
}



esp32FOTA::esp32FOTA() { }

esp32FOTA::esp32FOTA( FOTAConfig_t cfg )
{
  setConfig( cfg );
}


esp32FOTA::esp32FOTA(String firmwareType, int firmwareVersion, bool validate, bool allow_insecure_https)
{
    _cfg.name      = firmwareType.c_str();
    _cfg.sem       = SemverClass( firmwareVersion );
    _cfg.check_sig = validate;
    _cfg.unsafe    = allow_insecure_https;

    setupCryptoAssets();
    debugSemVer("Current firmware version", _cfg.sem.ver() );
}


esp32FOTA::esp32FOTA(String firmwareType, String firmwareSemanticVersion, bool validate, bool allow_insecure_https)
{
    _cfg.name      = firmwareType.c_str();
    _cfg.check_sig = validate;
    _cfg.unsafe    = allow_insecure_https;
    _cfg.sem       = SemverClass( firmwareSemanticVersion.c_str() );

    setupCryptoAssets();
    debugSemVer("Current firmware version", _cfg.sem.ver() );
}


esp32FOTA::~esp32FOTA()
{
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
        _cfg.pub_key = (CryptoAsset*)(new CryptoFileAsset( rsa_key_pub_default_path, _fs  ));
        _cfg.root_ca = (CryptoAsset*)(new CryptoFileAsset( root_ca_pem_default_path, _fs  ));
    }
}


// SHA-Verify the OTA partition after it's been written
// https://techtutorialsx.com/2018/05/10/esp32-arduino-mbed-tls-using-the-sha-256-algorithm/
// https://github.com/ARMmbed/mbedtls/blob/development/programs/pkey/rsa_verify.c
bool esp32FOTA::validate_sig( const esp_partition_t* partition, unsigned char *signature, uint32_t firmware_size )
{
    if( !partition ) {
        Serial.println( "Could not find update partition!" );
        return false;
    }
    size_t pubkeylen = _cfg.pub_key ? _cfg.pub_key->size() : 0;

    if( pubkeylen <= 1 ) {
        Serial.println("Public key empty, can't validate!");
        return false;
    }

    const char* pubkeystr = _cfg.pub_key->get();

    if( !pubkeystr ) {
        Serial.println("Unable to get public key, can't validate!");
        return false;
    }

    log_d("Creating mbedtls context");

    mbedtls_pk_context pk;
    mbedtls_md_context_t rsa;
    mbedtls_pk_init( &pk );

    log_d("Parsing public key");

    int ret;
    if( ( ret = mbedtls_pk_parse_public_key( &pk, (const unsigned char*)pubkeystr, pubkeylen ) ) != 0 ) {
        Serial.printf( "Parsing public key failed\n  ! mbedtls_pk_parse_public_key %d (%d bytes)\n%s\n", ret, pubkeylen, pubkeystr );
        return false;
    }

    if( !mbedtls_pk_can_do( &pk, MBEDTLS_PK_RSA ) ) {
        Serial.printf( "Public key is not an rsa key -0x%x\n\n", -ret );
        return false;
    }

    log_d("Initing mbedtls");

    const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type( MBEDTLS_MD_SHA256 );
    mbedtls_md_init( &rsa );
    mbedtls_md_setup( &rsa, mdinfo, 0 );
    mbedtls_md_starts( &rsa );

    int bytestoread = SPI_FLASH_SEC_SIZE;
    int bytesread = 0;
    int size = firmware_size;

    uint8_t *_buffer = (uint8_t*)malloc(SPI_FLASH_SEC_SIZE);
    if(!_buffer){
        Serial.println( "malloc failed" );
        return false;
    }

    log_d("Parsing content");

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
            Serial.println( "partitionRead failed!" );
            return false;
        }
    }

    free( _buffer );

    unsigned char *hash = (unsigned char*)malloc( mdinfo->size );
    if(!hash){
        Serial.println( "malloc failed" );
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

    Serial.println( "Validation failed, erasing the invalid partition" );
    // validation failed, overwrite the first few bytes so this partition won't boot!

    ESP.partitionEraseRange( partition, 0, ENCRYPTED_BLOCK_SIZE);

    return false;
}


// OTA Logic
bool esp32FOTA::execOTA()
{
    if( _flashFileSystemUrl != "" ) { // a data partition was specified in the json manifest, handle the spiffs partition first
        if( _fs ) { // Possible risk of overwriting certs and signatures, cancel flashing!
            Serial.println("Cowardly refusing to overwrite U_SPIFFS with "+_flashFileSystemUrl+". Use setCertFileSystem(nullptr) along with setPubKey()/setCAPem() to enable this feature.");
            return false;
        } else {
            log_i("Will check if U_SPIFFS needs updating");
            if( !execOTA( U_SPIFFS, false ) ) return false;
        }
    } else {
        log_i("This update is for U_FLASH only");
    }
    // handle the application partition and restart on success
    return execOTA( U_FLASH, true );
}


bool esp32FOTA::execOTA( int partition, bool restart_after )
{
    String UpdateURL = "";
    String PartitionLabel = "";

    switch( partition ) {
        case U_SPIFFS: // spiffs/littlefs/fatfs partition
            PartitionLabel = "data";
            if( _flashFileSystemUrl == "" ) {
                log_i("[SKIP] No spiffs/littlefs/fatfs partition was speficied");
                return true;
            }
            UpdateURL = _flashFileSystemUrl;
        break;
        case U_FLASH: // app partition (default)
        default:
            PartitionLabel = "app" + String( partition );
            partition = U_FLASH;
            UpdateURL = _firmwareUrl;
        break;
    }

    size_t contentLength = 0;
    bool isValidContentType = false;
    const char* rootcastr = nullptr;

    HTTPClient http;
    WiFiClientSecure client;
    //http.setConnectTimeout( 1000 );
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    log_i("Connecting to: %s\r\n", UpdateURL.c_str() );
    if( UpdateURL.substring( 0, 5 ) == "https" ) {
        if (!_cfg.unsafe) {
            if( !_cfg.root_ca ) {
                Serial.println("A strict security context has been set for "+PartitionLabel+" partition but no RootCA was provided");
                return false;
            }
            rootcastr = _cfg.root_ca->get();
            if( _cfg.root_ca->size() == 0 ) {
                Serial.println("A strict security context has been set for "+PartitionLabel+" partition but an empty RootCA was provided");
                Serial.println(rootcastr);
                return false;
            }
            if( !rootcastr ) {
                Serial.println("Unable to get RootCA for "+PartitionLabel+", aborting");
                return false;
            }
            Serial.println("Loading root_ca.pem");
            client.setCACert( rootcastr );
        } else {
            // We're downloading from a secure URL, but we don't want to validate the root cert.
            client.setInsecure();
        }
        http.begin( client, UpdateURL );
    } else {
        http.begin( UpdateURL );
    }

    if( extraHTTPHeaders.size() > 0 ) {
        // add custom headers provided by user e.g. http.addHeader("Authorization", "Basic " + auth)
        for( const auto &header : extraHTTPHeaders ) {
            http.addHeader(header.first, header.second);
        }
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
        switch( httpCode ) {
            // 1xx = Hold on
            // 2xx = Here you go
            // 3xx = Go away
            // 4xx = You fucked up
            // 5xx = I fucked up

            case 204: log_e("Status: 204 (No contents), "); break;
            case 401: log_e("Status: 401 (Unauthorized), check setExtraHTTPHeader() values"); break;
            case 403: log_e("Status: 403 (Forbidden), check path on webserver?"); break;
            case 404: log_e("Status: 404 (Not Found), also a palindrom, check path in manifest?"); break;
            case 418: log_e("Status: 418 (I'm a teapot), Brit alert!"); break;
            case 429: log_e("Status: 429 (Too many requests), throttle things down?"); break;
            case 500: log_e("Status: 500 (Internal Server Error), you broke the webs!"); break;
            default:
                // This error may be a false positive or a consequence of the network being disconnected.
                // Since the network is controlled from outside this class, only significant error messages are reported.
                if( httpCode > 0 ) {
                    Serial.printf("Server responded with HTTP Status '%i' when calling url '%s'. Please check your setup\n", httpCode, UpdateURL.c_str() );
                } else {
                    log_d("Unknown HTTP response");
                }
            break;
        }

        http.end();
        return false;
    }

    // TODO: Not all streams respond with a content length.
    // TODO: Set contentLength to UPDATE_SIZE_UNKNOWN when content type is valid.

    // check contentLength and content type
    if( !contentLength || !isValidContentType ) {
        Serial.printf("There was no content in the http response: (length: %i, valid: %s)\n", contentLength, isValidContentType?"true":"false");
        http.end();
        return false;
    }

    log_d("contentLength : %i, isValidContentType : %s", contentLength, String(isValidContentType));

    if( _cfg.check_sig && contentLength != UPDATE_SIZE_UNKNOWN ) {
        // If firmware is signed, extract signature and decrease content-length by 512 bytes for signature
        contentLength -= 512;
    }
    // Check if there is enough available space on the partition to perform the Update
    bool canBegin = Update.begin( contentLength, partition );

    if( !canBegin ) {
        Serial.println("Not enough space to begin OTA, partition size mismatch?");
        http.end();
        if( onUpdateBeginFail ) onUpdateBeginFail( partition );
        return false;
    }

    if( onOTAProgress ) {
        Update.onProgress( onOTAProgress );
    } else {
        Update.onProgress( [](size_t progress, size_t size) {
            if( progress >= size ) Serial.println();
            else if( progress > 0) Serial.print(".");
        });
    }

    Stream& stream = http.getStream();

    unsigned char signature[512];
    if( _cfg.check_sig ) {
        stream.readBytes( signature, 512 );
    }
    Serial.printf("Begin %s OTA. This may take 2 - 5 mins to complete. Things might be quiet for a while.. Patience!\n", partition==U_FLASH?"Firmware":"Filesystem");
    // Some activity may appear in the Serial monitor during the update (depends on Update.onProgress).
    // This may take 2 - 5mins to complete
    size_t written = Update.writeStream( stream );

    if ( written == contentLength) {
        Serial.println("Written : " + String(written) + " successfully");
    } else if ( contentLength == UPDATE_SIZE_UNKNOWN ) {
        Serial.println("Written : " + String(written) + " successfully");
        contentLength = written; // populate value as it was unknown until now
    } else {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Premature end of stream?");
        contentLength = written; // flatten value to prevent overflow when checking signature
    }

    if (!Update.end()) {
        Serial.println("An Update Error Occurred. Error #: " + String(Update.getError()));
        // #define UPDATE_ERROR_OK                 (0)
        // #define UPDATE_ERROR_WRITE              (1)
        // #define UPDATE_ERROR_ERASE              (2)
        // #define UPDATE_ERROR_READ               (3)
        // #define UPDATE_ERROR_SPACE              (4)
        // #define UPDATE_ERROR_SIZE               (5)
        // #define UPDATE_ERROR_STREAM             (6)
        // #define UPDATE_ERROR_MD5                (7)
        // #define UPDATE_ERROR_MAGIC_BYTE         (8)
        // #define UPDATE_ERROR_ACTIVATE           (9)
        // #define UPDATE_ERROR_NO_PARTITION       (10)
        // #define UPDATE_ERROR_BAD_ARGUMENT       (11)
        // #define UPDATE_ERROR_ABORT              (12)
        return false;
    }

    http.end();

    if( onUpdateEnd ) onUpdateEnd( partition );

    if( _cfg.check_sig ) { // check signature

        Serial.printf("Checking partition %d to validate\n", partition);

        getPartition( partition ); // updated partition => '_target_partition' pointer

        #define CHECK_SIG_ERROR_PARTITION_NOT_FOUND -1
        #define CHECK_SIG_ERROR_VALIDATION_FAILED   -2

        if( !_target_partition ) {
            Serial.println("Can't access partition #%i to check signature!");
            if( onUpdateCheckFail ) onUpdateCheckFail( partition, CHECK_SIG_ERROR_PARTITION_NOT_FOUND );
            return false;
        }

        Serial.printf("Checking signature for partition %d...\n", partition);

        const esp_partition_t* running_partition = esp_ota_get_running_partition();

        if( partition == U_FLASH ) {
            // /!\ An OTA partition is automatically set as bootable after being successfully
            // flashed by the Update library.
            // Since we want to validate before enabling the partition, we need to cancel that
            // by temporarily reassigning the bootable flag to the running-partition instead
            // of the next-partition.
            esp_ota_set_boot_partition( running_partition );
            // By doing so the ESP will NOT boot any unvalidated partition should a crash occur
            // during signature validation.
        }

        if( !validate_sig( _target_partition, signature, contentLength ) ) {
            // erase partition
            esp_partition_erase_range( _target_partition, _target_partition->address, _target_partition->size );

            if( onUpdateCheckFail ) onUpdateCheckFail( partition, CHECK_SIG_ERROR_VALIDATION_FAILED );

            Serial.println("Signature check failed!");
            if( restart_after ) {
                Serial.println("Rebooting.");
                ESP.restart();
            }
            return false;
        } else {
            Serial.println("Signature check successful!");
            if( partition == U_FLASH ) {
                // Set updated partition as bootable now that it's been verified
                esp_ota_set_boot_partition( _target_partition );
            }
        }
    }
    //Serial.println("OTA Update complete!");
    if (Update.isFinished()) {

        if( onUpdateFinished ) onUpdateFinished( partition, restart_after );

        Serial.println("Update successfully completed.");
        if( restart_after ) {
            Serial.println("Rebooting.");
            ESP.restart();
        }
        return true;
    } else {
        Serial.println("Update not finished! Something went wrong!");
    }
    return false;
}


void esp32FOTA::getPartition( int update_partition )
{
    _target_partition = nullptr;
    if( update_partition == U_FLASH ) {
        // select the last-updated OTA partition
        _target_partition = esp_ota_get_next_update_partition(NULL);
    } else if( update_partition == U_SPIFFS ) {
        // iterate through partitions table to find the spiffs/littlefs partition
        esp_partition_iterator_t iterator = esp_partition_find( ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL );
        while( iterator != NULL ) {
            _target_partition = (esp_partition_t *)esp_partition_get( iterator );
            iterator = esp_partition_next( iterator );
        }
        esp_partition_iterator_release( iterator );
    } else {
        // wut ?
        Serial.printf("Unhandled partition type #%i, must be one of U_FLASH / U_SPIFFS\n", update_partition );
    }
}



bool esp32FOTA::checkJSONManifest(JsonVariant doc)
{
    if(strcmp(doc["type"].as<const char *>(), _cfg.name) != 0) {
        log_d("Payload type in manifest %s doesn't match current firmware %s", doc["type"].as<const char *>(), _cfg.name );
        log_d("Doesn't match type: %s", _cfg.name );
        return false;  // Move to the next entry in the manifest
    }
    log_i("Payload type in manifest %s matches current firmware %s", doc["type"].as<const char *>(), _cfg.name );

    _flashFileSystemUrl = "";
    _firmwareUrl        = "";

    if(doc["version"].is<uint16_t>()) {
        uint16_t v = doc["version"].as<uint16_t>();
        log_d("JSON version: %d (int)", v);
        _payload_sem = SemverClass(v);
    } else if (doc["version"].is<const char *>()) {
        const char* c = doc["version"].as<const char *>();
        _payload_sem = SemverClass(c);
        log_d("JSON version: %s (semver)", c );
    } else {
        log_e( "Invalid semver format received in manifest. Defaulting to 0" );
        _payload_sem = SemverClass(0);
    }

    //debugSemVer("Payload firmware version", &_payloadVersion );
    debugSemVer("Payload firmware version", _payload_sem.ver() );

    // Memoize some values to help with the decision tree
    bool has_url        = doc.containsKey("url") && doc["url"].is<String>();
    bool has_firmware   = doc.containsKey("bin") && doc["bin"].is<String>();
    bool has_hostname   = doc.containsKey("host") && doc["host"].is<String>();
    bool has_port       = doc.containsKey("port") && doc["port"].is<uint16_t>();
    uint16_t portnum    = has_port ? doc["port"].as<uint16_t>() : 0;
    bool has_tls        = has_port ? (portnum  == 443 || portnum  == 4433) : false;
    bool has_spiffs     = doc.containsKey("spiffs") && doc["spiffs"].is<String>();
    bool has_littlefs   = doc.containsKey("littlefs") && doc["littlefs"].is<String>();
    bool has_fatfs      = doc.containsKey("fatfs") && doc["fatfs"].is<String>();
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

    log_i("JSON manifest provided keys: url=%s, host: %s, port: %s, bin: %s, fs: [%s]",
        has_url?"true":"false",
        has_hostname?"true":"false",
        has_port?"true":"false",
        has_firmware?"true":"false",
        flashFSPath.c_str()
    );

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
        log_e("JSON manifest was missing one of the required keys :(" );
        serializeJsonPretty(doc, Serial);
        return false;
    }

    if (semver_compare(*_payload_sem.ver(), *_cfg.sem.ver()) == 1) {
        return true;
    }

    return false;
}


bool esp32FOTA::execHTTPcheck()
{
    String useURL = String( _cfg.manifest_url );

    // being deprecated, soon unsupported!
    if( useURL=="" && checkURL!="" ) {
        Serial.println("checkURL will soon be unsupported, use FOTAConfig_t::manifest_url instead!!");
        useURL = checkURL;
    }

    const char* rootcastr = nullptr;

    // being deprecated, soon unsupported!
    if( useDeviceID ) {
        Serial.println("useDeviceID will soon be unsupported, use FOTAConfig_t::use_device_id instead!!");
        _cfg.use_device_id = useDeviceID;
    }

    if (_cfg.use_device_id) {
        // URL may already have GET values
        String argseparator = (useURL.indexOf('?') != -1 ) ? "&" : "?";
        useURL += argseparator + "id=" + getDeviceID();
    }

    if ((WiFi.status() != WL_CONNECTED)) {  //Check the current connection status
        log_i("WiFi not connected - skipping HTTP check");
        return false;  // WiFi not connected
    }

    log_i("Getting HTTP: %s", useURL.c_str());
    log_i("------");

    HTTPClient http;
    WiFiClientSecure client;
    http.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );

    if( useURL.substring( 0, 5 ) == "https" ) {
        if( _cfg.unsafe ) {
            // We're downloading from a secure port, but we don't want to validate the root cert.
            client.setInsecure();
        } else {
            // We're downloading from a secure port, and want to validate the root cert.
            if( !_cfg.root_ca ) {
                Serial.println("A strict security context has been set to fetch json manifest, but no RootCA was provided, aborting");
                return false;
            }
            rootcastr = _cfg.root_ca->get();
            if( _cfg.root_ca->size() == 0 ) {
                Serial.println("A strict security context has been set to fetch json manifest, but an empty RootCA was provided, aborting");
                return false;
            }
            if( !rootcastr ) {
                Serial.println("Unable to get RootCA to fetch json manifest, aborting");
                return false;
            }
            Serial.println("Loading root_ca.pem");
            client.setCACert( rootcastr );
        }
        http.begin( client, useURL );
    } else {
        http.begin( useURL );
    }

    if( extraHTTPHeaders.size() > 0 ) {
        // add custom headers provided by user e.g. http.addHeader("Authorization", "Basic " + auth)
        for( const auto &header : extraHTTPHeaders ) {
            http.addHeader(header.first, header.second);
        }
    }

    int httpCode = http.GET();  //Make the request

    // only handle 200/301, fail on everything else
    if( httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY ) {
        // This error may be a false positive or a consequence of the network being disconnected.
        // Since the network is controlled from outside this class, only significant error messages are reported.
        if( httpCode > 0 ) {
            Serial.printf("Error on HTTP request (httpCode=%i)\n", httpCode);
        } else {
            log_d("Unknown HTTP response");
        }
        http.end();
        return false;
    }

    // TODO: use payload.length() to speculate on JSONResult buffer size
    #define JSON_FW_BUFF_SIZE 2048
    DynamicJsonDocument JSONResult( JSON_FW_BUFF_SIZE );

    String payload = http.getString();

    DeserializationError err = deserializeJson( JSONResult, payload.c_str() );

    http.end();  // We're done with HTTP - free the resources

    if (err) {  // Check for errors in parsing, or JSON length may exceed buffer size
        Serial.printf("JSON Parsing failed (%s, in=%d bytes, buff=%d bytes):\n%s\n", err.c_str(), payload.length(), JSON_FW_BUFF_SIZE, payload.c_str() );
        return false;
    }

    if (JSONResult.is<JsonArray>()) {
        // Although improbable given the size on JSONResult buffer, we already received an array of multiple firmware types and/or versions
        JsonArray arr = JSONResult.as<JsonArray>();
        for (JsonVariant JSONDocument : arr) {
            if(checkJSONManifest(JSONDocument)) {
                // TODO: filter "highest vs next" version number for JSON with only one firmware type but several version numbers
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
    _cfg.check_sig = validate;
    execOTA();
}


void esp32FOTA::forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, bool validate )
{
    String firmwareURL = ( firmwarePort == 443 || firmwarePort == 4433 ) ? "https" : "http";
    firmwareURL += firmwareHost + ":" + String( firmwarePort ) + firmwarePath;
    forceUpdate( firmwareURL, validate );
}


void esp32FOTA::forceUpdate(bool validate )
{
    // Forces an update from a manifest, ignoring the version check
    if(!execHTTPcheck()) {
        if (!_firmwareUrl) {
            // execHTTPcheck returns false when the manifest is malformed or when the version isn't
            // an upgrade. If _firmwareUrl isn't set we can't force an upgrade.
            Serial.println("forceUpdate called, but unable to get _firmwareUrl from manifest via execHTTPcheck.");
            return;
        }
    }
    _cfg.check_sig = validate;
    execOTA();
}


/**
 * This function return the new version of new firmware
 */
int esp32FOTA::getPayloadVersion()
{
    log_w( "This function only returns the MAJOR version. For complete depth use getPayloadVersion(char *)." );
    return _payload_sem.ver()->major;
}


void esp32FOTA::getPayloadVersion(char * version_string)
{
    semver_render( _payload_sem.ver(), version_string );
}


void esp32FOTA::debugSemVer( const char* label, semver_t* version )
{
   char version_no[256] = {'\0'};
   semver_render( version, version_no );
   log_i("%s: %s", label, version_no );
}

#pragma GCC diagnostic pop
