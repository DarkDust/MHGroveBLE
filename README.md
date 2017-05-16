# MHGroveBLE – Arduino/PlatformIO support for Seeed Grove BLE module

This project provides an easy-to-use class to interface with
[Seeed Grove BLE v1](http://wiki.seeed.cc/Grove-BLE_v1/) modules. The
[Seeed Grove BLE (dual model) v1.0](http://wiki.seeed.cc/Grove-BLE-dual_model-v1.0/)
is *not* yet supported.


## Installation

### Arduino IDE/Library Manager

* Open _Sketch > Include Library > Manage Libraries_ in the Arduino IDE.
* Search for "MHGroveBLE" and click on the entry.
* Click the _"Install"_ button to install the libary.


### PlatformIO

* Open your project's `platformio.ini`.
* Add `MHGroveBLE` to the `lib_deps` entry:
```
lib_deps =
    MHGroveBLE
```

## Hardware

![Diagram showing how to connect the Grove BLE module to your board](https://rawgit.com/DarkDust/MHGroveBLE/master/docs/connecting.svg)

Connect the Grove BLE's RX and TX pins to your board's/MCU's pins that you want
to use for receiving/sending. And of course connect the Grove BLE's V<sub>CC</sub>
(3.3V – 5V) and ground.

## Usage

### Basic setup

The `MHGroveBLE` class needs a stream to interface with the module. Depending on
your MCU board, this should either be a `HardwareSerial` or `SoftwareSerial`
instance.

```c++
#include <MHGroveBLE.h>
#include <SoftwareSerial.h>

#define BLE_RX_PIN 16 // Arduino digital pin for receiving
#define BLE_TX_PIN 15 // Arduino digital pin for sending

SoftwareSerial bleStream(BLE_RX_PIN, BLE_TX_PIN);
MHGroveBLE ble(bleStream, "Grove Demo");

void setup() {
  pinMode(BLE_RX_PIN, INPUT);
  pinMode(BLE_TX_PIN, OUTPUT);
  // Default speed of the module is 9600 baud 8N1.
  bleStream.begin(9600);
  bleStream.listen();

  // Further BLE setup can be done here.
}

void loop() {
  // In every loop iteration, call the `runOnce()` method.
  ble.runOnce();
}
```

The `MHGroveBLE` class is designed to be called periodically via `runOnce()`
where all the work is done. No `delay()` is called by the class at any time.
Usually only small amounts of work are done in `runOnce()` and control is
returned quickly, so you can continue to do other work in your `loop()`. The
longest running operation is sending of commands which is a synchronous
operation when using `SoftwareSerial`.

Once a peer has connected, you can `send(data)` to it (which also may block
when using `SoftwareSerial`).


### Callbacks

A number of callbacks for events are provided by the class which you can either
pass functions or C++ lambdas. All of them are optional.

```c++
void setup() {
  // Called once the intialization is complete and
  // the Grove BLE is ready to receive connections.
  ble.setOnReady([]() {
    digitalWrite(LED_PIN, HIGH);
  });
  // Called when an unrecoverable error happens.
  ble.setOnPanic([]() {
    digitalWrite(LED_PIN, LOW);
  });
  // Called when a peer has connected.
  ble.setOnConnect([]() {
    Serial.println(F("Connected."));
  });
  // Called when a peer has disconnected.
  ble.setOnDisconnect([]() {
    Serial.println(F("Disconnected."));
  });
  // Called when data from the peer has been received.
  ble.setOnDataReceived([](const String & data) {
    Serial.print(F("Data received from peer: "));
    Serial.print(data);
    Serial.println();

    // Echo the received text back to the peer.
    ble.send(data);
  });
  // Internal debug messages.
  ble.setDebug([](const char * text) {
    Serial.println(text);
  });
}
```

### Other setup

A PIN can be set that will be required when a peer wants to connect to the
Grove BLE module. If you want to set a PIN, you should call this setter before
`runOnce()` is called for the first time (i.e. during `setup()`).

```c++
// Must be a string with six numbers, from "000000" to "999999".
ble.setPIN("123456");
```
