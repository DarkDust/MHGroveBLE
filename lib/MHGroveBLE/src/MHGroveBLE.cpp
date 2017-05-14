/*
MIT License

Copyright (c) 2017 Marc Haisenko

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "MHGroveBLE.h"

/*
Some notes about the Seeed Grove BLE:

The commands and responses are sent and received WITHOUT any delimiters like
"\r\n". After sending the command, you just wait for the response. You know the
reponse is complete if you don't receive any more stuff so you have to wait for
a timeout.

How utterly stupid! But that's the way it is.
*/

static const unsigned long kDefaultTimeout = 500;
static const unsigned long kRetryTimeout = 500;
static const unsigned long kWaitForDeviceTimeout = 5000;
static const unsigned long kConnectedReadTimeout = 50;

/** Internal state. */
enum class MHGroveBLE::InternalState {
  /** Initial state. */
  startup,
  /** Send "AT" periodically and wait until the device responds. */
  waitForDeviceAfterStartup,
  /** Set the Bluetooth name. */
  setName,
  /** Set the device role. */
  setRole,
  /** Set that we want to be notified about connections. */
  setNotification,
  /** Reset after setting the device up. */
  reset,
  /** Send "AT" periodically and wait until the device responds. */
  waitForDeviceAfterReset,
  /** Initialization is done, inform the handler. */
  initializationComplete,

  /** Waiting for a connection. */
  waitingForConnection,
  /** A peer has connected. */
  connected,

  /** An unrecoverable error occurred, operation is halted. */
  panicked
};

/** Return value for `receiveResponse`. */
enum class MHGroveBLE::ResponseState {
  /** Still receiving data. */
  receiving,
  /** Nothing received so far, need to resend the command. */
  needRetry,
  /** The timeout has been reached, no data received. */
  timedOut,
  /** The retry or timeout time has been reached, we have data in `rxBuffer`. */
  success
};

/** Internal helper: calculate whether a timeout occurred.

 Handles `millies()` overflow.
 */
static bool isTimeout(unsigned long now, unsigned long referenceTime, unsigned long duration)
{
  return (now - referenceTime) >= duration;
}


MHGroveBLE::MHGroveBLE(
  Stream & device,
  const char * name,
  unsigned int rxBufferSize
) :
  device(device),
  name(name),
  rxBufferSize(rxBufferSize),
  internalState(InternalState::startup)
{
  rxBuffer.reserve(rxBufferSize);
}

void MHGroveBLE::runOnce()
{
  switch (internalState) {
    case InternalState::startup:
      transitionToState(InternalState::waitForDeviceAfterStartup);
      break;

    case InternalState::waitForDeviceAfterStartup:
    case InternalState::waitForDeviceAfterReset:
      handleWaitForDevice();
      break;

    case InternalState::setName:
    case InternalState::setRole:
    case InternalState::setNotification:
      handleGenericCommand();
      break;

    case InternalState::reset:
      handleReset();
      break;

    case InternalState::initializationComplete:
      // It should not be possible to end up here since we immediately
      // transition to the `waitingForConnection` state.
      break;

    case InternalState::waitingForConnection:
      handleWaitForConnect();
      break;

    case InternalState::connected:
      handleConnected();
      break;

    case InternalState::panicked:
      // Once we've panicked we won't do anything again.
      break;
  }
}

MHGroveBLE::State MHGroveBLE::getState() {
  switch (internalState) {
    case InternalState::panicked:   return State::panicked;
    case InternalState::waitingForConnection: return State::waitingForConnection;
    case InternalState::connected:  return State::connected;
    default:                        return State::initializing;
  }
}

void MHGroveBLE::setOnReady(void (*onFunc)())
{
  onReady = onFunc;
}

void MHGroveBLE::setOnConnect(void (*onFunc)())
{
  onConnect = onFunc;
}

void MHGroveBLE::setOnDisconnect(void (*onFunc)())
{
  onDisconnect = onFunc;
}

void MHGroveBLE::setOnDataReceived(void (*onFunc)(const String &))
{
  onDataReceived = onFunc;

}

void MHGroveBLE::setDebug(void (*debugFunc)(const char *))
{
  debug = debugFunc;
}

bool MHGroveBLE::send(const String & data)
{
  if (internalState != InternalState::connected) {
    return false;
  }

  device.print(data);
}


/*******************************************************************************
 * Private section
 ******************************************************************************/

