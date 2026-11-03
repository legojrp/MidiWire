# Project Architecture & Implementation Plan: MidiWire

---

## 1. System Architecture Overview

`MidiWire` enables real-time, low-latency, remote cross-platform transmission of MIDI events between local performance hardware and remote Digital Audio Workstations (DAWs).

```
                                      [ LOCAL SYSTEM ]
┌────────────────────────┐      ┌──────────────────────────┐      ┌────────────────────────┐
│ USB MIDI Controller    │ ───> │ Native MIDI Driver       │ ───> │ MidiEngine             │
└────────────────────────┘      └──────────────────────────┘      └───────────┬────────────┘
                                                                              │ (MidiPacket)
                                                                              ▼
                                                                  ┌────────────────────────┐
                                                                  │ Serializer             │
                                                                  └───────────┬────────────┘
                                                                              │ (Binary Bytes)
                                                                              ▼
                                                                  ┌────────────────────────┐
                                                                  │ NetworkEngine (WebRTC) │
                                                                  └───────────┬────────────┘
                                                                              │
══════════════════════════════════════════════════════════════════════════════╪══════════════════ Network Boundary (UDP / WebRTC)
                                                                              │
                                      [ REMOTE SYSTEM ]                       │
┌────────────────────────┐      ┌──────────────────────────┐      ┌───────────▼────────────┐
│ Target DAW             │ <─── │ Native Virtual MIDI Source│ <─── │ JitterBuffer           │
└────────────────────────┘      └──────────────────────────┘      └───────────▲────────────┘
                                                                              │ (MidiPacket)
                                                                  ┌───────────┴────────────┐
                                                                  │ NetworkEngine (WebRTC) │
                                                                  └────────────────────────┘

```

### Performance Targets

* **End-to-End Network Latency:** < 30–50 ms (varies by geographic distance and ISP routing).
* **Jitter Buffer Variance Target:** < 5 ms arrival delta.
* **Timestamp Precision:** 64-bit nanosecond monotonic clocks (`std::chrono::steady_clock`).
* **Packet Drop Recovery:** Zero stuck notes via active sensing heartbeats and automatic `All Notes Off` (CC 123) triggers.

---

## 2. Module & Class Specifications

### A. Core Models & Serialization

#### `struct MidiPacket`

* **File:** `include/core/MidiPacket.hpp`
* **Responsibility:** Normalized internal data payload passed between modules.
* **Attributes:**
* `uint64_t timestampNs`: Monotonic nanosecond timestamp of event capture or scheduled playback.
* `uint8_t status`: MIDI status byte (e.g., `0x90` Note On, `0x80` Note Off, `0xB0` CC).
* `uint8_t data1`: Key number or Controller ID.
* `uint8_t data2`: Key velocity or Controller value.
* `uint8_t channel`: MIDI channel index (0–15).
* `std::vector<uint8_t> sysex`: Optional payload container for SysEx messages.



#### `class Serializer`

* **File:** `include/core/Serializer.hpp`
* **Responsibility:** High-speed binary serialization/deserialization of `MidiPacket` structures into raw byte vectors for wire transport.
* **Methods:**
* `static std::vector<uint8_t> serialize(const MidiPacket& packet)`
* `static MidiPacket deserialize(const uint8_t* data, size_t length)`



---

### B. Hardware Abstraction Layer (HAL) & MIDI Engine

#### `class IMidiDriver` (Abstract Base)

* **File:** `include/midi/IMidiDriver.hpp`
* **Responsibility:** Platform-agnostic interface for MIDI hardware enumeration, input listening, and virtual port creation.
* **Methods:**
* `virtual bool initialize() = 0`
* `virtual std::vector<std::string> getAvailableInputs() = 0`
* `virtual bool openInputPort(uint32_t index, std::function<void(const MidiPacket&)> callback) = 0`
* `virtual bool createVirtualOutputPort(const std::string& name) = 0`
* `virtual void sendToVirtualPort(const MidiPacket& packet) = 0`



#### `class MacMidiDriver : public IMidiDriver`

* **File:** `src/midi/MacMidiDriver.cpp`
* **Implementation:** macOS CoreMIDI (`MIDIClientRef`, `MIDIPortRef`, `MIDISourceCreate`, `MIDIReceived`).

#### `class WinMidiDriver : public IMidiDriver`

* **File:** `src/midi/WinMidiDriver.cpp`
* **Implementation:** Windows MIDI Services API (Win 11 native loopback) / `virtualMIDI` C++ SDK fallback.

#### `class LinuxMidiDriver : public IMidiDriver`

* **File:** `src/midi/LinuxMidiDriver.cpp`
* **Implementation:** ALSA Sequencer API (`snd_seq_open`, `snd_seq_create_simple_port`).

#### `class MidiEngine`

