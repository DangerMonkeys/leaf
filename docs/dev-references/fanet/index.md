## Fanet Support

The Leaf supports Fanet for the purposes of:

- Sending periodic flying updates
- Receiving broadcasted positional packets
  - Update traffic using FANET nmea strings to Bluetooth devices
- Sending periodic ground tracking positions for:
  - Landed Safe
  - Walking
  - Need A Ride
  - Need Technical Support.

As I believe we should not be encouraging the use of Fanet for emergency updates (voice radio and satellite preferred) a design decision was made to omit any emergency modes from the vario.

The leaf does NOT support any service (weather) or messaging capabilities of Fanet yet. It also does not support any form of message signatures and will not send them, and ignore the signature on any received packets.

The Leaf will also participate in forwarding on any received Fanet packets received with the requested forward flags set.

## Architecture

the fanet_radio.h and fanet_radio.cpp files contain the FanetRadio class. This class will initialize the SX1262 chip, the Fanet::Manager, and any buffers to interact with them.

The FanetRadio class will also provide a map of the last received neighbor to (ground or flying) tracking packet to keep state of where traffic is around, as well as a list of callbacks when this information updates to be able to be consumed by Bluetooth modules and the like.

### Fanet::Manager

To keep the airwaves clear, there are rules around how long we should hold packets before we forward them, and when we should forward or drop packets. We should also ensure that as the network becomes more busy, we send updates with less frequency to try and keep the airwaves as open as possible. We use the Fanet::Manager library for this. It has the following purposes:

- Parses any receive bytes into RX packets
- Keeps an internal txQueue for packets to egress and timers for those packets to go out
- Keeps track of positional information, and will request a send at the appropriate time, given the business of the network.
- Forward received packets back on the Fanet network.

### FanetRadio tasks

To interface with the Fanet::Manager library, the FanetRadio module keeps an instance of the manager, mutex to allow multiple tasks can interact with it in a thread-safe manner, and for simplicity of coding, two tasks:

**FanetRx:**

This task is responsible for reading packets from the LoRa radio and passing them to the Manager to parse.

In the event that positional updates are received, the task will update the position database and notify any modules that new positional updates are available. This is intended to be used by the Bluetooth modules.

**FanetTx:**

This task is responsible for moving packets from the FanetManager library to the LoRa wire, and reporting back success. It should sleep until the next scheduled wake up time, or woken up to process the new sleep time (after a request to send a packet was made).