void MHGroveBLE::sendCommand(const String & command)
{
  if (debug) {
    String text = F("Sending command: ");
    text += command;
    debug(text.c_str());
  }

  device.print(command);

  // Clear the receive buffer after sending a command.
  rxBuffer = "";
}

MHGroveBLE::ResponseState MHGroveBLE::receiveResponse()
{
  unsigned long now = millis();
  bool timeoutReached = isTimeout(now, timeoutReferenceTime, timeoutDuration);
  bool retryReached =
    retryDuration > 0
    ? isTimeout(now, retryReferenceTime, retryDuration)
    : false;

  readIntoBuffer();

  if (retryReached || timeoutReached) {
    if (rxBuffer.length() > 0) {
      // We reached a timeout and have data! We're done.
      if (debug) {
        String text = F("Received response: ");
        text += rxBuffer;
        debug(text.c_str());
      }
      return ResponseState::success;

    } else if (timeoutReached) {
      // The hard timeout has been reached.
      return ResponseState::timedOut;

    } else {
      // The retry timeout (soft timeout) has been reached. Reset the reference
      // time to start the next retry phase.
      retryReferenceTime = now;
      return ResponseState::needRetry;
    }

  } else {
    // No timeout reached yet, keep on reading.
    return ResponseState::receiving;
  }
}

void MHGroveBLE::transitionToState(MHGroveBLE::InternalState nextState)
{
  unsigned long now = millis();

  if (debug) {
    String text = F("Transitioning to state: ");
    text += (int)nextState;
    debug(text.c_str());
  }

  switch (nextState) {
    case InternalState::startup: break;

    case InternalState::waitForDeviceAfterStartup:
      sendCommand(F("AT"));
      retryReferenceTime = now;
      retryDuration = kRetryTimeout;
      timeoutReferenceTime = now;
      timeoutDuration = kWaitForDeviceTimeout;
      break;

    case InternalState::setName: {
      String command = F("AT+NAME");
      command += name;
      sendCommand(command);
      retryReferenceTime = 0;
      retryDuration = 0;
      timeoutReferenceTime = now;
      timeoutDuration = kDefaultTimeout;
      genericNextInternalState = InternalState::setRole;
      break;
    }

    case InternalState::setRole:
      sendCommand(F("AT+ROLE0"));
      retryReferenceTime = 0;
      retryDuration = 0;
      timeoutReferenceTime = now;
      timeoutDuration = kDefaultTimeout;
      genericNextInternalState = InternalState::setNotification;
      break;

    case InternalState::setNotification:
      sendCommand(F("AT+NOTI1"));
      retryReferenceTime = 0;
      retryDuration = 0;
      timeoutReferenceTime = now;
      timeoutDuration = kDefaultTimeout;
      genericNextInternalState = InternalState::reset;
      break;

    case InternalState::reset:
      sendCommand(F("AT+RESET"));
      retryReferenceTime = 0;
      retryDuration = 0;
      timeoutReferenceTime = now;
      timeoutDuration = kDefaultTimeout;
      break;

    case InternalState::waitForDeviceAfterReset:
      sendCommand(F("AT"));
      retryReferenceTime = now;
      retryDuration = kRetryTimeout;
      timeoutReferenceTime = now;
      timeoutDuration = kWaitForDeviceTimeout;
      break;

    case InternalState::initializationComplete:
      if (onReady) {
        onReady();
      }
      break;

    case InternalState::waitingForConnection:
      if (internalState == InternalState::connected && onDisconnect) {
        onDisconnect();
      }
      rxBuffer = "";
      // No timeout handling in this state.
      break;

    case InternalState::connected:
      if (onConnect) {
        onConnect();
      }
      retryReferenceTime = 0;
      retryDuration = 0;
      timeoutReferenceTime = 0;
      timeoutDuration = 0;
      break;

    case InternalState::panicked:
      if (debug) {
        debug("Panic!");
      }
      break;
  }

  internalState = nextState;

  if (nextState == InternalState::initializationComplete) {
    // When we've reached the `initializationComplete` state, we want to inform
    // the handler (done above) and then continue to the `waitingForConnection`
    // state right away.
    transitionToState(InternalState::waitingForConnection);
  }
}

