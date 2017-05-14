#include <MHGroveBLE.h>

#define BLERXPin 16
#define BLETXPin 15

SoftwareSerial bleStream(BLERXPin, BLETXPin);
MHGroveBLE ble(bleStream, "Grove Demo");

void setup()
{
  Serial.begin(19200);

  pinMode(BLERXPin, INPUT);
  pinMode(BLETXPin, OUTPUT);
  bleStream.begin(9600);
  bleStream.listen();

  ble.setDebug([](const char * text) {
    Serial.println(text);
  });

  ble.setOnConnect([]() {
    Serial.println(F("Connected."));
  });
  ble.setOnDisconnect([]() {
    Serial.println(F("Disconnected."));
  });
  ble.setOnDataReceived([](const String & data) {
    Serial.print(F("Data received from app: "));
    Serial.print(data);
    Serial.println();
  });
}

void loop()
{
  ble.runOnce();
}
