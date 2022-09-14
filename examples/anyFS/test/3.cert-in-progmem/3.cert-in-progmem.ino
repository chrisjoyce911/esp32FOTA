/**
   esp32 firmware OTA

   Purpose: Perform an OTA update to both firmware and filesystem from binaries located
            on a webserver (HTTPS) while using progmem to check for certificate validity


*/

#include <esp32fota.h>


const char* github_root_ca = R"ROOT_CA(
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

// esp32fota settings
const int firmware_version  = 3;
#if defined FOTA_URL
  const char* fota_url        = FOTA_URL;
#else
  const char* fota_url        = "https://github.com/chrisjoyce911/esp32FOTA/raw/tests/examples/anyFS/test/stage1/firmware.json";
#endif
const char* firmware_name   = "esp32-fota-http";
const bool check_signature  = false;
const bool disable_security = false;
// for debug only
const char* description     = "PROGMEM example with security";

const char* fota_debug_fmt = R"DBG_FMT(

***************** STAGE %i *****************

  Description      : %s
  Firmware type    : %s
  Firmware version : %i
  Signature check  : %s
  TLS Cert check   : %s

********************************************

)DBG_FMT";

// esp32fota esp32fota("<Type of Firme for this device>", <this version>, <validate signature>, <allow insecure TLS>);
esp32FOTA esp32FOTA( String(firmware_name), firmware_version, check_signature, disable_security );

// create an abstraction of the root_ca file
CryptoMemAsset *GithubRootCA = new CryptoMemAsset("Root CA", github_root_ca, strlen(github_root_ca)+1 );

void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to WiFi ");
  Serial.println( WiFi.macAddress() );
  //Serial.println(ssid);

  WiFi.begin(); // no WiFi creds in this demo :-)

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println(WiFi.localIP());

  esp32FOTA.setRootCA( GithubRootCA );

}


void setup()
{
  Serial.begin(115200);
  Serial.printf( fota_debug_fmt, firmware_version, description, firmware_name, firmware_version, check_signature?"Enabled":"Disabled", disable_security?"Disabled":"Enabled" );

  esp32FOTA.checkURL = fota_url;

  setup_wifi();
}

void loop()
{

  bool updatedNeeded = esp32FOTA.execHTTPcheck();
  if (updatedNeeded)
  {
    esp32FOTA.execOTA();
  }

  delay(20000);
}

