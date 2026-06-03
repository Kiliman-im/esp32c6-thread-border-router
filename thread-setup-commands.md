# Thread Network Setup Commands

Open a serial terminal (`idf.py -p /dev/ttyACM0 monitor`) and use the `ot` prefix at the `esp32c6>` prompt.

## Auto-start on boot

Thread starts automatically after reboot — no manual steps needed. The sequence is:
1. WiFi connects (~7s)
2. Border router backbone initializes (~8s)
3. Thread attaches: `disabled → detached → child → router` (~40s)
4. mDNS advertises `esp-ot-br-2._meshcop._udp`

## First-time setup: join an existing Thread network

Get your network's active dataset TLV from:
- **Home Assistant:** Settings → Devices & Services → Thread → (3-dot) → Manage → Copy dataset
- **Another OT device:** `ot dataset active -x`

Then on the device console:

```
ot thread stop
ot ifconfig down
ot dataset set active <TLV hex>
ot dataset commit active
ot ifconfig up
ot thread start
```

Wait ~40s, then verify:
```
ot state        # router
ot netdata show # prefixes with border router flags
```

## Verify border router is advertising

```bash
avahi-browse -t _meshcop._udp
# esp-ot-br    = ESP32-C5
# esp-ot-br-2  = ESP32-C6 (this device)
# Disney ATV   = Apple TV
```

## Useful diagnostic commands

```
ot state          # router / child / leader / disabled
ot netdata show   # routes and prefixes published to mesh
ot ipaddr         # assigned IPv6 addresses
ot rloc16         # RLOC (mesh address)
ot neighbor table # other Thread devices seen
ot router table   # all routers in the partition
```

## Notes

- All commands require the `ot` prefix at the `esp32c6>` prompt
- Dataset (network credentials) persists in NVS across reboots
- Thread role `router` is correct for a border router — "border router" is a capability, not a role
- `ot state` = `disabled` after reboot means the auto-start fix is not active; reflash latest firmware