* **File:** `include/midi/MidiEngine.hpp`
* **Responsibility:** High-level manager wrapping the active platform's `IMidiDriver`. Manages physical controller connection state and handles emergency flush functions.
* **Methods:**
* `bool startInput(uint32_t deviceIndex)`
* `bool setupVirtualPort(const std::string& name)`
* `void panic()` (Sends `Note Off` / CC 123 across all 16 channels to resolve stuck notes).



---

### C. Synchronization & Buffer Subsystem

#### `class ClockSynchronizer`

* **File:** `include/net/ClockSynchronizer.hpp`
* **Responsibility:** Estimates monotonic clock offset ($\Delta t$) and Round-Trip Time (RTT) between remote systems using an NTP-style ping/pong handshake.
* **Attributes:**
* `int64_t clockOffsetNs`: Estimated delta between local and remote monotonic clocks.
* `uint64_t roundTripTimeNs`: Estimated current network round-trip time.


* **Methods:**
* `void processPing(uint64_t t1_remote, uint64_t t2_local)`
* `void processPong(uint64_t t1_local, uint64_t t2_remote, uint64_t t3_remote, uint64_t t4_local)`
* `uint64_t remoteToLocalTime(uint64_t remoteNs) const`



#### `class JitterBuffer`

* **File:** `include/net/JitterBuffer.hpp`
* **Responsibility:** Min-heap priority queue sorted by timestamp. Holds incoming remote packets for a configurable target buffer depth (e.g., 10–20 ms) to smooth out variable packet arrival times before feeding the virtual port.
* **Methods:**
* `void push(const MidiPacket& packet)`
* `bool popReadyPacket(uint64_t currentLocalTimeNs, MidiPacket& outPacket)`
* `void setBufferDepthMs(uint32_t ms)`



---

### D. Networking Subsystem

#### `class SignalingClient`

* **File:** `include/net/SignalingClient.hpp`
* **Responsibility:** Connects to the central WebSocket signaling server to exchange room management events, WebRTC SDP offers/answers, and ICE candidates.
* **Methods:**
* `void connect(const std::string& serverUrl)`
* `void joinRoom(const std::string& roomId)`
* `void sendSignal(const std::string& type, const nlohmann::json& payload)`



#### `class NetworkEngine`

* **File:** `include/net/NetworkEngine.hpp`
* **Dependencies:** `libdatachannel`
* **Responsibility:** Manages the WebRTC `PeerConnection` lifecycle and configures dual DataChannels.
* **Channels:**
* **Unreliable DataChannel (SCTP over UDP):** Configured with `maxRetransmits = 0` and `unordered = true`. Used for minimal latency transmission of `MidiPacket` note and controller events.
* **Reliable DataChannel (SCTP over DTLS):** Configured with ordered/reliable delivery. Used for clock sync ping/pong messages, room status, and SysEx dumps.


* **Methods:**
* `void initializePeerConnection(const std::string& stunServerUrl)`
* `void sendMidi(const std::vector<uint8_t>& binaryPayload)`
* `void sendControl(const std::vector<uint8_t>& binaryPayload)`



---

### E. Application Controller

#### `class AppController`

* **File:** `include/core/AppController.hpp`
* **Responsibility:** Central orchestrator connecting event signals across MIDI, network, clock sync, and buffer threads.
* **Key Tasks:**
1. Instantiates platform-specific `IMidiDriver`.
2. Routes hardware MIDI inputs into `Serializer` and `NetworkEngine`.
3. Receives remote network bytes, deserializes to `MidiPacket`, computes local target time via `ClockSynchronizer`, and pushes into `JitterBuffer`.
4. Runs a high-priority playback loop popping ready packets from `JitterBuffer` to `IMidiDriver::sendToVirtualPort`.



---

## 3. Class Interaction & Sequence Diagrams

### Hardware Capture & Outbound Network Flow

```
[Hardware Controller]       [MacMidiDriver]        [AppController]       [Serializer]       [NetworkEngine]
          │                        │                      │                   │                    │
          │── (Physical Note Press)│                      │                   │                    │
          └───────────────────────>│                      │                   │                    │
                                   │── (midiInputCallback)│                   │                    │
                                   └─────────────────────>│                   │                    │
                                                          │── serialize(pkt)─>│                    │
                                                          │<── (binary buffer)│                    │
                                                          │                   │                    │
                                                          │────── sendMidi(binary payload)─────────>│
                                                                                                   │──> [WebRTC UDP]

```

### Inbound Network & DAW Playback Loop

```
[WebRTC UDP]       [NetworkEngine]        [AppController]       [Serializer]       [JitterBuffer]       [MacMidiDriver]       [Target DAW]
     │                    │                      │                   │                   │                     │                  │
     │── (Incoming Bytes)─>│                      │                   │                   │                     │                  │
                          │── (onDataReceived)──>│                   │                   │                     │                  │
                                                 │── deserialize(...)│                   │                     │                  │
                                                 │<── (MidiPacket)───│                   │                     │                  │
                                                 │                                       │                     │                  │
                                                 │──────── push(adjustedPacket)─────────>│                     │                  │
                                                 │                                       │                     │                  │
                                                 │ [ High-Resolution Render Thread Loop ]│                     │                  │
                                                 │──────────────── popReadyPacket(...)──>│                     │                  │
                                                 │<─────────────── (readyPacket)─────────│                     │                  │
                                                 │                                                             │                  │
                                                 │────────────────────── sendToVirtualPort(readyPacket)───────>│                  │
                                                                                                               │── (CoreMIDI)────>│

```

