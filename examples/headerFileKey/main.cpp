/*
Date: 2022-08-24
Author: Nguyen Anh Tuan <https://github.com/tuan-karma>
Feature added: use the rsa_pub_key.h to embed the key into the fimrware. This feature helps:
- Preventing someone with physical access to your esp32-board altering the rsa_pub_key in the SPI flash memory 
hence compromising the next firmware update.
- Being able to update the rsa_key for the next version of your firmware conveniently. 

Usges:
    + Gen the rsa_key.pub as raw format from openssl. 
    + Copy and paste the raw content in rsa_key.pub into the space between a "Raw string literal" as seen above. 
    + `R"~~~(<your_rsa_raw_text_here_without_any_extra_space_or_newline)~~~"` 
    + Include the `...rsa_pub_key.h` only in your main.cpp code. And use the API as seen in this example.

Local server using python:
- `python -m http.server --help`

*/

#include <Arduino.h>
#include <esp32fota.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "ota_rsa_pub_key.h"
namespace
{
    const char *ssid = "VTCC";
    const char *pass = "vtcc40pbc";
    const char *current_version = "0.1.2";
}

esp32FOTA mOTA("m5stack_FOTA", current_version, rsa_pub_key, sizeof(rsa_pub_key), true);

void setup_wifi()
{
    delay(10);
    // LittleFS.begin(false);
    
    Serial.printf("Connecting to %s ", ssid);
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWF Connected!");
}

void setup()
{
    mOTA.checkURL = "http://10.130.0.141:8000/firmwares.json";
    Serial.begin(115200);
    Serial.println(current_version);
    setup_wifi();
}

void loop()
{
    bool updateNeeded = mOTA.execHTTPcheck();
    if (updateNeeded)
    {
        Serial.println("A newer firmware version was found --> update");
        mOTA.execOTA_internal();
    }
    delay(2000);
}