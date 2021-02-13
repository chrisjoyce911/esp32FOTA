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

esp32FOTA::esp32FOTA(String firwmareType, int firwmareVersion)
{
    _firwmareType = firwmareType;
    _firwmareVersion = firwmareVersion;
    useDeviceID = false;
}

static void splitHeader(String src, String &header, String &headerValue)
{
    int idx = 0;

    idx = src.indexOf(':');
    header = src.substring(0, idx);
    headerValue = src.substring(idx + 1, src.length());
    headerValue.trim();

    return;
}





// OTA Logic


void esp32FOTA::execOTA()
{
  bool hasSPIFFS = (String(_spiffs) != "");
  // flash spiffs first without restarting
  if( hasSPIFFS )
  {
    execOTA( U_SPIFFS );
  }
  // flash the app
  execOTA( U_FLASH );
  ESP.restart();
}


void esp32FOTA::execOTA( int partition )
{

    String binFilePath = "";
    int contentLength = 0;
    bool isValidContentType = false;
    bool gotHTTPStatus = false;

    switch( partition )
    {
      case U_SPIFFS: // spiffs partition
        if( String(_spiffs) == "" ) {
          Serial.println("No spiffs binary was speficied, job finished");
          return;
        }
        binFilePath = String(_spiffs);
        break;
      case U_FLASH: // app partition (default)
      default:
        partition = U_FLASH;
        binFilePath = String(_bin);
        break;
    }

    Serial.println("Connecting to: " + String(_host));
    // Connect to Webserver
    if (clientForOta.connect(_host.c_str(), _port))
    {
        // Connection Succeed.
        // Fetching the bin
        Serial.println("Fetching Bin: " + binFilePath);

        // Get the contents of the bin file
        clientForOta.print(String("GET ") + binFilePath + " HTTP/1.1\r\n" +
                     "Host: " + _host + "\r\n" +
                     "Cache-Control: no-cache\r\n" +
                     "Connection: close\r\n\r\n");

        unsigned long timeout = millis();
        while (clientForOta.available() == 0)
        {
            if (millis() - timeout > 5000)
            {
                Serial.println("Client Timeout !");
                clientForOta.stop();
                return;
            }
        }

        while (clientForOta.available())
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

            // Check if the HTTP Response is 200
            // else break and Exit Update
            if (line.startsWith("HTTP/1.1"))
            {
                if (line.indexOf("200") < 0)
                {
                    Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
                    clientForOta.stop();
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
                Serial.println("Got " + String(contentLength) + " bytes from server");
                continue;
            }

            // Next, the content type
            if (header.equalsIgnoreCase("Content-type"))
            {
                String contentType = headerValue;
                Serial.println("Got " + contentType + " payload.");
                if (contentType == "application/octet-stream")
                {
                    isValidContentType = true;
                }
            }
        }
    }
    else
    {
        // Connect to webserver failed
        // May be try?
        // Probably a choppy network?
        Serial.println("Connection to " + String(_host) + " failed. Please check your setup");
        // retry??
        // execOTA();
    }



    // Check what is the contentLength and if content type is `application/octet-stream`
    Serial.println("contentLength : " + String(contentLength) + ", isValidContentType : " + String(isValidContentType));

    // check contentLength and content type
    if (contentLength && isValidContentType)
    {
        // Check if there is enough to OTA Update
        bool canBegin = Update.begin(contentLength, partition);

        // If yes, begin
        if (canBegin)
        {
            Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
            // No activity would appear on the Serial monitor
            // So be patient. This may take 2 - 5mins to complete
            size_t written = Update.writeStream(clientForOta);

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
            // not enough space to begin OTA
            // Understand the partitions and
            // space availability
            Serial.println("Not enough space to begin OTA");
            clientForOta.flush();
        }
    }
    else
    {
        Serial.println("There was no content in the response");
        clientForOta.flush();
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

    Serial.println("Getting HTTP");
    Serial.println(useURL);
    Serial.println("------");
    if ((WiFi.status() == WL_CONNECTED))
    { //Check the current connection status

        HTTPClient http;

        http.begin(useURL);        //Specify the URL
        int httpCode = http.GET(); //Make the request

        if (httpCode == 200)
        { //Check is a file was returned

            String payload = http.getString();

            int str_len = payload.length() + 1;
            char JSONMessage[str_len];
            payload.toCharArray(JSONMessage, str_len);

            StaticJsonDocument<300> JSONDocument; //Memory pool
            DeserializationError err = deserializeJson(JSONDocument, JSONMessage);

            if (err)
            { //Check for errors in parsing
                Serial.println("Parsing failed");
                delay(5000);
                return false;
            }

            const char *pltype   = JSONDocument["type"];
            int plversion = JSONDocument["version"];
            const char *plhost   = JSONDocument["host"];
            _port = JSONDocument["port"];
            const char *plbin    = JSONDocument["bin"];
            const char *plspiffs = JSONDocument["spiffs"];

            String jshost(plhost);
            String jsbin(plbin);
            String jsspiffs(plspiffs);

            _host   = jshost;
            _bin    = jsbin;
            _spiffs = jsspiffs;

            String fwtype(pltype);

            if (plversion > _firwmareVersion && fwtype == _firwmareType)
            {
                return true;
            }
            else
            {
                return false;
            }
        }

        else
        {
            Serial.println("Error on HTTP request");
            return false;
        }

        http.end(); //Free the resources
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
void esp32FOTA::forceUpdate(String firmwareHost, int firmwarePort, String firmwarePath, String spiffsPath)
{
    _host   = firmwareHost;
    _bin    = firmwarePath;
    _spiffs = spiffsPath;
    _port   = firmwarePort;
    execOTA();
}

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
    String spiffs;
    String type;
} OTADescription;

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
        //Serial.println("connected");
        clientForOta.println("GET https://" + String(_host) + destinationURL + " HTTP/1.0");
        clientForOta.println("Host: " + String(_host) + "");
        clientForOta.println("Connection: close");
        clientForOta.println();
        while (clientForOta.connected())
        {
            String line = clientForOta.readStringUntil('\n');
            if (line == "\r")
            {
                //Serial.println("headers received");
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

    String unparsedDescriptionOfFirmware = secureGetContent();

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

    description->type = JSONDocument["type"].as<String>();

    description->host    = JSONDocument["host"].as<String>();
    description->version = JSONDocument["version"].as<int>();
    description->bin     = JSONDocument["bin"].as<String>();
    description->spiffs  = JSONDocument["spiffs"].as<String>();

    clientForOta.stop();

    if (description->version > _firwmareVersion && description->type == _firwmareType)
    {
        locationOfFirmware = description->host;
        _bin    = description->bin;
        _spiffs = description->spiffs;
        return true;
    }

    return false;
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
  bool hasSPIFFS = (String(_spiffs) != "");
  // flash spiffs first without restarting
  if( hasSPIFFS )
  {
    executeOTA( U_SPIFFS );
  }
  // flash the app
  executeOTA( U_FLASH );
  ESP.restart();
}


void secureEsp32FOTA::executeOTA( int partition )
{
    String binFilePath = "";

    switch( partition )
    {
      case U_SPIFFS: // spiffs partition
        if( String(_spiffs) == "" ) {
          Serial.println("No spiffs binary was speficied, job finished");
          return;
        }
        binFilePath = String(_spiffs);
        break;
      case U_FLASH: // app partition (default)
      default:
        partition = U_FLASH;
        binFilePath = String(_bin);
        break;
    }

    Serial.println("location of fw " + String(locationOfFirmware) + binFilePath + " HTTP/1.0");

    bool canCorrectlyConnectToServer = prepareConnection(locationOfFirmware);
    int contentLength = 0; // fix for warning: 'contentLength' may be used uninitialized in this function
    bool isValid = false; // fix for warning: 'isValid' may be used uninitialized in this function
    bool gotHTTPStatus = false;
    if (canCorrectlyConnectToServer)
    {
        //Serial.println("ok");

        clientForOta.println("GET https://" + String(locationOfFirmware) + binFilePath + " HTTP/1.0");
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

            if (line.startsWith("HTTP/1.1"))
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
            bool canBegin = Update.begin(contentLength, partition);
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
