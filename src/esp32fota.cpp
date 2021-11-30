/*
   esp32 firmware OTA
   Date: December 2018
   Author: Chris Joyce <https://github.com/chrisjoyce911/esp32FOTA/esp32FOTA>
   Purpose: Perform an OTA update from a bin located on a webserver (HTTP Only)
*/

#include "esp32fota.h"
#include "Arduino.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "ArduinoJson.h"

#include <WiFiClientSecure.h>


//=============================================================================
//=======================UPDATE OVER HTTPS=====================================
//=============================================================================

/*
This structure describes the characteristics of a firmware, namely:
the host where it is located
the path of the actual bin file 
the port where it can be retrieved
the type of target device
the version of the firmware
*/
typedef struct
{
    int version;
    int port;
    String host;
    String bin;
    String type;
} OTADescription;

static void splitHeader(String src, String &header, String &headerValue)
{
    int idx = 0;

    idx = src.indexOf(':');
    header = src.substring(0, idx);
    headerValue = src.substring(idx + 1, src.length());
    headerValue.trim();

    return;
}


/*
The constructor sets only the firmware type and its versions, as these two 
parameters are hardcoded in the device. The other parameters have to be set 
separately.
*/
secureEsp32FOTA::secureEsp32FOTA(String firwmareType, int firwmareVersion)
{
    _firwmareType = firwmareType;
    _firwmareVersion = firwmareVersion;
}

/*
This function initializes the connection with the server, verifying that it is
compliant with the provided certificate.
*/
bool secureEsp32FOTA::prepareConnection(String destinationServer)
{
    char *certificate = _certificate;
    clientForOta.setCACert(certificate);
    if (clientForOta.connect(destinationServer.c_str(), _securePort))
    {
        return true;
    }
    return false;
}

/*
This function performs a GET request to fetch a resource over HTTPS.
It makes use of the attributes of the class, that have to be already set.
In particular, it makes use of the certificate, the descriptionOfFirmwareURL, 
the host.
*/
String secureEsp32FOTA::secureGetContent()
{
    String destinationURL = _descriptionOfFirmwareURL;

    bool canConnectToServer = prepareConnection(_host);
    if (canConnectToServer)
    {
        Serial.println("connected");
        clientForOta.println("GET https://" + String(_host) + destinationURL + " HTTP/1.0");
        clientForOta.println("Host: " + String(_host) + "");
        clientForOta.println("Connection: close");
        clientForOta.println();

        Serial.println("GET https://" + String(_host) + destinationURL + " HTTP/1.0");
        Serial.println("Host: " + String(_host) + "");
        Serial.println("Connection: close");

        while (clientForOta.connected())
        {
            String line = clientForOta.readStringUntil('\n');
            if (line == "\r")
            {
                Serial.println("headers received");
                break;
            }
        }

        String partialContentOfHTTPSResponse = "";
        while (clientForOta.available())
        {
            char c = clientForOta.read();
            partialContentOfHTTPSResponse += c;
        }
        clientForOta.stop();
        Serial.println("return partialContentOfHTTPSResponse");
        return partialContentOfHTTPSResponse;
    }
    clientForOta.stop();
    return "";
}

