#include <Arduino.h>
#include <MHGroveBLE.h>

// Values are for my Pretzelboard test system. Please insert your own.
// The RX pin is where we're going to receive serial messages, which are sent
// by the TX pin of the Grove BLE. Got it?
#define LED_PIN 13
#define BLERXPin 16
#define BLETXPin 15

SoftwareSerial bleStream(BLERXPin, BLETXPin);
MHGroveBLE ble(bleStream, "Grove Demo");
bool isReady;

void setup()
{
  Serial.begin(19200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BLERXPin, INPUT);
  pinMode(BLETXPin, OUTPUT);
  bleStream.begin(9600);
  bleStream.listen();

  ble.setDebug([](const char * text) {
    Serial.println(text);
  });
  ble.setOnReady([]() {
    digitalWrite(LED_PIN, HIGH);
    isReady = true;
  });
  ble.setOnPanic([]() {
    digitalWrite(LED_PIN, LOW);
    isReady = true;
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

    ble.send(data);
  });
}

void loop()
{
  // Blink during initialization.
  if (!isReady) {
    bool on = (millis() % 1024) >= 512;
    digitalWrite(LED_PIN, on ? HIGH : LOW);
  }

  ble.runOnce();
}
