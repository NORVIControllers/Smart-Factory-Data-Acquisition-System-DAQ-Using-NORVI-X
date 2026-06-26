# NORVI X — Smart Factory Data Acquisition (DAQ) Example

Reference firmware for building a data acquisition node on the NORVI X platform.
The node reads analogue process signals, digital machine states, and RS-485 Modbus RTU
instruments, timestamps every sample against an on-board real-time clock, and publishes
the data as JSON over MQTT to Grafana, NORVI Cloud, or any MQTT broker.

This is an example application intended as a starting point. The acquisition logic is
fixed and tested; the parts an integrator changes (network, broker, and the I/O tag list)
are isolated in two configuration files.

Target hardware: NORVI X-CPU-ESPS3-X1 (ESP32-S3-N16R2) with AI4 and DI16 expansion modules.

---

## Contents

- [Overview](#overview)
- [Hardware](#hardware)
- [Acquisition layers](#acquisition-layers)
- [Repository layout](#repository-layout)
- [Dependencies](#dependencies)
- [Build and flash](#build-and-flash)
- [Configuration](#configuration)
- [Defining the I/O list](#defining-the-io-list)
- [MQTT output](#mqtt-output)
- [Store-and-forward](#store-and-forward)
- [Reliability behaviour](#reliability-behaviour)
- [Serial diagnostics](#serial-diagnostics)
- [Troubleshooting](#troubleshooting)
- [Reference documents](#reference-documents)
- [License](#license)

---

## Overview

A single NORVI X node covers the full range of signals found on a production machine:

- 4–20 mA / 0–10 V transmitters (pressure, temperature, level, load) via the AI4 module
- Discrete machine states (run/stop, gate, overload, part-present) via the DI16 module
- Serial instruments (energy meters, flow meters, analysers) over RS-485 Modbus RTU
- A built-in MQTT publisher that pushes a unified, timestamped JSON record upstream

The firmware runs a non-blocking cooperative scheduler on the ESP32-S3. Each layer is
polled on its own interval, all values are written into one shared data model, and a single
publish task assembles and transmits the payload. When the network or broker is unavailable,
records are buffered to the microSD card and replayed in order once connectivity returns,
so no data gaps appear in the time-series database.

---

## Hardware

| Component        | Device            | Interface     | Default address |
|------------------|-------------------|---------------|-----------------|
| CPU              | ESP32-S3-N16R2    | —             | —               |
| Output expander  | PCA9539           | I²C           | 0x75            |
| Button / LED     | PCA9536           | I²C           | 0x41            |
| RTC              | DS3231            | I²C           | 0x68            |
| Analogue input   | ADS1115 (AI4)     | I²C           | 0x48 (0x49…)    |
| Digital input    | PCA9555 (DI16)    | I²C           | 0x27 (0x26…)    |
| Storage          | microSD           | SPI           | CS = GPIO42     |
| Ethernet (X1)    | W5500             | SPI           | CS = GPIO1      |
| Serial field bus | RS-485 transceiver| UART2         | DE = GPIO41     |

CPU pin map used by the firmware:

```
I2C        SDA 8   SCL 9
SPI        SCLK 12 MISO 13 MOSI 11
SD CS      42
Ethernet   CS 1
RS-485     RX 16   TX 15   DE/flow-control 41
PCA9539    reset 38
```

The AI4 and DI16 modules sit on the shared I²C bus and are addressed individually, so up to
two of each are supported in this example by populating the address tables. The SPI bus is
shared by the SD card and the W5500; the firmware deselects the Ethernet chip before
addressing the card to avoid bus contention.

---

## Acquisition layers

| Layer | Source            | Default rate | Notes                                                |
|-------|-------------------|--------------|------------------------------------------------------|
| AI    | ADS1115 (AI4)     | 500 ms       | 4–20 mA or 0–10 V, scaled to engineering units; open-loop detection below 3.6 mA |
| DI    | PCA9555 (DI16)    | 100 ms       | Debounced; rising edges counted per channel          |
| Modbus| RS-485 RTU master | 1–5 s        | Per-instrument schedule; dead-slave backoff          |
| MQTT  | Publisher         | 5 s          | One JSON record per cycle; retained online/offline status |

All rates are defined in `Config.h`.

---

## Repository layout

```
NORVI-X-DAQ/
├── NORVI_X_DAQ.ino   Application logic. No edits required for normal use.
├── Config.h          Site settings: identity, network, broker, pins, intervals.
├── IOList.h          I/O tag database: which signal is on which channel, units, scaling.
└── README.md
```

Workflow for an integrator:

1. Set identity, network, and broker details in `Config.h`.
2. List the connected sensors and instruments in `IOList.h`.
3. Compile and upload `NORVI_X_DAQ.ino` unchanged.

---

## Dependencies

Install through the Arduino Library Manager unless noted otherwise.

- PubSubClient — Nick O'Leary
- Adafruit ADS1X15
- RTClib — Adafruit
- ModbusMaster — Doc Walker
- clsPCA9555
- PCA9539
- PCA9536D
- TFT_eSPI (required only if the on-board display is enabled)
- CST816S (touch controller, used with the display)

The Wi-Fi, Ethernet, SD, SPI, and Wire libraries ship with the ESP32 Arduino core.

The PCA9539, PCA9536D, clsPCA9555, TFT_eSPI configuration, and CST816S drivers used by
NORVI X are available in the platform repository:
https://github.com/NORVIControllers/NORVI-X-Version-01

`TFT_eSPI` must use the NORVI X display configuration (`User_Setup`). If the display is not
required, set `ENABLE_DISPLAY 0` in `Config.h` and the TFT and touch libraries can be omitted.

---

## Build and flash

Arduino IDE board settings:

| Setting          | Value                                  |
|------------------|----------------------------------------|
| Board            | ESP32S3 Dev Module                     |
| Flash Size       | 16 MB                                  |
| PSRAM            | OPI PSRAM                              |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) or any ≥3 MB app |
| Upload Speed     | 921600                                 |

Open the three files as tabs in one sketch folder, set the values in `Config.h` and
`IOList.h`, then upload. Open the Serial Monitor at 115200 baud to confirm start-up.

---

## Configuration

All site-specific settings are in `Config.h`. The most common changes:

```c
// Identity — used in the MQTT topic tree and payload
#define SITE_ID    "plant1"
#define LINE_ID    "lineA"
#define DEVICE_ID  "norvi-x-daq-01"

// Transport: 1 = W5500 Ethernet (NORVI X1), 0 = Wi-Fi
#define USE_ETHERNET 0
#define WIFI_SSID  "YOUR_WIFI_SSID"
#define WIFI_PASS  "YOUR_WIFI_PASSWORD"

// MQTT broker
#define MQTT_HOST  "192.168.1.100"
#define MQTT_PORT  1883
#define MQTT_USER  ""        // leave empty for anonymous
#define MQTT_PASS  ""

// Publish interval — the single send-rate control
#define PUBLISH_MS 5000

// Disable Modbus if no RS-485 instruments are connected
#define ENABLE_MODBUS 1
```

Acquisition rates, reconnect intervals, RS-485 baud, the NTP server, the SD buffer cap,
and the expansion-module I²C address tables are all set in the same file.

The RTC is maintained in UTC and synchronised from NTP when the network is available.
Timestamps are emitted in ISO-8601 form, for example `2026-06-26T06:49:08Z`.

---

## Defining the I/O list

`IOList.h` is the only file that describes the physical wiring. Each entry maps a channel
to a JSON key, an engineering unit, and a scaling range. Runtime fields are left out of the
initialisers and default to zero.

Analogue inputs — `{ enabled, key, unit, module, channel, mode, engMin, engMax }`:

```c
AiChannel aiList[] = {
  { true, "coolant_press", "bar", 0, 1, AI_MODE_420MA, 0.0f, 10.0f },
  { true, "hyd_press",     "bar", 0, 2, AI_MODE_420MA, 0.0f, 250.0f },
};
```

`engMin` and `engMax` map the 4 mA and 20 mA endpoints (or 0 V and 10 V) to the value the
sensor represents at full scale.

Digital inputs — `{ enabled, key, module, pin, invert }`. Set `invert` for active-low
signals such as a normally-closed overload contact:

```c
DiChannel diList[] = {
  { true, "conveyor_run", 0, 0, false },
  { true, "motor_ovld",   0, 3, true  },
};
```

Modbus registers — `{ enabled, key, unit, slaveId, fc, addr, dtype, wordSwap, scale, offset, pollMs }`:

```c
ModbusReg mbList[] = {
  { true, "active_power", "kW", 1, MB_FC_INPUT, 0x0034, MB_FLOAT32, false, 0.001f, 0, 1000 },
};
```

Supported data types are `MB_U16`, `MB_S16`, `MB_U32`, and `MB_FLOAT32`. Use `wordSwap` if
the instrument transmits 32-bit values low-word first. The array sizes are counted
automatically; no separate count constants need to be maintained.

To add a second AI4 or DI16 module, set its `module` index to 1 in the relevant entries and
confirm the second address in the `ADS_ADDR` / `PCA_ADDR` tables in `Config.h`.

---

## MQTT output

Topic tree:

```
norvi/<site>/<line>/<device>/data     telemetry (JSON)
norvi/<site>/<line>/<device>/status   "online" / "offline" (retained, Last Will)
norvi/<site>/<line>/<device>/cmd      command input
```

Telemetry payload:

```json
{
  "device": "norvi-x-daq-01",
  "site": "plant1",
  "line": "lineA",
  "ts": "2026-06-26T06:49:08Z",
  "ai":  { "coolant_press": 4.81, "hyd_press": 118.0 },
  "di":  { "conveyor_run": 1, "motor_ovld": 0 },
  "cnt": { "conveyor_run": 12, "cycle_pulse": 18423 },
  "mb":  { "active_power": 12.450 }
}
```

`ai` holds scaled engineering values; a channel reads `null` when an open loop or out-of-range
condition is detected. `di` is the live debounced state. `cnt` is the rising-edge count per
channel, useful for cycle and part counting. `mb` holds decoded Modbus values, or `null` when
an instrument has not responded.

A retained Last Will message on the `status` topic lets the broker report node availability
immediately if a node drops off the network. Publishing `publish_now` to the `cmd` topic
triggers an immediate telemetry send.

The payload is plain MQTT with JSON, so it integrates with Grafana (through an MQTT data
source or an InfluxDB bridge), NORVI Cloud, Node-RED, AWS IoT Core, or Azure IoT Hub without
firmware changes.

---

## Store-and-forward

When the broker is unreachable, each record is appended to `daq_buffer.jsonl` on the microSD
card. On reconnection the firmware replays buffered records in chronological order, up to a
configurable batch size per cycle, then resumes live publishing. The buffer file is capped
(2 MB by default) to protect the card.

The microSD card is initialised at start-up. If it is not present or fails to mount, the
firmware retries periodically without requiring a reboot, and buffering begins as soon as the
card becomes available.

For store-and-forward to work, the card must be formatted FAT32. Cards of 32 GB or smaller
are the most reliable.

---

## Reliability behaviour

The application is built to run unattended:

- Non-blocking main loop. No layer stalls another; the publish cadence stays consistent.
- Network reconnect. Wi-Fi or Ethernet loss is detected and reconnection is attempted on a
  fixed interval, with each attempt logged.
- MQTT reconnect. The client reconnects on its own interval and flushes any buffered data
  immediately after a successful connection.
- Modbus dead-slave backoff. After a configurable number of consecutive failures, an
  unresponsive instrument is parked for a fixed period instead of being polled every cycle.
  This prevents a single offline instrument from delaying the rest of the acquisition loop.
  A recovered instrument is logged and resumes normal polling.
- SD recovery. A card inserted after boot is picked up automatically.

---

## Serial diagnostics

At 115200 baud the node reports its state during start-up and operation:

```
=== NORVI X Smart Factory DAQ ===
FW 1.1.0  Device norvi-x-daq-01
[I2C]  device @0x27
[I2C]  device @0x48
[I2C]  device @0x68
[BRD] PCA9536 OK
[BRD] RTC DS3231 OK
[SD ] mounted, 7600 MB
[NET] Wi-Fi connected, IP: 192.168.1.42
[NET] RSSI: -58 dBm
[TIME] RTC synced from NTP
[MQTT] connect attempt #1 ... connected
```

The status LEDs on the CPU module indicate a running heartbeat and link/broker health.

---

## Troubleshooting

SD card does not mount
: Confirm the card is FAT32 and fully seated; 32 GB or smaller is recommended. The firmware
  logs each re-init attempt and recovers automatically once the card is readable.

Analogue channels read `null`
: Expected when no transmitter is connected, or when the loop current is below the open-loop
  threshold. Once a 4–20 mA loop is wired, values appear scaled to the configured range.

A Modbus value stays `null`
: The instrument is not responding. Check the slave address, baud rate, register address, and
  RS-485 wiring including the 120 Ω termination at the end of the bus. The node logs when a
  slave is parked and when it recovers. Set `ENABLE_MODBUS 0` while no instruments are wired.

Publish interval looks irregular
: The send rate is `PUBLISH_MS`. If Modbus instruments are configured but absent, ensure
  `ENABLE_MODBUS 0` is set so timeouts do not affect timing during commissioning.

No data in Grafana
: Verify the broker host and port, confirm the node prints `connected`, and subscribe to
  `norvi/#` with an MQTT client to confirm the device is publishing.

---

## Reference documents

- NORVI X CPU (ESPS3-X1) datasheet — https://norvi.io/docs/norvi-x-cpu-esps3-x1-datasheet/
- NORVI X DI16 datasheet — https://norvi.io/docs/norvi-x-di16-datasheet/
- NORVI X AI4 datasheet — https://norvi.io/docs/norvi-x-ai4-datasheet/
- NORVI X platform repository — https://github.com/NORVIControllers/NORVI-X-Version-01

---

## License

Released under the MIT License. See `LICENSE` for details.
