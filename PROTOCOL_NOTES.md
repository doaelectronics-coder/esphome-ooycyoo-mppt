# OOYCYOO remote-display protocol notes

## Transport

- STM8S003F3 remote display UART: `9600 8N1`.
- UART connects to a 7LB184 half-duplex RS485 transceiver.
- RJ45 port: pin 4 (blue) is RS485 A(+), pin 5 (blue/white) is RS485 B(-).
  Pins 1-3 carry +12 V and pins 6-8 are a common
  ground (one connection is enough). Only pins 4 and 5 carry data. Pins 1-3
  appear common inside the original display; tying them together changed
  nothing, and the controller answers polls with only A, B and ground wired.
  Tested wiring: controller ground to the RS485-side GND of a MAX485-based
  auto-direction module, with no separate wire to the ESP32 ground. The
  module's isolation is unknown.
- Display polls at approximately `0.84 s`.
- Frame: `0x55`, 30-byte payload, one checksum byte.
- Checksum: sum of the 30 payload bytes modulo 256. The `0x55` header is not
  included.
- This is not Modbus and has no CRC.

### Startup exchange

The physical display sends one initialization frame before normal polling:

```text
55 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

The controller replies, and the display then starts its recurring setting/poll
frame. The ESPHome component reproduces this one-time startup frame when
polling is first enabled.

## Response payload map

Offsets exclude the `0x55` header.

| Bytes | Encoding | Meaning |
|---|---|---|
| `R[0..1]` | big-endian, 0.1 V | Controller-reported PV-off threshold |
| `R[3..4]` | little-endian, 0.1 V | Controller-reported load-off threshold |
| `R[5]` | byte, 0.1 V | Controller-reported load-on threshold (low byte) |
| `R[6]` | unknown | Not part of load-on in replies: MPPT 1 answered `FC EF` to the display's `FC 00` (25.2 V) in every 2026-09-10 capture and `EF` never followed a setting change; later seen as `A0`. The display's own polls send load-on little-endian in `R[5..6]`. |
| `R[7]` | hours | Controller-reported evening setting |
| `R[8]` | hours | Controller-reported interval setting |
| `R[9]` | hours | Controller-reported dawn setting |
| `R[10..11]` | big-endian, 0.1 V | PV voltage |
| `R[12..13]` | big-endian, 0.1 A | Battery charging current |
| `R[14..15]` | big-endian, 0.1 V | Battery voltage |
| `R[16..17]` | big-endian, 0.1 C | Battery temperature |
| `R[18]` | byte | `0` load on, `1` load off |
| `R[20..21]` | big-endian, 0.1 A | Load current |
| `R[22].0` | bit | Battery-to-load path X/fault icon |
| `R[23].0` | bit | PV-to-battery path X/fault icon |
| `R[24..25]` | unknown | No repeatable visible effect; leave zero |
| `R[28..29]` | big-endian | Total energy; LCD displays `raw / 10` kWh |

The display is the bus master and sends desired settings in each poll. Response
bytes `R[0..9]` report the controller's current stored settings. They may remain
at the old values during the nine pre-commit polls; this is expected and is not
a refusal.

## Confirmed write and acknowledgement sequence

The 2026-09-10 physical-display capture includes a PV-off change from 27.5 V
(`01 13`) to 27.8 V (`01 16`):

1. The outgoing display setting changes to `01 16`.
2. Controller replies remain `01 13` throughout the ordinary pre-commit polls.
3. The display sends the changed setting with `R[10] = 1` on the tenth poll.
4. The controller's immediately following response changes to `01 16`.

A captured load-on change shows the same pattern. The response after the commit
poll is therefore a real acknowledgement, and software must compare response
settings to requested settings rather than echoing its transmit buffer.

## Load timers (Evening, Interval, Dawn)

From the controller manual (page 6), all in hours:

- **Evening** `R[7]`: `24` = load always on; `0` = street-lamp mode, on at
  dusk and off at dawn; `1`-`23` = on at dusk for that many hours.
- **Interval** `R[8]`: after the evening period, hours off before the dawn
  period (`0` = none).
- **Dawn** `R[9]`: hours the load comes back on before dawn (`0` = none).

With Evening `24` the other two have no effect. There is **no rule that the
three add up to 24**: in the 2026-09-10 capture the display committed
`20/2/2`, then `24/2/2` (28 h), and the controller acknowledged both in its
next reply; while editing, the display passed through `20/24/0` and wraps
`0` down to `24`. Timers are written and acknowledged exactly like the
voltages (nine polls, then the `R[10] = 1` commit).

## Derived values

```text
Load voltage = battery voltage when R[18] = 0, otherwise 0 V.
PV current = battery_voltage * battery_charging_current / PV_voltage
             when PV voltage >= battery voltage + 1.0 V; otherwise 0 A.
Battery percent is estimated locally from battery voltage and the
controller-reported load-off/PV-off thresholds.
```

PV voltage and battery charging current are direct controller telemetry. PV
current and solar power are estimates because the captured response does not
contain a separate PV-current field.

## Commands/events

```text
Short Load press: R[13] = 1 for one outgoing frame, then 0.

Long Load hold/reset:
  reset settings to 13.8 V / 10.7 V / 12.6 V / 24 h / 0 h / 0 h
  send R[11] = 1 for one frame
  send R[11] = 2 for approximately three frames
  return R[11] = 0
```

The physical remote uses PA1 and PA2 as activity LEDs:

```text
PA1 toggles while battery current is >= 0.6 A.
PA2 toggles while load current is >= 0.6 A.
```
