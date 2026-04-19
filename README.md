# ESPHome NTP Server

Host a GPS-disciplined NTP server from an ESP32. Uses the GPS module's PPS (Pulse Per Second)
signal to discipline the system clock to sub-millisecond accuracy, then serves NTP to your
local network at stratum 1.

## Features

- **PPS-disciplined clock**: ISR captures the PPS edge in hardware, correcting the system clock
  every second
- **ISR latency compensation**: detects and subtracts UART preemption of the PPS interrupt
- **Smooth correction**: ESP-IDF builds use `adjtime()` to slew without jumps; Arduino builds
  use `settimeofday()` with an accumulated crystal drift estimate
- **Honest NTP responses**: stratum 1, precision 2^-20 (~1 µs), fractional-second timestamps,
  distinct receive/transmit timestamps
- **Unsynchronized guard**: advertises stratum 16 / LI=3 when GPS lock is lost, preventing
  clients from syncing to bad time

## Hardware

- ESP32 with Ethernet (tested on esp-wrover-kit + LAN8720)
- u-blox GPS module (or any module with NMEA over UART + PPS output)
- PPS pin wired to any available ESP32 GPIO

## Basic Usage (GPS + PPS)

```yaml
uart:
  rx_pin: GPIO3
  tx_pin: GPIO1
  baud_rate: 9600

gps:
  latitude:
    name: "Latitude"
  longitude:
    name: "Longitude"
  altitude:
    name: "Altitude"
  satellites:
    name: "Satellites"

time:
  - platform: gps_pps
    id: gps_time
    pps_pin: GPIO34        # replace with your PPS GPIO

ntp_server:
  time_id: gps_time

external_components:
  source:
    type: git
    url: https://github.com/gdave44/ESPHome-NTP-Server
  refresh: 30s
  components: [ ntp_server, gps_pps ]
```

## Optional Sensors

The `gps_pps` platform exposes two diagnostic sensors for Home Assistant:

```yaml
time:
  - platform: gps_pps
    id: gps_time
    pps_pin: GPIO34
    clock_offset:
      name: "Clock Offset"   # µs between system clock and GPS truth at each PPS edge
    pps_drift:
      name: "PPS Drift"      # raw unfiltered drift measurement for diagnostics
```

`clock_offset` is the filtered value NTP clients actually see. In normal operation it should
stay within ±50 µs on Arduino builds and smaller on ESP-IDF builds.

## Finding Your PPS GPIO

If you don't remember which GPIO the PPS wire is connected to, flash the firmware and watch
the logs. A correct PPS pin produces:

```
[gps_pps] GPS coarse time set: 2026-04-18 21:00:00 (waiting for PPS)
[gps_pps] PPS-disciplined time synchronized
[gps_pps] PPS #1 epoch=1745010001 drift=0 µs
[gps_pps] PPS #2 epoch=1745010002 drift=42 µs
```

A wrong pin shows only the coarse GPS time line and nothing else.

## Startup Sequence

1. GPS NMEA sentences arrive over UART → coarse time is set (accuracy ~500 ms)
2. First PPS pulse → system clock hard-snapped to GPS second boundary
3. Subsequent PPS pulses → clock corrected and drift tracked each second
4. NTP server begins responding to clients with stratum 1 once step 2 completes

## Legacy Usage (no PPS)

The original `platform: gps` time source still works without PPS, but accuracy is limited
to the NMEA sentence transmission delay (~100–500 ms at 9600 baud). Not recommended for
NTP serving.

```yaml
time:
  - platform: gps
    id: gps_time

ntp_server:

external_components:
  source:
    type: git
    url: https://github.com/gdave44/ESPHome-NTP-Server
  refresh: 30s
  components: [ ntp_server ]
```

## Full Example

See [`nts-32.yaml`](nts-32.yaml) for a complete working configuration using the esp-wrover-kit
with LAN8720 Ethernet.