/*
This function checks whether it is necessary to perform an OTA update.
It fetches the description of a firmware from the host, reading descriptionOfFirmwareURL.
In case the version of the fetched firmware is greater than the current version, and 
the content has the right format, it returns true, and it modifies the attributes 
of secureEsp32FOTA accordingly.
*/
bool secureEsp32FOTA::execHTTPSCheck()
{
    String destinationUrl = _descriptionOfFirmwareURL;
    OTADescription obj;
    OTADescription *description = &obj;

    Serial.println("Get DescriptionOfFirmware");

    String unparsedDescriptionOfFirmware = secureGetContent();

    Serial.println("DescriptionOfFirmware");
    Serial.println(DescriptionOfFirmware);


    int str_len = unparsedDescriptionOfFirmware.length() + 1;
    char JSONMessage[str_len];
    unparsedDescriptionOfFirmware.toCharArray(JSONMessage, str_len);

    StaticJsonDocument<300> JSONDocument; //Memory pool
    DeserializationError err = deserializeJson(JSONDocument, JSONMessage);

    if (err)
    { //Check for errors in parsing
        Serial.println("Parsing failed");
        delay(5000);
        return false;
    }

    Serial.println("JSONDocument");
    Serial.println(JSONDocument);


    description->type = JSONDocument["type"].as<String>();

    description->host = JSONDocument["host"].as<String>();
    description->version = JSONDocument["version"].as<int>();
    description->bin = JSONDocument["bin"].as<String>();
    _payloadVersion = description->version;

    clientForOta.stop();

    Serial.println("description->host");
    Serial.println(description->host);

    Serial.println("description->type");
    Serial.println(description->type);

    Serial.println("description->version");
    Serial.println(description->version);


    Serial.println("description->bin");
    Serial.println(description->bin);


    if (description->version > _firwmareVersion && description->type == _firwmareType)
    {
        locationOfFirmware = description->host;
        _bin = description->bin;
        return true;
    }

    return false;
}

/**
 * This function return the new version of new firmware
 */
int secureEsp32FOTA::getPayloadVersion() {
    return _payloadVersion;
}

/*
This function checks whether the content of a response is a firmware.
*/
bool secureEsp32FOTA::isValidContentType(String contentType)
{
    if (contentType == "application/octet-stream")
    {
        return true;
    }

    return false;
}

/*
This function launches an OTA, using the attributes of the class that describe it:
server, url and certificate.
*/

void secureEsp32FOTA::executeOTA()
{
    Serial.println("location of fw " + String(locationOfFirmware) + _bin + " HTTP/1.0");

    bool canCorrectlyConnectToServer = prepareConnection(locationOfFirmware);
    int contentLength = 0;
    bool isValid = false;
    bool gotHTTPStatus = false;
    if (canCorrectlyConnectToServer)
    {
        //Serial.println("ok");

        clientForOta.println("GET https://" + String(locationOfFirmware) + _bin + " HTTP/1.0");
        clientForOta.println("Host: " + String(locationOfFirmware) + "");
        clientForOta.println("Connection: close");
        clientForOta.println();

        while (clientForOta.connected())
        {
            String header, headerValue;
            // read line till /n
            String line = clientForOta.readStringUntil('\n');
            // remove space, to check if the line is end of headers
            line.trim();

            if (!line.length())
            {
                //headers ended
                break; // and get the OTA started
            }

            if (line.startsWith("HTTP/1.0") || line.startsWith("HTTP/1.1"))
            {
                if (line.indexOf("200") < 0)
                {
                    //Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
                    break;
                }
                gotHTTPStatus = true;
            }

            if (false == gotHTTPStatus)
            {
                continue;
            }

            splitHeader(line, header, headerValue);
            // extract headers here
            // Start with content length
            if (header.equalsIgnoreCase("Content-Length"))
            {
                contentLength = headerValue.toInt();
                continue;
            }

            // Next, the content type
            if (header.equalsIgnoreCase("Content-Type"))
            {
                isValid = isValidContentType(headerValue);
            }
        }

        if (contentLength && isValid)
        {
            //Serial.println("beginn");
            bool canBegin = Update.begin(contentLength);
            if (canBegin)
            {
                //Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");

                size_t written = Update.writeStream(clientForOta);

                if (written == contentLength)
                {
                    //Serial.println("Written : " + String(written) + " successfully");
                }
                else
                {
                    //Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
                }

                if (Update.end())
                {
                    Serial.println("OTA done!");
                    if (Update.isFinished())
                    {
                        Serial.println("Update successfully completed. Rebooting.");
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
                Serial.println(" could not begin");
            }
        }
        else
        {
            Serial.println("Content invalid");
        }
    }
    else
    {
        Serial.println("Generic error");
    }
}
