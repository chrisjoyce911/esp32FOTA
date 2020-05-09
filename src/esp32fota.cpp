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

// Utility to extract header value from headers
String esp32FOTA::getHeaderValue(String header, String headerName)
{
    return header.substring(strlen(headerName.c_str()));
}

// OTA Logic
void esp32FOTA::execOTA()
{

    WiFiClient client;
    int contentLength = 0;
    bool isValidContentType = false;

    Serial.println("Connecting to: " + String(_host));
    // Connect to Webserver
    if (client.connect(_host.c_str(), _port))
    {
        // Connection Succeed.
        // Fecthing the bin
        Serial.println("Fetching Bin: " + String(_bin));

        // Get the contents of the bin file
        client.print(String("GET ") + _bin + " HTTP/1.1\r\n" +
                     "Host: " + _host + "\r\n" +
                     "Cache-Control: no-cache\r\n" +
                     "Connection: close\r\n\r\n");

        unsigned long timeout = millis();
        while (client.available() == 0)
        {
            if (millis() - timeout > 5000)
            {
                Serial.println("Client Timeout !");
                client.stop();
                return;
            }
        }

        while (client.available())
        {
            // read line till /n
            String line = client.readStringUntil('\n');
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
                    break;
                }
            }

            // extract headers here
            // Start with content length
            if (line.startsWith("Content-Length: "))
            {
                contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
                Serial.println("Got " + String(contentLength) + " bytes from server");
            }

            // Next, the content type
            if (line.startsWith("Content-type: "))
            {
                String contentType = getHeaderValue(line, "Content-type: ");
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
        bool canBegin = Update.begin(contentLength);

        // If yes, begin
        if (canBegin)
        {
            Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
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
            client.flush();
        }
    }
    else
    {
        Serial.println("There was no content in the response");
        client.flush();
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

            const char *pltype = JSONDocument["type"];
            int plversion = JSONDocument["version"];
            const char *plhost = JSONDocument["host"];
            _port = JSONDocument["port"];
            const char *plbin = JSONDocument["bin"];

            String jshost(plhost);
            String jsbin(plbin);

            _host = jshost;
            _bin = jsbin;

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
void esp32FOTA::forceUpdate(String firmwareHost, int firmwarePort, String firmwarePath)
{
    _host = firmwareHost;
    _bin = firmwarePath;
    _port = firmwarePort;
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
typedef   struct
{
  int version;
  int port;
  const char * host;
  const char * bin;
  const char * type;
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
    useDeviceID = false;
}


/*
This function initializes the connection with the server, verifying that it is
compliant with the provided certificate.
*/
bool prepareConnection()
{
    char* server=_host;
    char* certificate=_certificate;
    clientForOta.setCACert(certificate);
    if(clientForOta.connect(server, 443))
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
String secureGetContent( )
{
    char* server=_host;
    String destinationURL=_descriptionOfFirmwareURL;
    char* certificate=_certificate;


    bool canConnectToServer=prepareConnection();
    if(canConnectToServer)
    {
        //Serial.println("connected");
        clientForOta.println("GET https://"+String(server)+destinationURL+" HTTP/1.0");
        clientForOta.println("Host: "+String(server)+"");
        clientForOta.println("Connection: close");
        clientForOta.println();
        while (clientForOta.connected()) 
        {
            String line = clientForOta.readStringUntil('\n');
            if (line == "\r") {
                //Serial.println("headers received");
                break;
            }
        }

        String partialContentOfHTTPSResponse="";
        while (clientForOta.available()) 
        {
            char c = clientForOta.read();
            partialContentOfHTTPSResponse+=c;
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
    char * certificate=_certificate;
    char * server=_host;

    String destinationUrl=_descriptionOfFirmwareURL;
    OTADescription * description;
    String unparsedDescriptionOfFirmware=secureGetContent();

      int str_len = unparsedDescriptionOfFirmware.length() + 1;
      char JSONMessage[str_len];
      pippo.toCharArray(JSONMessage, str_len);

      StaticJsonBuffer<300> JSONBuffer;                         //Memory pool
      JsonObject &parsed = JSONBuffer.parseObject(JSONMessage); //Parse message

      if (!parsed.success())
      { //Check for errors in parsing
          //Serial.println("Parsing failed");
          delay(5000);
          return false;
      }

      
      description->type = (parsed["type"]);
      
      description->host=(parsed["host"]);
      description->version=parsed["version"];
      description->bin=(parsed["bin"]);
      
      clientForOta.stop();

    
    
    return true;
}

/*
This function parses a header string, keeping only its value and discarding the key.
*/
String secureEsp32FOTA::getHeaderValue(String header, String headerName)
{
    return header.substring(strlen(headerName.c_str()));
}


/*
This function extracts the content length from the corresponding header.
*/
int secureEsp32FOTA::getContentLength(String line)
{
  return atoi((getHeaderValue(line, "Content-Length: ")).c_str());          
}


/*
This function checks whether the content of a response is a firmware.
*/
bool secureEsp32FOTA::isValidContentType(String line)
{
        String contentType = getHeaderValue(line, "Content-Type: ");
            //Serial.println("Got " + contentType + " payload.");
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

void executeOTA()
{
  char* server=_host;
  String destinationUrl=_descriptionOfFirmwareURL;
  char* certificate=_certificate;

  bool canCorrectlyConnectToServer=prepareConnection();
  int contentLength;
  bool isValid;
  if(canCorrectlyConnectToServer)
  {
    //Serial.println("ok");
    clientForOta.println("GET https://"+String(server)+destinationURL+" HTTP/1.0");
    clientForOta.println("Host: "+String(server)+"");
    clientForOta.println("Connection: close");
    clientForOta.println();
    while (clientForOta.connected())
    {
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
        }
        // extract headers here
        // Start with content length
        if (line.startsWith("Content-Length: "))
        {
            contentLength = getContentLength(line);
        }
        // Next, the content type
        if (line.startsWith("Content-Type: "))
        {
          isValid=isValidContentType(line);   
        }
    }

    if (contentLength && isValid)
    {
      //Serial.println("beginn");
      bool canBegin= Update.begin(contentLength);
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
        Serial.println("");
       }
    }
  }
  else
  {
    Serial.println("Generic error");
  }
}
