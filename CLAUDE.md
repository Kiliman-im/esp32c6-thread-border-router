# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C6 OpenThread Border Router (OTBR) firmware built on ESP-IDF v6.1.0. The C6 runs Thread natively (no external RCP) and bridges the Thread mesh to Wi-Fi for Home Assistant integration via mDNS discovery.

- **Target:** `esp32c6` — native 802.15.4 + Wi-Fi on one die
- **ESP-IDF:** `~/esp/esp-idf-v6.1.0` (use alias `get-idf610`)
- **Device serial:** `/dev/ttyACM0` (USB Serial/JTAG)
- **Network:** SSID `VillaP23`, device IP `10.10.11.170`
- **mDNS name:** `esp-ot-br-2` (auto-incremented; `esp-ot-br` is the C5)

## Build Commands

```bash
# Source IDF environment first (required every shell session)
# IMPORTANT: source without piping — pipe creates subshell, env doesn't persist
source ~/esp/esp-idf-v6.1.0/export.sh > /tmp/idf610.log 2>&1

# Build, flash, and monitor
idf.py -p /dev/ttyACM0 build flash monitor

# Full clean rebuild (after target or sdkconfig.defaults changes)
rm -rf build && idf.py update-dependencies && idf.py set-target esp32c6 && idf.py build

# menuconfig — adjust Kconfig options interactively
idf.py menuconfig

# Save current config as the new defaults
idf.py save-defconfig
```

**Serial capture** (non-interactive — `idf.py monitor` requires a TTY):
```bash
stty -F /dev/ttyACM0 115200 sane && timeout 30 cat /dev/ttyACM0 > /tmp/boot.log &
```

## Key Configuration (`sdkconfig.defaults`)

| Option | Value | Why |
|---|---|---|
| `CONFIG_IDF_TARGET` | `esp32c6` | single-die Thread + Wi-Fi |
| `CONFIG_OPENTHREAD_RADIO_NATIVE` | `y` | no external RCP needed |
| `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` | `y` | required for single-die coexistence |
| `CONFIG_OPENTHREAD_NETWORK_AUTO_START` | `y` | auto-start Thread on boot |
| `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` | `y` | console via USB-JTAG, not UART |
| `CONFIG_LWIP_IPV6_*` | various | IPv6 forwarding + routing hooks |

## Architecture

```
main/esp_ot_br.c                       — app_main: init NVS, netif, mDNS, eventfds,
                                         calls esp_openthread_start() (blocks on OT task)
main/esp_ot_config.h                   — radio/host/port config macros
main/Kconfig.projbuild                 — project-level Kconfig (HW RCP reset option)
main/idf_component.yml                 — component dependencies
components/ot_examples_br/             — LOCAL OVERRIDE of upstream IDF component (see below)
dependencies.lock                      — pinned component versions
```

**Thread initialization flow:**
1. `app_main` → initializes peripherals, starts mDNS hostname `esp-ot-br`
2. `esp_openthread_start()` → starts the OpenThread FreeRTOS task
3. `esp_openthread_border_router_start()` → spawns `ot_br_init` task
4. `ot_br_init` connects WiFi, sets backbone netif, calls `esp_openthread_border_router_init()`
5. After releasing lock: `ot_network_auto_start()` → starts Thread interface → role: `disabled → detached → child → router`
6. Role change triggers mDNS publication of `_meshcop._udp`

**Radio mode** is selected at compile time via `esp_ot_config.h`:
- `CONFIG_OPENTHREAD_RADIO_NATIVE` → `RADIO_MODE_NATIVE` (this project)
- `CONFIG_OPENTHREAD_RADIO_SPINEL_UART` → UART RCP on GPIO 4/5 at 460800 baud
- default → SPI RCP

## Local Component Override: `components/ot_examples_br/`

The upstream IDF `ot_examples_br.c` `ot_br_init()` omits the `ot_network_auto_start()` call after `esp_openthread_border_router_init()`. Without it, Thread stays `DISABLED` and `_meshcop._udp` is never advertised.

**Two bugs fixed in the local override:**
1. Missing `ot_network_auto_start()` call — Thread never starts
2. Must call it AFTER `esp_openthread_lock_release()` — `ot_network_auto_start()` acquires the lock internally; calling it while holding the lock deadlocks the `ot_br_init` task silently

`main/idf_component.yml` points to the local override:
```yaml
ot_examples_br:
  path: ../components/ot_examples_br
```

## Verify Working State

```bash
# All three border routers should appear:
avahi-browse -t _meshcop._udp
# esp-ot-br    = ESP32-C5
# esp-ot-br-2  = ESP32-C6 (this device)
# Disney ATV   = Apple TV
```

From the device console:
```
ot state       # should show "router"
ot netdata show  # should show prefixes with border router flags
```

## Component Dependencies

Managed components live in `managed_components/` and must not be edited directly.

- `ot_examples_br` — **local override** at `components/ot_examples_br/` (fixes auto-start)
- `ot_examples_common` — console + CLI + `ot_network_auto_start()`
- `ot_led` — LED state indicator (optional)
- `protocol_examples_common` — Wi-Fi connection helpers
- `espressif/ethernet_init` — pulled in by `protocol_examples_common` in IDF 6.1.0; fetched automatically by `idf.py update-dependencies`

The `dependencies.lock` pins all versions; re-run `idf.py update-dependencies` to refresh.
