#include <MHGroveBLE.h>

// Input pin. Connected to the Grove BLE TX pin.
#define BLE_RX_PIN 16
// Output pin. Connected to the Grove BLE RX pin.
#define BLE_TX_PIN 15

// Pin with status LED.
#define LED_PIN 13

// Stream connecting the pins to the BLE class.
SoftwareSerial bleStream(BLE_RX_PIN, BLE_TX_PIN);
// The BLE class.
MHGroveBLE ble(bleStream, "Grove Demo");
// Whether a final state has been reached.
bool isReady;

void setup()
{
  Serial.begin(19200);

  // Set up status LED port.
  pinMode(LED_PIN, OUTPUT);

  // Set up the ports connecting to the Grove BLE.
  pinMode(BLE_RX_PIN, INPUT);
  pinMode(BLE_TX_PIN, OUTPUT);

  // Start the stream.
  bleStream.begin(9600);
  bleStream.listen();

  // Set a PIN. Remove this line to allow connecting to the Grove BLE
  // without a PIN.
  ble.setPIN("123456");

  // All following event handlers are optional. You can remove those
  // you don't need.

  // Pass on debug informations to the serial line.
  ble.setDebug([](const char * text) {
    Serial.println(text);
  });
  // Called once the initialization is complete.
  ble.setOnReady([]() {
    digitalWrite(LED_PIN, HIGH);
    isReady = true;
  });
  // Called when an unrecoverable error occurs.
  ble.setOnPanic([]() {
    digitalWrite(LED_PIN, LOW);
    isReady = true;
  });
  // Called whenever a peer connects.
  ble.setOnConnect([]() {
    Serial.println(F("Connected."));
  });
  // Called whenever a peer disconnects.
  ble.setOnDisconnect([]() {
    Serial.println(F("Disconnected."));
  });
  // Handle incoming data.
  ble.setOnDataReceived([](const String & data) {
    Serial.print(F("Data received from peer: "));
    Serial.print(data);
    Serial.println();

    // Send data back.
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

  // Run the BLE state machine.
  ble.runOnce();
}
