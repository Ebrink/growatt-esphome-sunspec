# growatt-sunspec

An [ESPHome](https://esphome.io) configuration and external component that reads live data from a **Growatt 3600 TL-X** solar inverter over RS485/Modbus RTU and re-publishes it as a **SunSpec-compliant Modbus TCP server** on port 502.

This lets any SunSpec-aware system — a Victron Cerbo GX / VenusOS, SolarAssistant, Home Assistant Energy Dashboard, or a custom monitoring tool — discover and poll the inverter over Wi-Fi using the industry-standard SunSpec protocol, without needing a direct RS485 connection.

---

## Features

- Full **SunSpec Model 1** (Common) block — manufacturer, model, serial number
- **SunSpec Model 101** (Single-phase AC inverter) — AC voltage, current, power, frequency, lifetime energy, temperature, operating state
- **SunSpec Model 160** (Multiple MPPT Extension) — per-string DC voltage, current and power for both PV1 and PV2 independently
- **Writable power limit** — a SunSpec client can write `WMaxLimPct` to throttle inverter output, which propagates to the Growatt's own holding register
- All inverter data simultaneously published to **Home Assistant** via the ESPHome native API
- Optimised for the **ESP8266 ESP-07** that is found in the Growatt ShineWifi-X module

---

## Hardware

| Component | Detail |
|---|---|
| Microcontroller | ESp8266 (ESP8266, 2 MB flash) |
| Inverter | Growatt 3600 TL-X (single-phase, dual MPPT) |
| Interface | Growatt ShineWifi-X |

The ShineWifi-X uses the Growatt USB-port and connects to your Wi-Fi network. It acts as a bridge in both directions: polling the inverter every 10 seconds over Modbus RTU, and serving any TCP client that connects on port 502.

---

## File layout

```
├── growatt.yaml            ESPHome device configuration
└── components/
    └── sunspec_server/
        ├── __init__.py             ESPHome component registration (Python)
        └── sunspec_server.h        SunSpec TCP server (C++)
```

---

## How it works

### 1. Modbus RTU polling (ESP → Growatt)

ESPHome's built-in `modbus_controller` component polls the Growatt's input and holding registers on a configurable interval (default 10 s). Each register maps to an ESPHome `sensor`, `number`, or `select` entity, which ESPHome keeps up to date and publishes to Home Assistant automatically.

```
Growatt 3600 TL-X
      │  RS485
      ▼
   ShineWifi-X
  (Modbus RTU master, 115200 baud)
```

Slow-changing values (temperatures, daily/total energy, fault codes, configuration) use a `skip_updates` multiplier so they are only read every ~60 seconds rather than every poll, reducing bus traffic.

### 2. SunSpec TCP server (TCP client → ESP)

The `sunspec_server` external component runs a non-blocking TCP server on port 502. When a client connects and sends a Modbus TCP read request, the component:

1. Updates a 348-byte in-memory register image with the latest sensor values
2. Slices out the requested registers and sends them back in a valid Modbus TCP response frame

```
SunSpec client
(Victron, SolarAssistant, mbpoll, etc.)
      │  Modbus TCP port 502
      ▼
   ShineWifi-X
  (SunSpec server)
```

Only one TCP client is served at a time. The server accepts a new connection only after the previous client disconnects.

### 3. SunSpec register map

The register image implements the full SunSpec model chain, which a compliant client discovers by reading from address 40001 and walking the chain until it hits the end marker:

```
Wire addr    SunSpec reg   Content
──────────   ───────────   ──────────────────────────────────────
40000-40001  40001-40002   "SunS" magic identifier
40002-40068  40003-40069   Model 1  — Common (manufacturer, model, S/N)
40069-40120  40070-40121   Model 101 — Single-phase AC inverter
40121-40170  40122-40171   Model 160 — Multiple MPPT (PV1 + PV2 strings)
40171-40172  40172-40173   End of map marker (DID=0xFFFF)
```

> **Note on addressing:** SunSpec register numbers are 1-based. Modbus TCP wire addresses are 0-based (`wire = register − 1`). Tools like `mbpoll` use 1-based register numbers, so always pass `wire_address + 1` as the `-r` argument.

### 4. Model 101 — single-phase AC inverter

Reports the AC output side: voltage (L-N), current, real power, frequency, apparent power, and lifetime AC energy. The DC summary fields (`I_DC_Current`, `I_DC_Voltage`, `I_DC_Power`) carry the totals across both strings — useful for clients that don't understand Model 160.

### 5. Model 160 — multiple MPPT extension

Reports each PV string independently. The model header declares shared scale factors once, then each module block carries the per-string values:

| Module | Input_ID | Source |
|---|---|---|
| Module 1 | 1 | PV1 string — voltage, current, power |
| Module 2 | 2 | PV2 string — voltage, current, power |

Clients that understand Model 160 (e.g. Victron VenusOS ≥ 3.x) will display both strings separately. Clients that only understand Model 101 see the combined DC totals.

### 6. Writable power limit

Two registers in the Model 101 block are writable:

| Wire addr | SunSpec reg | Field | Description |
|---|---|---|---|
| 40111 | 40112 | `WMaxLimPct` | Power limit 0–100%, encoded as integer × 100 (SF=−2, so 10000 = 100.00%) |
| 40112 | 40113 | `WMaxLim_Ena` | 0 = disabled, 1 = enabled |

A write to `WMaxLimPct` (FC06 or FC16) immediately updates the Growatt's **Max Output Power** holding register (address 3) via the `sunspec_power_limit` number entity. The `SunSpec Power Limit` entity in Home Assistant reflects the current value.

---

## Installation

### 1. Folder structure

Place the files so the `components/` folder sits next to your YAML:

```
/config/esphome/
├── growatt.yaml
└── components/
    └── sunspec_server/
        ├── __init__.py
        └── sunspec_server.h
```

Using the Home Assistant file editor, Samba share, or SSH — whichever you prefer.

### 2. Secrets

Add your Wi-Fi credentials to `/config/esphome/secrets.yaml`:

```yaml
wifi_ssid: "YourNetwork"
wifi_password: "YourPassword"
```

### 3. Framework requirement

The component uses the Arduino `WiFiServer` / `WiFiClient` API. Make sure your config specifies:

```yaml
esp8266:
  board: esp07s
  framework:
    type: arduino
```

### 4. Flash

From the ESPHome dashboard, click **Install** on the device. Or from the CLI:

```bash
esphome run growatt-sunspec.yaml
```

### 5. Verify startup

In the device logs you should see:

```
[I][sunspec:] SunSpec TCP on port 502 — M101 + M160 (2 MPPT), 174 regs
```

---

## Testing

### Check the SunS magic

```bash
mbpoll -t 4 -r 40001 -c 4 -1 192.168.x.x
```

Expected response:

```
[40001]:  21365   (0x5375 = 'Su')
[40002]:  28243   (0x6E53 = 'nS')
[40003]:  1               ← Common Model DID
[40004]:  65              ← Common Model length
```

### Read the Model 101 AC inverter block

```bash
mbpoll -t 4 -r 40072 -c 50 -1 192.168.x.x
```

Cross-check key values against the inverter display (remember scale factors):

| mbpoll ref | Field | Scale | Example raw | Decoded |
|---|---|---|---|---|
| 40072 | I_AC_Current | ÷10 | 142 | 14.2 A |
| 40087 | I_AC_Voltage_AN | ÷10 | 2305 | 230.5 V |
| 40085 | I_AC_Power | ×1 | 3200 | 3200 W |
| 40099 | I_AC_Frequency | ÷100 | 5000 | 50.00 Hz |
| 40108 | I_Status | — | 4 | MPPT (normal) |

### Read the Model 160 MPPT block

```bash
mbpoll -t 4 -r 40122 -c 52 -1 192.168.x.x
```

| mbpoll ref | Field | Expected |
|---|---|---|
| 40122 | Model DID | 160 |
| 40123 | Length | 48 |
| 40132 | Module 1 Input_ID | 1 (PV1) |
| 40133 | PV1 DcCurrent (÷100) | e.g. 823 = 8.23 A |
| 40134 | PV1 DcVoltage (÷10) | e.g. 3812 = 381.2 V |
| 40152 | Module 2 Input_ID | 2 (PV2) |
| 40153 | PV2 DcCurrent (÷100) | e.g. 756 = 7.56 A |

### Full SunSpec model walk with Python

```bash
pip install sunspec2
```

```python
import sunspec2.modbus.client as ss2

dev = ss2.SunSpecModbusClientDeviceTCP(ipaddr='192.168.x.x', ipport=502)
dev.connect()
dev.scan()

for model_id in dev.models:
    for model in dev.models[model_id]:
        model.read()
        print(f'\n=== Model {model_id} ===')
        print(model)

dev.close()
```

This performs the full discovery walk and prints every field with decoded values and units. Seeing Models 1, 101, and 160 all parsed correctly means the implementation is spec-compliant.

### Test the writable power limit

```bash
# Set limit to 50% (raw 5000, SF=−2 means 5000 × 0.01 = 50.00%)
mbpoll -t 4 -r 40112 192.168.x.x 5000

# Enable the limit
mbpoll -t 4 -r 40113 192.168.x.x 1

# Read back to confirm
mbpoll -t 4 -r 40112 -c 2 -1 192.168.x.x
```

Check the ESPHome logs for confirmation:

```
[I][sunspec:] WMaxLimPct=50.00%
[I][sunspec:] WMaxLim_Ena=1
```

And verify the **SunSpec Power Limit** entity in Home Assistant has updated to 50%.

---

## ESP8266 optimisation notes

The ESP-07 has no hardware floating-point unit and around 80 KB of usable heap. Several deliberate choices keep the component lean:

- **Register image pre-built at startup.** All constant bytes (SunSpec magic, model headers, NaN sentinels, scale factors, string data) are written once in `setup()`. Only the ~30 live-data positions are updated on each read request, rather than rebuilding the full 348-byte image from scratch each time.
- **No `powf()` or `roundf()`.** The only scale factors used are −2, −1, and 0, so scaling is done with integer multiply (×100, ×10, ×1) and a branchless `+0.5f` cast. This eliminates roughly 20 software floating-point library calls per request.
- **Fast `isnan()`.** Implemented as a direct IEEE-754 bit-field test via a `union`, avoiding a `libm` call.
- **PROGMEM strings.** Common Model string constants (`"Growatt"`, `"3600TL-X"`, etc.) and the status lookup table live in flash and are copied to the image once at boot, not kept in DRAM.
- **Buffers in BSS.** The 348-byte image, 260-byte RX buffer, and 260-byte TX buffer are class members (static storage), not stack allocations.
- **No `flush()`.** `setNoDelay(true)` disables Nagle's algorithm so data is sent immediately; calling `flush()` on top of that would just busy-wait for no benefit.
- **Wi-Fi power save disabled.** `power_save_mode: none` prevents the modem sleep cycles that can add 100+ ms of latency to TCP connections.

---

## SunSpec status mapping

The Growatt's integer status code is translated to the nearest SunSpec `I_Status` value:

| Growatt code | Growatt state | SunSpec state |
|---|---|---|
| 0 | Standby | 8 — Standby |
| 1 | Normal | 4 — MPPT |
| 2 | Discharge | 8 — Standby |
| 3 | Fault | 7 — Fault |
| 4 | Flash | 3 — Starting |
| 5 | PV Charging | 4 — MPPT |
| 6 | AC Charging | 4 — MPPT |
| 7–10 | Combined/Bypass variants | 4 — MPPT |
| 11 | Bypass | 5 — Throttled |
| 12 | PV Charge and Discharge | 4 — MPPT |

The raw Growatt code is also available in `I_Status_Vendor` for clients that want the full detail.

---

## Limitations

- **One TCP client at a time.** A second connection attempt is rejected until the first client disconnects. This is standard Modbus TCP behaviour for embedded devices.
- **No per-string lifetime energy.** The Growatt does not expose energy totals per MPPT input, so `DcEnergy` in the Model 160 module blocks is reported as NaN. Lifetime AC energy is available in Model 101.
- **No reactive power or power factor.** The Growatt 3600 TL-X does not expose these over Modbus; the corresponding SunSpec fields are reported as NaN.
- **No RTC timestamp.** The Model 160 timestamp field is always 0. If timestamps matter, ESPHome's `homeassistant` time platform could be plumbed in as a future improvement.
- **Arduino framework only.** The component uses `ESP8266WiFi.h` (`WiFiServer` / `WiFiClient`). It will not compile under the ESP-IDF framework.
