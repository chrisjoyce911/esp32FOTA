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

#include "esp32FOTA.hpp"

#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/md_internal.h"
#include "esp_ota_ops.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#define FW_SIGNATURE_LENGTH     512


static int64_t getHTTPStream( esp32FOTA* fota, int partition );
static int64_t getFileStream( esp32FOTA* fota, int partition );
static int64_t getSerialStream( esp32FOTA* fota, int partition );
static bool WiFiStatusCheck();


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



esp32FOTA::esp32FOTA(){}
esp32FOTA::~esp32FOTA(){}


esp32FOTA::esp32FOTA( FOTAConfig_t cfg )
{
    setConfig( cfg );
}


void esp32FOTA::setString( char **dest, const char* src )
{
    if( !src ) {
      log_e("Can't set string to empty source");
      return;
    }
    if( *dest != nullptr ) free( *dest );
    *dest = (char*)malloc( strlen(src)+1 );
    if( *dest == NULL ) {
      log_e("Unable to allocate %d bytes", strlen(src)+1);
      return;
    }
    //strcpy( dest, src);
    memcpy( *dest, src, strlen(src)+1);
    log_d("Assigned value: %s <= %s", *dest, src );
}



esp32FOTA::esp32FOTA(const char* firmwareType, int firmwareVersion, bool validate, bool allow_insecure_https)
{
    setString( &_cfg.name, firmwareType );
    //_cfg.name      = (char*)firmwareType;
    _cfg.sem       = SemverClass( firmwareVersion );
    _cfg.check_sig = validate;
    _cfg.unsafe    = allow_insecure_https;

    setupCryptoAssets();
    debugSemVer("Current firmware version", _cfg.sem.ver() );
}


esp32FOTA::esp32FOTA(const char* firmwareType, const char* firmwareSemanticVersion, bool validate, bool allow_insecure_https)
{
    setString( &_cfg.name, firmwareType );
    //_cfg.name      = (char*)firmwareType;
    _cfg.check_sig = validate;
    _cfg.unsafe    = allow_insecure_https;
    _cfg.sem       = SemverClass( firmwareSemanticVersion );

    setupCryptoAssets();
    debugSemVer("Current firmware version", _cfg.sem.ver() );
}



void esp32FOTA::setConfig( FOTAConfig_t cfg )
{
    setString( &_cfg.name, cfg.name );
    setString( &_cfg.manifest_url, cfg.manifest_url );

    _cfg.sem           = cfg.sem;
    _cfg.check_sig     = cfg.check_sig;
    _cfg.unsafe        = cfg.unsafe;
    _cfg.use_device_id = cfg.use_device_id;
    _cfg.root_ca       = cfg.root_ca;
    _cfg.pub_key       = cfg.pub_key;
}


