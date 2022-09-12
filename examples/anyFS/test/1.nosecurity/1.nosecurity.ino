
const int firmware_version  = 5;
const char* firmware_name   = "esp32-fota-http";
const bool check_signature  = false;
const bool disable_security = false;
const char* description     = "Final Stage (does nothing)";

const char* fota_debug_fmt = R"DBG_FMT(

***************** STAGE %i *****************

  Description      : %s
  Firmware type    : %s
  Firmware version : %i

********************************************

TEST COMPLETE

)DBG_FMT";

void setup()
{
  Serial.begin(115200);
  Serial.printf( fota_debug_fmt, firmware_version, description, firmware_name, firmware_version );
}

void loop()
{
}
