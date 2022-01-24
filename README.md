[![PlatformIO](https://github.com/chrisjoyce911/esp32FOTA/workflows/PlatformIO/badge.svg)](https://github.com/chrisjoyce911/esp32FOTA/actions/)

# esp32FOTA library for Arduino

## Purpose

A simple library to add support for Over-The-Air (OTA) updates to your project.

## Features

- [x] Web update (requires web server)
- [x] Batch firmware sync
- [x] Force firmware update [issues 8]
- [x] https support [#26][i26] ( Thanks to @fbambusi )
- [x] Signature check of downloaded firmware-image [issue 65]
- [x] https or https
- [x] Signature verification
- [x] Semantic versioning support
- [ ] Checking for update via bin headers [issue 15]

## How it works

This library tries to access a JSON file hosted on a webserver, and reviews it to decide if a newer firmware has been published, if so it will download it and install it.

There are a few things that need to be in place for an update to work.

- A webserver with the firmware information in a JSON file
- Firmware version
- Firmware type
- Firmware bin
- For https or signature check: SPIFFS with root_ca.pem (https) and rsa_key.pem (signature check)

You can supply http or https URLs to the checkURL. If you are using https, you need the root_ca.pem in your SPIFFS partition. For the actual firmware it will use https when you define port 443 or 4433. Otherwise it will use plain http.

## Usage

### Hosted JSON

This is hosted by a webserver and contains information about the latest firmware:

```json
{
    "type": "esp32-fota-http",
    "version": 2,
    "host": "192.168.0.100",
    "port": 80,
    "bin": "/fota/esp32-fota-http-2.bin",
    "check_signature": true
}
```

Version information can be either a single number or a semantic version string. Alternatively, a full URL path can be provided:

```json
{
    "type": "esp32-fota-http",
    "version": "2.5.1",
    "url": "http://192.168.0.100/fota/esp32-fota-http-2.bin",
    "check_signature": true
}
```

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

In this example a version 1  of 'esp32-fota-http' is in use, it would be updated when using the JSON example.

```cpp
#include <esp32fota.h>
#include <WiFi.h>

const char *ssid = "";
const char *password = "";

esp32FOTA esp32FOTA("esp32-fota-http", "1.0.0");

void setup()
{
  esp32FOTA.checkURL = "http://server/fota/fota.json";
  Serial.begin(115200);
  setup_wifi();
}

void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
}

void loop()
{
  bool updatedNeeded = esp32FOTA.execHTTPcheck();
  if (updatedNeeded)
  {
    esp32FOTA.execOTA();
  }
  delay(2000);
}
```
### Verified images via signature

You can now sign your firmware image with an RSA public/private key pair and have the ESP32 check if the signature is correct before
it switches over to the new image.

In order to use this feature, enable `check_signature` in `firmware.json`. Next create a key-pair to sign your firmware image:
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


[issue 15]: https://github.com/chrisjoyce911/esp32FOTA/issues/15
[issues 8]: https://github.com/chrisjoyce911/esp32FOTA/issues/8
[issue 65]: https://github.com/chrisjoyce911/esp32FOTA/issues/65


### Libraries

This relies on [semver.c by h2non](https://github.com/h2non/semver.c) for semantic versioning support. semver.c is licensed under [MIT](https://github.com/h2non/semver.c/blob/master/LICENSE).

### Thanks to 

* @nuclearcat
* @thinksilicon
* @nuclearcat 
* @hpsaturn 