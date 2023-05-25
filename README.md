[![PlatformIO](https://github.com/chrisjoyce911/esp32FOTA/workflows/PlatformIO/badge.svg)](https://github.com/chrisjoyce911/esp32FOTA/actions/)

[![arduino-library-badge](https://www.ardu-badge.com/badge/esp32FOTA.svg?)](https://www.ardu-badge.com/esp32FOTA)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/chrisjoyce911/library/esp32FOTA.svg)](https://registry.platformio.org/libraries/chrisjoyce911/esp32FOTA)



# esp32FOTA library for Arduino

## Purpose

A simple library to add support for Over-The-Air (OTA) updates to your project.

## Features

- [x] Zlib or gzip compressed firmware support
- [x] SPIFFS/LittleFS partition Update [#25], [#47], [#60], [#92]  (thanks to all participants)
- [x] Any fs::FS support (SPIFFS/LITTLEFS/SD) for cert/signature storage [#79], [#74], [#91], [#92] (thanks to all participants)
- [x] Seamless http/https
- [x] Web update (requires web server)
- [x] Batch firmware sync
- [x] Force firmware update [#8]
- [x] https support [#26] ( Thanks to @fbambusi )
- [x] Signature check of downloaded firmware-image [#65]
- [x] Signature verification
- [x] Semantic versioning support
- [ ] Checking for update via bin headers [#15]

## How it works

This library tries to access a JSON file hosted on a webserver, and reviews it to decide if a newer firmware has been published, if so it will download it and install it.

There are a few things that need to be in place for an update to work.

- A webserver with the firmware information in a JSON file
- Firmware version
- Firmware type
- Firmware bin (can optionnally be compressed with zlib or gzip)
- For https or signature check: SPIFFS with root_ca.pem (https) and rsa_key.pem (signature check)

You can supply http or https URLs. If you are using https, you need the root_ca.pem in your SPIFFS partition. For the actual firmware it will use https when you define port 443 or 4433. Otherwise it will use plain http.

## Usage

### Hosted JSON

This is hosted by a webserver and contains information about the latest firmware:

```json
{
    "type": "esp32-fota-http",
    "version": 2,
    "host": "192.168.0.100",
    "port": 80,
    "bin": "/fota/esp32-fota-http-2.bin"
}
```

Version information can be either a single number or a semantic version string. Alternatively, a full URL path can be provided:

```json
{
    "type": "esp32-fota-http",
    "version": "2.5.1",
    "url": "http://192.168.0.100/fota/esp32-fota-http-2.bin"
}
```

A single JSON file can provide information on multiple firmware types by combining them together into an array. When this is loaded, the firmware manifest with a type matching the one passed to the esp32FOTA constructor will be selected:

```json
[
   {
      "type":"esp32-fota-http",
      "version":"0.0.2",
      "url":"http://192.168.0.100/fota/esp32-fota-http-2.bin"
   },
   {
      "type":"esp32-other-hardware",
      "version":"0.0.3",
      "url":"http://192.168.0.100/fota/esp32-other-hardware.bin"
   }
]
```


A single JSON file can also contain several versions of a single firmware type:

```json
[
   {
      "type":"esp32-fota-http",
      "version":"0.0.2",
      "url":"http://192.168.0.100/fota/esp32-fota-0.0.2.bin"
   },
   {
      "type":"esp32-fota-http",
      "version":"0.0.3",
      "url":"http://192.168.0.100/fota/esp32-fota-0.0.3.bin",
      "spiffs":"http://192.168.0.100/fota/esp32-fota-0.0.3.spiffs.bin"
   }
]
```




#### Filesystem image (spiffs/littlefs)

Adding `spiffs` key to the JSON entry will end up with the filesystem being updated first, then the firmware.

Obviously don't use the filesystem you're updating to store certificates needed by the update, spiffs partition
doesn't have redundancy like OTA0/OTA1 and won't recover from a failed update without a restart and format.

```json
{
    "type": "esp32-fota-http",
    "version": 2,
    "host": "192.168.0.100",
    "port": 80,
    "bin": "/fota/esp32-fota-http-2.bin",
    "spiffs": "/fota/default_spiffs.bin"
}
```

Other accepted keys for filesystems are `spiffs`, `littlefs` and `fatfs`.
Picking one or another doesn't make any difference yet.


#### Firmware types

Types are used to compare with the current loaded firmware, this is used to make sure that when loaded, the device will still do the intended job.

As an example, a device used as a data logger should ony be updated with new versions of the data logger.

##### examples

- TTGO-T8-ESP32-Logger
- TTGO-T8-ESP32-Temp
- TTGO-T8-ESP32-Relay


### Debug

Messages depends of build level. If you pass -D CORE_DEBUG_LEVEL=3 to build flags, it enable the messages

### Sketch

In this early init example, a version 1  of 'esp32-fota-http' is in use, it would be updated when using the JSON example.

```cpp
#include <esp32FOTA.hpp>

const char *ssid = "";
const char *password = "";

esp32FOTA esp32FOTA("esp32-fota-http", "1.0.0");

const char* manifest_url = "http://server/fota/fota.json";

void setup()
{
  Serial.begin(115200);
  setup_wifi();
  esp32FOTA.setManifestURL( manifest_url );
  // esp32FOTA.useDeviceId( true ); // optionally append the device ID to the HTTP query
}

void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

void loop()
{
  esp32FOTA.handle();
  // or ...
  // bool updatedNeeded = esp32FOTA.execHTTPcheck();
  // if (updatedNeeded) {
  //   esp32FOTA.execOTA();
  // }
  delay(2000);
}
```


Late init is possible using `FOTAConfig_t`, allowing more complex configurations:

```cpp
#include <SPIFFS.h> // include filesystem *before* esp32FOTA librart
#include <esp32FOTA.hpp>

esp32FOTA FOTA;

const char* manifest_url = "http://server/fota/fota.json";
const char* fota_name = "esp32-fota-http";

// CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &SPIFFS );
// CryptoFileAsset *MyRSAKey = new CryptoFileAsset( "/rsa_key.pub", &SD );

void setup()
{
  Serial.begin( 115200 );
  setup_wifi();

  {
    auto cfg = FOTA.getConfig();
    cfg.name          = fota_name;
    cfg.manifest_url  = manifest_url;
    cfg.sem           = SemverClass( 1, 0, 0 ); // major, minor, patch
    cfg.check_sig     = false; // verify signed firmware with rsa public key
    cfg.unsafe        = true; // disable certificate check when using TLS
    //cfg.root_ca       = MyRootCA;
    //cfg.pub_key       = MyRSAKey;
    //cfg.use_device_id = false;
    FOTA.setConfig( cfg );
  }
}

void loop()
{
  esp32FOTA.handle();
  // or ...
  // bool updatedNeeded = esp32FOTA.execHTTPcheck();
  // if (updatedNeeded) {
  //   esp32FOTA.execOTA();
  // }
  delay(2000);
}

```


### Zlib/gzip support

⚠️ This feature cannot be used with signature check.


For firmwares compressed with `pigz` utility (see , file extension must be `.zz`:

```cpp
#include <flashz.hpp> // http://github.com/vortigont/esp32-flashz
#include <esp32FOTA.hpp>
```

```bash
$ pigz -9kzc esp32-fota-http-2.bin > esp32-fota-http-2.bin.zz
```

```json
{
    "type": "esp32-fota-http",
    "version": "2.5.1",
    "url": "http://192.168.0.100/fota/esp32-fota-http-2.bin.zz"
}
```


For firmwares compressed with `gzip` utility, file extension must be `.gz`

```cpp
#include <ESP32-targz.h> // http://github.com/tobozo/ESP32-targz
#include <esp32FOTA.hpp>
```

```bash
$ gzip -c esp32-fota-http-2.bin > esp32-fota-http-2.bin.gz
```

```json
{
    "type": "esp32-fota-http",
    "version": "2.5.1",
    "url": "http://192.168.0.100/fota/esp32-fota-http-2.bin.gz"
}
```






### Root Certificates

Certificates and signatures can be stored in different places: any fs::FS filesystem or progmem as const char*.

Filesystems:

```C++
CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &SPIFFS );
```

```C++
CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &LittleFS );
```

```C++
CryptoFileAsset *MyRootCA = new CryptoFileAsset( "/root_ca.pem", &SD );
```

Progmem:

```C++
const char* root_ca = R"ROOT_CA(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)ROOT_CA";

// mixed sources is possible
CryptoMemAsset  *MyRootCA = new CryptoMemAsset("Root CA", root_ca, strlen(root_ca)+1 );
CryptoFileAsset *MyPubKey = new CryptoFileAsset("RSA Key", "/rsa_key.pub", &SD);

```

Then later in the `setup()`:

```C++

const char* manifest_url = "http://server/fota/fota.json";

void setup()
{
  // (...)
  esp32FOTA.setManifestURL( manifest_url );
  esp32FOTA.setRootCA( MyRootCA );
  esp32FOTA.setPubKey( MyPubKey );
}

```


# Update callbacks


## Progress callback

Can be used to draw a progress bar e.g. on a TFT.

The callback signature is: `void my_progress_callback( size_t progress, size_t size);`, lambda functions are accepted.

Use `esp32FOTA.setProgressCb( my_progress_callback )` to attach the callback.

This method is aliased to Update.h `onProgress()` feature and defaults to printing dots in the serial console.

```C++
void my_progress_callback( size_t progress, size_t size )
{
  if( progress == size || progress == 0 ) Serial.println();
  Serial.print(".");
}

void setup()
{
  // (...)

  // usage with callback function:
  esp32FOTA.setProgressCb( my_progress_callback ) ;

  // usage with lambda function:
  esp32FOTA.setProgressCb( [](size_t progress, size_t size) {
      if( progress == size || progress == 0 ) Serial.println();
      Serial.print(".");
  });
}
```


## Update begin-fail callback

- Description: fired when Update.begin() failed
- Callback type: `void(int partition)`
- Callback setter: `setUpdateBeginFailCb( cb )`
- Usage:

```cpp
esp32FOTA.setUpdateBeginFailCb( [](int partition) {
  Serial.printf("Update could not begin with %s partition\n", partition==U_SPIFFS ? "spiffs" : "firmware" );
});
```

## Update end callback

- Description: fired after Update.end() and before signature check
- Callback type: `void(int partition)`
- Callback setter: `setUpdateEndCb( cb )`
- Usage:

```cpp
esp32FOTA.setUpdateEndCb( [](int partition) {
  Serial.printf("Update could not finish with %s partition\n", partition==U_SPIFFS ? "spiffs" : "firmware" );
});
```


## Update check-fail callback

- Description: fired when partition or signature check failed
- Callback type: `void(int partition, int update_error_code)`
- Callback setter: `setUpdateCheckFailCb( cb )`
- Usage:

```cpp
esp32FOTA.setUpdateCheckFailCb( [](int partition, int error_code) {
  Serial.printf("Update could validate %s partition (error %d)\n", partition==U_SPIFFS ? "spiffs" : "firmware", error_code );
  // error codes:
  //  -1 : partition not found
  //  -2 : validation (signature check) failed
});
```


## Update finished callback

- Description: fired update is complete
- Callback type: `void(int partition, bool needs_restart)`
- Callback setter: `setUpdateFinishedCb( cb )`
- Usage:

```cpp
esp32FOTA.setUpdateFinishedCb( [](int partition, bool restart_after) {
  Serial.printf("Update could not begin with %s partition\n", partition==U_SPIFFS ? "spiffs" : "firmware" );
  // do some stuff e.g. notify a MQTT server the update completed successfully
  if( restart_after ) {
      ESP.restart();
  }
});
```






### Verified images via signature

You can now sign your firmware image with an RSA public/private key pair and have the ESP32 check if the signature is correct before
it switches over to the new image.

In order to use this feature just set the boolean `validate` to `true` in the constructor. Next create a key-pair to sign your firmware image:
```
openssl genrsa -out priv_key.pem 4096
openssl rsa -in priv_key.pem -pubout > rsa_key.pub
```

Compile your code so you get your OTA update file (e.g. `firmware.bin`). Now it's time to create the signature:
```
# Create signature file
openssl dgst -sign priv_key.pem -keyform PEM -sha256 -out firmware.sign -binary firmware.bin

# throw it all in one file
cat firmware.sign firmware.bin > firmware.img
```

Upload `firmware.img` to your OTA server and point to it in your `firmware.json`

Last step, create an SPIFFS partition with your `rsa_key.pub` in it. The OTA update should not touch this partition during the update. You'll only need to distribute this partition once.

On the next update-check the ESP32 will download the `firmware.img` extract the first 512 bytes with the signature and check it together with the public key against the new image. If the signature check runs OK, it'll reset into the new firmware.



[#8]: https://github.com/chrisjoyce911/esp32FOTA/issues/8
[#15]: https://github.com/chrisjoyce911/esp32FOTA/issues/15
[#25]: https://github.com/chrisjoyce911/esp32FOTA/issues/25
[#26]: https://github.com/chrisjoyce911/esp32FOTA/issues/26
[#60]: https://github.com/chrisjoyce911/esp32FOTA/issues/60
[#65]: https://github.com/chrisjoyce911/esp32FOTA/issues/65
[#74]: https://github.com/chrisjoyce911/esp32FOTA/issues/74
[#47]: https://github.com/chrisjoyce911/esp32FOTA/pull/47
[#79]: https://github.com/chrisjoyce911/esp32FOTA/pull/79
[#91]: https://github.com/chrisjoyce911/esp32FOTA/pull/91
[#92]: https://github.com/chrisjoyce911/esp32FOTA/pull/92


### Libraries

This library relies on [semver.c by h2non](https://github.com/h2non/semver.c) for semantic versioning support. semver.c is licensed under [MIT](https://github.com/h2non/semver.c/blob/master/LICENSE).

Optional dependencies (zlib/gzip support):
* [esp32-flashz](https://github.com/vortigont/esp32-flashz)
* [esp32-targz](https://github.com/tobozo/ESP32-targz)


### Thanks to

* @nuclearcat
* @thinksilicon
* @tuan-karma
* @hpsaturn
* @tobozo
* @vortigont

