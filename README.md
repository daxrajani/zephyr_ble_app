# Dax BLE Sensor Node

[![Zephyr](https://img.shields.io/badge/RTOS-Zephyr-7341FF.svg)](https://www.zephyrproject.org/)
[![Board](https://img.shields.io/badge/Board-nRF52840--DK-00A9CE.svg)](https://www.nordicsemi.com/Products/Development-hardware/nrf52840-dk)
[![Language](https://img.shields.io/badge/Language-C-00599C.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![BLE](https://img.shields.io/badge/Wireless-BLE-0082FC.svg)](https://www.bluetooth.com/learn-about-bluetooth/tech-overview/)

BLE peripheral firmware for `nrf52840dk/nrf52840` built with Zephyr RTOS.  
Implements a custom 128-bit GATT service for sensor telemetry and runtime configuration, with flash-backed settings persistence, encrypted access control, and Eddystone-URL broadcast support.

## Features

- Custom BLE service with:
  - Temperature characteristic (`read`, `notify`)
  - Humidity characteristic (`read`, `notify`)
  - Sampling interval characteristic (`read`, `write`)
- Extended advertising + automatic re-advertise on disconnect
- Background RTOS sensor update thread
- Per-characteristic CCC notification control
- Persistent sample rate using Zephyr Settings + NVS (`sensor/rate`)
- Startup-time error checks for BLE and settings initialization
- LE security flow with pairing/bonding callbacks and encrypted link upgrade
- Encrypted GATT permissions for characteristic reads/writes and CCC operations
- Eddystone-URL frame in advertising payload for app-less URL discovery

## Hardware / Software

- Board: `nRF52840-DK` (PCA10056)
- SDK/RTOS: Zephyr (NCS-compatible)
- Language: C
- Flash backend: NVS

## Repository Layout

- `src/main.c` - BLE service definition, callbacks, sensor thread, settings handler, app entrypoint
- `prj.conf` - Zephyr Kconfig options
- `CMakeLists.txt` - Application CMake target
- `native_sim_native_64.overlay` - Optional native simulation overlay

## Architecture

```mermaid
flowchart TD
    A[Mobile BLE Client] -->|Read / Write / Subscribe| B[Custom GATT Service]
    B --> C[Temperature Char]
    B --> D[Humidity Char]
    B --> E[Sample Rate Char]
    E -->|Write new rate| F[sample_rate_ms]
    F --> G[Sensor Thread]
    G -->|notify| C
    G -->|notify| D
    E -->|settings_save_one sensor/rate| H[NVS Flash]
    I[Boot] --> J[settings_load]
    J -->|restore rate| F
```

## Quick Start

### 1) Build

```powershell
west build -b nrf52840dk/nrf52840 -p always
```

### 2) Flash

```powershell
west flash -d build -r nrfjprog
```

## Demo

BLE service and characteristics in nRF Connect:

![nRF Connect BLE Service View](screenshot/IMG_0171.PNG)

UART runtime logs in Tera Term:

![Runtime Logs](screenshot/log_screenshot.png)

## Security (LE Secure Connections)

- `CONFIG_BT_SMP=y`, `CONFIG_BT_BONDABLE=y`, and `CONFIG_BT_SETTINGS=y` enabled in `prj.conf`
- Pairing callbacks implemented for passkey display, pairing completion, and failure diagnostics
- Connection security upgraded at runtime with `bt_conn_set_security(conn, BT_SECURITY_L2)`
- Custom characteristics require encrypted access (`BT_GATT_PERM_READ_ENCRYPT`, `BT_GATT_PERM_WRITE_ENCRYPT`)

## Eddystone-URL Broadcast

Advertising payload includes Eddystone service data (`0xFEAA`) with URL frame type (`0x10`), broadcasting a GitHub URL so nearby scanners can discover a link without opening the custom GATT service.

## Validate Persistence

1. Connect from a BLE client (e.g. nRF Connect mobile).
2. Write sample-rate characteristic to `500` (ms).
3. Reset/power-cycle the board.
4. Confirm log message:

```text
Loaded sample rate: 500 ms
```

## Validate Secure Access + Beacon

1. Connect from nRF Connect mobile and complete pairing when passkey is shown.
2. Confirm encrypted read/write works only after security is established.
3. In scanner view, verify Eddystone service data (`0xFEAA`) is present in advertising.

## UART Logs (Tera Term)

Open the DK virtual COM port with:

- Baud: `115200`
- Data bits: `8`
- Parity: `None`
- Stop bits: `1`
- Flow control: `None`

## Notes

- If flashing via `nrfutil` fails, use `-r nrfjprog`.
- If `nrfjprog` is missing, install nRF Command Line Tools and add its `bin` to `PATH`.
- If J-Link errors (`JLinkARM.dll -256`) appear, update SEGGER J-Link and reconnect via the DK `DEBUG` USB port.

## What Was Hard

- Intel wireless combo card interference caused intermittent scan/connect behavior during early validation, which looked like firmware instability until host-side radio behavior was isolated.
- Connection ownership bugs around `bt_conn_ref`/`bt_conn_unref` caused reconnect edge cases until lifecycle handling was tightened in connect/disconnect callbacks.
- A shared CCC state approach led to incorrect notification behavior across characteristics; splitting to per-characteristic flags fixed cross-talk.
- Native simulation (`bsim`) was useful for early BLE flow checks, but final debugging still required real hardware due to controller- and probe-specific behavior.