void esp32FOTA::printConfig( FOTAConfig_t *cfg )
{
  if( cfg == nullptr ) cfg = &_cfg;
  Serial.printf("Name: %s\nManifest URL:%s\nSemantic Version: %d.%d.%d\nCheck Sig: %s\nUnsafe: %s\nUse Device ID: %s\nRootCA: %s\nPubKey: %s\n",
    cfg->name ? cfg->name : "None",
    cfg->manifest_url ? cfg->manifest_url : "None",
    cfg->sem.ver()->major,
    cfg->sem.ver()->minor,
    cfg->sem.ver()->patch,
    cfg->check_sig ?"true":"false",
    cfg->unsafe ?"true":"false",
    cfg->use_device_id ?"true":"false",
    cfg->root_ca ?"true":"false",
    cfg->pub_key ?"true":"false"
  );
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



void esp32FOTA::handle()
{
  if ( execHTTPcheck() ) {
      execOTA();
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

    ret = mbedtls_pk_verify( &pk, MBEDTLS_MD_SHA256, hash, mdinfo->size, (unsigned char*)signature, FW_SIGNATURE_LENGTH );

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




bool esp32FOTA::setupHTTP( const char* url )
{
    const char* rootcastr = nullptr;
    _http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    log_i("Connecting to: %s", url );
    if( String(url).startsWith("https") ) {
        if (!_cfg.unsafe) {
            if( !_cfg.root_ca ) {
                Serial.println("A strict security context has been set but no RootCA was provided");
                return false;
            }
            rootcastr = _cfg.root_ca->get();
            if( _cfg.root_ca->size() == 0 ) {
                Serial.println("A strict security context has been set but an empty RootCA was provided");
                Serial.println(rootcastr);
                return false;
            }
            if( !rootcastr ) {
                Serial.println("Unable to get RootCA, aborting");
                return false;
            }
            Serial.println("Loading root_ca.pem");
            _client.setCACert( rootcastr );
        } else {
            // We're downloading from a secure URL, but we don't want to validate the root cert.
            _client.setInsecure();
        }
        _http.begin( _client, url );
    } else {
        _http.begin( url );
    }

    if( extraHTTPHeaders.size() > 0 ) {
        // add custom headers provided by user e.g. _http.addHeader("Authorization", "Basic " + auth)
        for( const auto &header : extraHTTPHeaders ) {
            _http.addHeader(header.first, header.second);
        }
    }

    // TODO: add more watched headers e.g. Authorization: Signature keyId="rsa-key-1",algorithm="rsa-sha256",signature="Base64(RSA-SHA256(signing string))"
    const char* get_headers[] = { "Content-Length", "Content-type", "Accept-Ranges" };
    _http.collectHeaders( get_headers, sizeof(get_headers)/sizeof(const char*) );

    return true;
}




void esp32FOTA::setupStream()
{
    if(!getStream) {
        switch( _stream_type ) {
            case FOTA_FILE_STREAM:
                setStreamGetter( getFileStream );
            break;
            case FOTA_SERIAL_STREAM:
                setStreamGetter( getSerialStream );
            break;
            case  FOTA_HTTP_STREAM:
            default:
                setStreamGetter( getHTTPStream );
            break;
        }
    }

    if( !isConnected ) {
        setStatusChecker( WiFiStatusCheck );
    }
}


void esp32FOTA::stopStream()
{
    if( endStream ) { // user function provided via ::setStreamEnder( fn )
        endStream( this );
        return;
    }

    // no user function provided, apply default behaviour
    switch( _stream_type ) {
        case FOTA_FILE_STREAM:
            if( _file ) _file.close();
        break;
        case  FOTA_HTTP_STREAM:
            _http.end();
        break;
        case FOTA_SERIAL_STREAM:
        default:
        break;
    }

}



// OTA Logic
bool esp32FOTA::execOTA()
{
    setupStream();

    if( !_flashFileSystemUrl.isEmpty() ) { // a data partition was specified in the json manifest, handle the spiffs partition first
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
    bool ret = execOTA( U_FLASH, true );

    stopStream();

    return ret;
}


bool esp32FOTA::execOTA( int partition, bool restart_after )
{
    // health checks
    if( partition != U_SPIFFS && partition != U_FLASH ) {
        Serial.printf("Bad partition number: %i or empty URL, aborting\n", partition);
        return false;
    }
    if( partition == U_SPIFFS && _flashFileSystemUrl.isEmpty() ) {
        log_i("[SKIP] No spiffs/littlefs/fatfs partition was specified");
        return true; // data partition is optional, so not an error
    }
    if ( partition == U_FLASH && _firmwareUrl.isEmpty() ) {
        Serial.printf("No firmware URL, aborting\n");
        return false; // app partition is mandatory
    }

    // call getHTTPStream
    int64_t updateSize = getStream( this, partition );

    if( updateSize<=0 || _stream == nullptr ) {
        log_e("HTTP Error");
        return false;
    }

    // some network streams (e.g. Ethernet) can be laggy and need to 'breathe'
    if( ! _stream->available() ) {
        uint32_t timeout = millis() + _stream_timeout;
        while( ! _stream->available() ) {
            if( millis()>timeout ) {
                log_e("Stream timed out");
                return false;
            }
            vTaskDelay(1);
        }
    }

    mode_z = F_isZlibStream();

    log_d("compression: %s", mode_z ? "enabled" : "disabled" );

    if( _cfg.check_sig ) {
        if( mode_z ) {
            Serial.println("[ERROR] Compressed && signed image is not (yet) supported");
            return false;
        }
        if( updateSize == UPDATE_SIZE_UNKNOWN || updateSize <= FW_SIGNATURE_LENGTH ) {
            Serial.println("[ERROR] Malformed signature+fw combo");
            return false;
        }
        updateSize -= FW_SIGNATURE_LENGTH;
    }

    // If using compression, the size is implicitely unknown
    size_t fwsize = mode_z ? UPDATE_SIZE_UNKNOWN : updateSize;       // fw_size is unknown if we have a compressed image

    bool canBegin = F_canBegin();

    if( !canBegin ) {
        Serial.println("Not enough space to begin OTA, partition size mismatch?");
        F_abort();
        if( onUpdateBeginFail ) onUpdateBeginFail( partition );
        return false;
    }

    if( onOTAProgress ) {
        F_Update.onProgress( onOTAProgress );
    } else {
        F_Update.onProgress( [](size_t progress, size_t size) {
            if( progress >= size ) Serial.println();
            else if( progress > 0) Serial.print(".");
        });
    }

    unsigned char signature[FW_SIGNATURE_LENGTH];
    if( _cfg.check_sig ) {
        _stream->readBytes( signature, FW_SIGNATURE_LENGTH );
    }

    Serial.printf("Begin %s OTA. This may take 2 - 5 mins to complete. Things might be quiet for a while.. Patience!\n", partition==U_FLASH?"Firmware":"Filesystem");
    // Some activity may appear in the Serial monitor during the update (depends on Update.onProgress)
    size_t written = F_writeStream();

    if (fwsize == UPDATE_SIZE_UNKNOWN)      // match compressed fw size to responce length
        fwsize = updateSize;

    if ( written == fwsize ) {
        Serial.println("Written : " + String(written) + " successfully");
        updateSize = written; // flatten value to prevent overflow when checking signature
    } else {
        Serial.println("Written only : " + String(written) + "/" + String((int)updateSize) + ". Premature end of stream?");
        F_abort();
        return false;
    }

    if (!F_UpdateEnd()) {
        Serial.println("An Update Error Occurred. Error #: " + String(F_Update.getError()));
        return false;
    }

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
            // By doing so the ESP will NOT boot any unvalidated partition should a reset occur
            // during signature validation (crash, oom, power failure).
        }

        if( !validate_sig( _target_partition, signature, updateSize ) ) {
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
    if (F_Update.isFinished()) {

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

    _flashFileSystemUrl.clear();
    _firmwareUrl.clear();

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

    debugSemVer("Payload firmware version", _payload_sem.ver() );

    // Memoize some values to help with the decision tree
    bool has_url        = doc["url"].is<const char*>();
    bool has_firmware   = doc["bin"].is<const char*>();
    bool has_hostname   = doc["host"].is<const char*>();
    //bool has_signature  = doc["sig"].is<const char*>();
    bool has_port       = doc["port"].is<uint16_t>();
    uint16_t portnum    = doc["port"].as<uint16_t>();
    bool has_tls        = has_port ? (portnum  == 443 || portnum  == 4433) : false;
    bool has_spiffs     = doc["spiffs"].is<const char*>();
    bool has_littlefs   = doc["littlefs"].is<const char*>();
    bool has_fatfs      = doc["fatfs"].is<const char*>();
    bool has_filesystem = has_littlefs || has_spiffs || has_fatfs;

    String protocol(has_tls ? "https" : "http");
    String flashFSPath  =
      has_filesystem
      ? (
        has_littlefs
        ? doc["littlefs"].as<const char*>()
        : has_spiffs
          ? doc["spiffs"].as<const char*>()
          : doc["fatfs"].as<const char*>()
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
        _firmwareUrl = doc["url"].as<const char*>();
        if( has_hostname ) { // If the manifest provides both, warn the user
            log_w("Manifest provides both url and host - Using URL");
        }
    } else if( has_firmware && has_hostname && has_port ) { // Precise scenario: Hostname, Port and Firmware Path were provided
        _firmwareUrl = protocol + "://" + doc["host"].as<const char*>() + ":" + portnum + doc["bin"].as<const char*>();
        if( has_filesystem ) { // More complex scenario: the manifest also provides a [spiffs, littlefs or fatfs] partition
            _flashFileSystemUrl = protocol + "://" + doc["host"].as<const char*>() + ":" + portnum + flashFSPath;
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

    if( useURL.isEmpty() ) {
      Serial.println("No manifest_url provided in config, aborting!");
      return false;
    }

    // being deprecated, soon unsupported!
    // if( useURL.isEmpty() && !checkURL.isEmpty() ) {
    //     Serial.println("checkURL will soon be unsupported, use FOTAConfig_t::manifest_url instead!!");
    //     useURL = checkURL;
    // }

    // // being deprecated, soon unsupported!
    // if( useDeviceID ) {
    //     Serial.println("useDeviceID will soon be unsupported, use FOTAConfig_t::use_device_id instead!!");
    //     _cfg.use_device_id = useDeviceID;
    // }

    if (_cfg.use_device_id) {
        // URL may already have GET values
        String argseparator = (useURL.indexOf('?') != -1 ) ? "&" : "?";
        useURL += argseparator + "id=" + getDeviceID();
    }

    if ( isConnected && !isConnected() ) { // Check the current connection status
        log_i("Connection check requested but network not ready - skipping");
        return false;  // WiFi not connected
    }

    log_i("Getting HTTP: %s", useURL.c_str());

    if(! setupHTTP( useURL.c_str() ) ) {
      log_e("Unable to setup http, aborting!");
      return false;
    }

    int httpCode = _http.GET();  //Make the request

    // only handle 200/301, fail on everything else
    if( httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY ) {
        // This error may be a false positive or a consequence of the network being disconnected.
        // Since the network is controlled from outside this class, only significant error messages are reported.
        if( httpCode > 0 ) {
            Serial.printf("Error on HTTP request (httpCode=%i)\n", httpCode);
        } else {
            log_d("Unknown HTTP response");
        }
        _http.end();
        return false;
    }

    // TODO: use payload.length() to speculate on JSONResult buffer size
    #define JSON_FW_BUFF_SIZE 2048
    DynamicJsonDocument JSONResult( JSON_FW_BUFF_SIZE );
    DeserializationError err = deserializeJson( JSONResult, _http.getStream() );

    if (err) {  // Check for errors in parsing, or JSON length may exceed buffer size
        Serial.printf("JSON Parsing failed (%s, in=%d bytes, buff=%d bytes):\n", err.c_str(), _http.getSize(), JSON_FW_BUFF_SIZE );
        return false;
    }

    _http.end();  // We're done with HTTP - free the resources

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
void esp32FOTA::forceUpdate(const char* firmwareURL, bool validate )
{
    _firmwareUrl = firmwareURL;
    _cfg.check_sig = validate;
    execOTA();
}


void esp32FOTA::forceUpdate(const char* firmwareHost, uint16_t firmwarePort, const char*  firmwarePath, bool validate )
{
    static String firmwareURL("http");
    if ( firmwarePort == 443 || firmwarePort == 4433 ) firmwareURL += "s";
    firmwareURL += String(firmwareHost);
    firmwareURL += ":";
    firmwareURL += String(firmwarePort);
    firmwareURL += firmwarePath;
    forceUpdate( firmwareURL.c_str(), validate );
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






static int64_t getHTTPStream( esp32FOTA* fota, int partition )
{

    const char* url = partition==U_SPIFFS ? fota->getFlashFS_URL() : fota->getFirmwareURL();

    Serial.printf("Opening item %s\n", url );

    if(! fota->setupHTTP( url ) ) { // certs
        log_e("unable to setup http, aborting!");
        return -1;
    }

    int64_t updateSize = 0;
    int httpCode = fota->getHTTPCLient()->GET();
    String contentType;

    fota->setFotaStream( nullptr );

    if( httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY ) {
        updateSize = fota->getHTTPCLient()->getSize();
        contentType = fota->getHTTPCLient()->header( "Content-type" );
        String acceptRange = fota->getHTTPCLient()->header( "Accept-Ranges" );
        if( acceptRange == "bytes" ) {
            Serial.println("This server supports resume!" );
        } else {
            Serial.println("This server does not support resume!" );
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
                    Serial.printf("Server responded with HTTP Status '%i'. Please check your setup\n", httpCode );
                } else {
                    log_d("Unknown HTTP response");
                }
            break;
        }

        return -1;
    }

    // TODO: Not all streams respond with a content length.
    // TODO: Set updateSize to UPDATE_SIZE_UNKNOWN when content type is valid.

    // check updateSize and content type
    if( updateSize<=0 ) {
        Serial.printf("There was no content in the http response: (length: %" PRId64 ", contentType: %s)\n", updateSize, contentType.c_str());
        return -1;
    }

    log_d("updateSize : %" PRId64 ", contentType: %s", updateSize, contentType.c_str());

    fota->setFotaStream( fota->getHTTPCLient()->getStreamPtr() );

    return updateSize;
}



static int64_t getFileStream( esp32FOTA* fota, int partition)
{
    fs::FS* fs = fota->getFotaFS();

    if(!fs ) {
      Serial.println("[ERROR] No filesystem defined, use ::setCertFileSystem( &SD ) ");
      return -1;
    }

    const char* path = partition==U_SPIFFS ? fota->getFlashFS_URL() : fota->getFirmwareURL();
    Serial.printf("Opening item %s\n", path );

    fs::File* file = (fs::File*)fota->getFotaStreamPtr();
    *file = fs->open( path );

    if(! file ) {
        log_e("unable to access filesystem, aborting!");
        return -1;
    }

    int64_t updateSize = file->size();

    // check updateSize and content type
    if( !updateSize ) {
        Serial.println("[ERROR] Empty file");
        file->close();
        fota->setFotaStream( nullptr );
        return -1;
    }

    log_d("updateSize : %i", updateSize);

    return updateSize;
}



static int64_t getSerialStream( esp32FOTA* fota, int partition)
{
    return -1;
}



static bool WiFiStatusCheck()
{
    return (WiFi.status() == WL_CONNECTED);
}

/*
static bool EthernetStatusCheck()
{
    return eth_connected;
}
*/



#pragma GCC diagnostic pop