---

## 4. Binary Wire Protocol Specification

Every MIDI event transmitted over the WebRTC Unreliable DataChannel adheres to a fixed **12-Byte Header Format**:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                     Timestamp (uint64_t)                      +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Status Byte  |   Data Byte 1 |   Data Byte 2 |  Channel Byte |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

```

* **Bytes 0–7:** Monotonic Timestamp in nanoseconds (`uint64_t`, Big-Endian network byte order).
* **Byte 8:** MIDI Status Byte (`0x80`–`0xEF`).
* **Byte 9:** MIDI Data 1 (Key Number `0–127` or CC Controller Number).
* **Byte 10:** MIDI Data 2 (Velocity `0–127` or CC Value).
* **Byte 11:** MIDI Channel (`0–15`).

*Note: For SysEx packets transmitted over the Reliable Channel, a variable-length frame header containing a 32-bit payload length prefix is prepended.*

---

## 5. Cross-Platform Abstraction Strategy

| Component | macOS | Windows | Linux |
| --- | --- | --- | --- |
| **Virtual Port Creation** | Native CoreMIDI (`MIDISourceCreate`) | Windows MIDI Services API (or `virtualMIDI` driver loopback) | ALSA Sequencer (`snd_seq_create_simple_port`) |
| **Hardware MIDI Listening** | CoreMIDI (`MIDIInputPortCreate`) | Windows MIDI Services / WinMM | ALSA Sequencer (`snd_seq_connect_from`) |
| **Monotonic Clock Source** | `std::chrono::steady_clock` (`clock_gettime_nsec_np`) | `QueryPerformanceCounter` / `std::chrono` | `clock_gettime(CLOCK_MONOTONIC)` |
| **Network Socket Layer** | `libdatachannel` (POSIX UDP) | `libdatachannel` (Winsock2) | `libdatachannel` (POSIX UDP) |

---

## 6. Project Directory Layout

```
MidiWire/
├── CMakeLists.txt
├── PROJECT_PLAN.md
├── cmake/
│   └── FindLibDataChannel.cmake
├── include/
│   ├── core/
│   │   ├── AppController.hpp
│   │   ├── MidiPacket.hpp
│   │   └── Serializer.hpp
│   ├── midi/
│   │   ├── IMidiDriver.hpp
│   │   └── MidiEngine.hpp
│   └── net/
│       ├── ClockSynchronizer.hpp
│       ├── JitterBuffer.hpp
│       ├── NetworkEngine.hpp
│       └── SignalingClient.hpp
└── src/
    ├── main.cpp
    ├── core/
    │   ├── AppController.cpp
    │   └── Serializer.cpp
    ├── midi/
    │   ├── LinuxMidiDriver.cpp
    │   ├── MacMidiDriver.cpp
    │   ├── MidiEngine.cpp
    │   └── WinMidiDriver.cpp
    └── net/
        ├── ClockSynchronizer.cpp
        ├── JitterBuffer.cpp
        ├── NetworkEngine.cpp
        └── SignalingClient.cpp

```

---

## 7. Implementation Roadmap

### Phase 1: Core Engine & Local Loopback (Current Phase)

* [x] Project workspace initialization with CMake.
* [x] macOS CoreMIDI native virtual port validation.
* [x] CoreMIDI physical hardware input capture validation.
* [x] Implement `MidiPacket` and `Serializer`.
* [x] Implement `JitterBuffer` priority queue and local playback worker thread.

### Phase 2: Dependency Integration & Networking Base

* [x] Integrate `libdatachannel` via Homebrew/CMake for WebRTC DataChannel abstraction.
* [x] Integrate `nlohmann_json` and `websocketpp` (or `libdatachannel` WebSocket client) for signaling.
* [x] Write a lightweight Node.js/Go Signaling Server for room matchmaking.

### Phase 3: P2P Connectivity & Synchronization

* [x] Implement `SignalingClient` WebRTC SDP/ICE exchange.
* [x] Implement `NetworkEngine` with dual DataChannels (Fast Unreliable vs. Reliable).
* [x] Implement `ClockSynchronizer` ping/pong offset estimation.

### Phase 4: Cross-Platform Abstractions & Hardening

* [x] Implement `WinMidiDriver` for Windows 10/11.
* [x] Implement `LinuxMidiDriver` via ALSA Sequencer.
* [x] Implement active sensing and network disconnection panic flush (`All Notes Off`).
