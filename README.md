# ESP32-C6 Thread Border Router

OpenThread Border Router (OTBR) firmware for the ESP32-C6, which runs Thread natively (no external RCP) and bridges the Thread mesh to Wi-Fi. Discovered by Home Assistant via `_meshcop._udp` mDNS.

Ported from the [ESP32-C5 OTBR project](https://github.com/l0cut15/esp32c5-thread-border-router) with fixes for the IDF 6.1.0 `ot_examples_br` auto-start bugs.

## Hardware

- **Target SoC:** ESP32-C6 — single-die 802.15.4 + 2.4 GHz Wi-Fi 6
- **No external RCP required** — radio mode is `NATIVE`
- USB Serial/JTAG for flashing and console (`/dev/ttyACM0`)

## Build Requirements

| Requirement | Version / Detail |
|---|---|
| ESP-IDF | v6.1.0 |
| Target | `esp32c6` |
| Python | 3.8+ (IDF bundled) |
| CMake | 3.16+ |

## Quick Start

```bash
# Source the IDF environment
source ~/esp/esp-idf-v6.1.0/export.sh

# Fetch managed component dependencies
idf.py update-dependencies

# Build, flash, and open serial monitor
idf.py -p /dev/ttyACM0 build flash monitor
```

Full clean rebuild (after target or `sdkconfig.defaults` changes):

```bash
rm -rf build && idf.py update-dependencies && idf.py set-target esp32c6 && idf.py build
```

### Wi-Fi Configuration

Set your SSID and password before building:

```bash
idf.py menuconfig
# → Example Connection Configuration → WiFi SSID / Password
```

Or set directly in `sdkconfig.defaults`:

```
CONFIG_EXAMPLE_WIFI_SSID="YourSSID"
CONFIG_EXAMPLE_WIFI_PASSWORD="YourPassword"
```

## Boot Behaviour

Thread starts automatically on every boot — no manual commands needed:

1. WiFi connects (~7 s)
2. Border router backbone initialises (~8 s)
3. Thread attaches: `disabled → detached → child → router` (~40 s)
4. mDNS advertises `esp-ot-br._meshcop._udp` (or `esp-ot-br-2` if another BR is already using that name)

## Key Configuration (`sdkconfig.defaults`)

| Option | Value | Reason |
|---|---|---|
| `CONFIG_IDF_TARGET` | `esp32c6` | single-die Thread + Wi-Fi |
| `CONFIG_OPENTHREAD_RADIO_NATIVE` | `y` | no external RCP |
| `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` | `y` | required for single-die coexistence |
| `CONFIG_OPENTHREAD_NETWORK_AUTO_START` | `y` | auto-start Thread on boot |
| `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` | `y` | console via USB-JTAG |

## First Flash: Join an Existing Thread Network

After flashing, open the serial monitor and configure the Thread dataset once. The dataset persists in NVS across all subsequent reboots.

Get your network's active dataset TLV from:
- **Home Assistant:** Settings → Devices & Services → Thread → (⋮) → Manage → Copy dataset
- **Another OT device:** `ot dataset active -x`

Then at the `esp32c6>` console prompt:

```
ot thread stop
ot ifconfig down
ot dataset set active <TLV hex string>
ot dataset commit active
ot ifconfig up
ot thread start
```

Wait ~40 s, then verify:

```
ot state          # → router
ot netdata show   # → prefixes with border router flags
```

## Verify Border Router is Active

From any host on the same Wi-Fi network:

```bash
avahi-browse -t _meshcop._udp
# Should show esp-ot-br (or esp-ot-br-2) alongside any Apple TV / HomePod
```

The device will appear in **Home Assistant → Settings → Devices & Services → Thread** automatically.

## Useful Console Commands

```
ot state          # router / child / leader / disabled
ot netdata show   # routes and prefixes published to the mesh
ot ipaddr         # assigned IPv6 addresses
ot rloc16         # RLOC (mesh address)
ot neighbor table # other Thread devices seen
ot router table   # all routers in the partition
ot dataset active -x  # export dataset TLV (to share with other BRs)
```

## Component Dependencies

Managed components are downloaded by `idf.py update-dependencies` and are not committed to the repo.

- `espressif__esp_ot_cli_extension` — OT CLI extensions
- `espressif__mdns` — mDNS for `_meshcop._udp` advertisement
- `espressif__ethernet_init` — pulled in by `protocol_examples_common` in IDF 6.1.0
- `espressif__led_strip`, `espressif__iperf[-cmd]` — optional utilities

Versions are pinned in `dependencies.lock`.

## Upstream Bug Fixes (`components/ot_examples_br/`)

The upstream IDF `ot_examples_br.c` has two bugs that prevent Thread from auto-starting after reboot. Both are fixed in the local component override at `components/ot_examples_br/`:

**Bug 1 — Missing `ot_network_auto_start()` call**
`ot_br_init()` calls `esp_openthread_border_router_init()` but never calls `ot_network_auto_start()`. Without it, Thread stays in `DISABLED` role and `_meshcop._udp` is never advertised.

**Bug 2 — Deadlock**
`ot_network_auto_start()` acquires the OpenThread lock internally. The upstream code would call it while already holding the lock, causing a silent deadlock. The fix calls it after `esp_openthread_lock_release()`.

`main/idf_component.yml` points to the local override:
```yaml
ot_examples_br:
  path: ../components/ot_examples_br
```