bool MHGroveBLE::readIntoBuffer()
{
  bool didReceive = false;

  while (device.available() > 0) {
    int value = device.read();
    if (value < 0) {
      // Shouldn't happen? We asked whether there's stuff available!
      break;
    }

    if (rxBuffer.length() == rxBufferSize) {
      // We don't want to grow the receive buffer. Discard old data.
      rxBuffer.remove(0, 1);
    }

    rxBuffer += (char)value;
    didReceive = true;
  }

  return didReceive;
}

void MHGroveBLE::handleWaitForDevice()
{
  switch (receiveResponse()) {
    case ResponseState::receiving:
      break;

    case ResponseState::needRetry:
      sendCommand(F("AT"));
      break;

    case ResponseState::timedOut:
      panic();
      break;

    case ResponseState::success:
      switch (internalState) {
        case InternalState::waitForDeviceAfterStartup:
          transitionToState(InternalState::setName);
          break;

        case InternalState::waitForDeviceAfterReset:
          transitionToState(InternalState::initializationComplete);
          break;

        default:
          // Bug! Must not happen.
          panic();
      }
      break;
  }
}

void MHGroveBLE::handleGenericCommand()
{
  switch (receiveResponse()) {
    case ResponseState::receiving:
      break;

    case ResponseState::needRetry: // Bug, must not happen
    case ResponseState::timedOut:
      panic();
      break;

    case ResponseState::success:
      transitionToState(genericNextInternalState);
      break;
  }
}

void MHGroveBLE::handleReset()
{
  switch (receiveResponse()) {
    case ResponseState::receiving:
      break;

    case ResponseState::needRetry: // Bug, must not happen
      panic();
      break;

    case ResponseState::timedOut:
      // Try waiting for the device.
      // Fall-through
    case ResponseState::success:
      transitionToState(InternalState::waitForDeviceAfterReset);
      break;
  }
}

void MHGroveBLE::handleWaitForConnect()
{
  if (!readIntoBuffer()) {
    return;
  }

  if (rxBuffer.indexOf(F("OK+CONN")) != -1) {
    rxBuffer = "";
    // Once we've received this string immediately transition to the "connected"
    // state. There's no point in waiting for a timeout like when we're waiting
    // for AT command responses.
    // The point of doing this with `indexOf` is to recover if we have received
    // garbage before.
    transitionToState(InternalState::connected);
  }
}

void MHGroveBLE::handleConnected()
{
  unsigned long now = millis();
  bool connectionClosed = false;
  bool dataWasRead = readIntoBuffer();
  bool timeoutReached =
    timeoutDuration > 0
    ? isTimeout(now, timeoutReferenceTime, timeoutDuration)
    : false;

  if (!dataWasRead && !timeoutReached) {
    // Nothing happened, no need to react yet.
    return;
  }

  if (dataWasRead) {
    // Every time we read data we need to reset our timeout to avoid
    // timing out in the middle of a stream.
    timeoutReferenceTime = now;
    timeoutDuration = kConnectedReadTimeout;
  }

  // Pass the data to the handler if either the timeout occurred or if the
  // receive buffer is full. The later is necessary to not lose any data.
  if (timeoutReached || rxBuffer.length() == rxBufferSize) {
    // The Grove BLE sends "OK+LOST" when the connection is closed.
    // Unfortunately, an app is also able to send this string and we don't know
    // whether the Grove BLE or the app has sent it.
    // This is why `handleWaitForConnect` also needs to be able to handle
    // garbage: it's possible that an app sends "OK+LOST" and once the
    // connection is really closed, another "OK+LOST" is sent by Grove BLE
    // before the "OK+CONN" for a new connection arrives.
    int index = rxBuffer.lastIndexOf(F("OK+LOST"));
    // Check whether it's the last thing in the buffer to avoid reacting on the
    // string being part of some other text.
    if (index >= 0 && ((unsigned)index+7 == rxBuffer.length())) {
      connectionClosed = true;
      // Remove the sentinel.
      rxBuffer.remove(index);
    }

    // Pass the data to the handler...
    if (onDataReceived && rxBuffer.length() > 0) {
      onDataReceived(rxBuffer);
    }
    // ... and clear it.
    rxBuffer = "";
    timeoutReferenceTime = 0;
    timeoutDuration = 0;
  }

  if (connectionClosed) {
    transitionToState(InternalState::waitingForConnection);
  }
}

void MHGroveBLE::panic()
{
  transitionToState(InternalState::panicked);
}
