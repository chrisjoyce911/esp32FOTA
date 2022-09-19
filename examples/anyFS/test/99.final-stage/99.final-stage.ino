// Just a dummy script prevent an endless OTA loop to occur
// It validates the last step in the test suite by printing a message in the serial.


void setup()
{
  Serial.begin( 115200 );
  Serial.println();
  Serial.println();
  Serial.println();
  Serial.println("**************************");
  Serial.println("Test suite COMPLETE :-)");
  Serial.println();
  Serial.println();
}

void loop()
{

}
