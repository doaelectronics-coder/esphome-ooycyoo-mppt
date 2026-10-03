# ESPHome OOYCYOO MPPT bridge

An [ESPHome](https://esphome.io) external component that replaces the remote
display of OOYCYOO-style MPPT solar charge controllers with an ESP32. It reads
the controller's telemetry and settings over the display's RS485 link and
exposes them, and the controller's settings, to Home Assistant.

One ESP32 can serve several controllers, with one UART and one RS485
transceiver per controller.

> **Disclaimer.** This is an independent, unofficial project, not affiliated
> with or endorsed by OOYCYOO or any controller manufacturer. The protocol was
> worked out from bus captures and may be wrong for your hardware. Wrong
> settings written to a charge controller can damage batteries, loads or
> equipment, or cause fire. You use this software and any wiring entirely at
> your own risk. It is provided "as is", without warranty of any kind, and the
> authors accept no liability for any damage or loss; see [LICENSE](LICENSE).
> Never rely on it in place of the controller's, battery's or BMS's own
> protections.

## Hardware

- The controller's remote-display port: `9600 8N1` over half-duplex RS485.
  See [PROTOCOL_NOTES.md](PROTOCOL_NOTES.md).
- An ESP32 (the example uses `esp32dev`).
- One automatic-direction RS485 transceiver module per controller.

### Controller RJ45 pinout

| RJ45 pin | Usual wire colour | Signal |
|---|---|---|
| 1, 2, 3 | | **+12 V** |
| 4 | blue | RS485 **A (+)** |
| 5 | blue/white | RS485 **B (-)** |
| 6, 7, 8 | | Ground (-), all common |

Only pins 4 and 5 carry data. Connect them to the transceiver's A and B
terminals and nothing else.

Pins 6, 7 and 8 are all connected together, so one ground wire is enough.

In the tested setup, the controller's ground was wired to the GND
terminal on the RS485 side of the transceiver module (a MAX485-based
auto-direction board), and no separate wire ran from the controller's ground
to the ESP32. Whether the two grounds are really separate depends on the
module: the MAX485 itself is not isolated, so on most such boards the
RS485-side GND and the TTL-side GND are joined on the board. To check yours,
measure continuity between the two GND terminals. If it beeps, the controller
and ESP32 share a ground through the module.

> **Warning: 12 V on the port.** Pins 1-3 carry about 12 V. Never connect any
> pin other than 4 and 5 to the transceiver's A/B terminals or to an ESP32
> GPIO, as this can destroy the transceiver and the ESP32. Check with a
> multimeter before connecting anything, because cable colours vary and a
> crossed or non-standard cable moves the pins.

The GPIO pins in [example.yaml](example.yaml) are examples. Change them to
match your wiring.

## Safety: polling starts off

The original display sends all six threshold settings in **every** poll, so
the bridge does too. Each controller block therefore starts with
`poll_enabled: false`. To bring a controller up:

1. Copy `secrets.example.yaml` to `secrets.yaml` and fill in your Wi-Fi.
2. Set the GPIO pins for your board and transceivers.
3. Enter the six settings shown on each controller's own display.
4. Compile and flash with `poll_enabled: false`.
5. Check the wiring and settings, then set `poll_enabled: true` and flash
   again.

On its first boot with polling on, the bridge **adopts the controller's own
settings** once three replies agree. The YAML values are only placeholders
until then and are never pushed unasked. Settings you change from Home
Assistant are saved to flash and survive reboots. `Reset Settings` clears the
saved copy, so the next boot adopts the controller's settings again.

## Usage

```yaml
external_components:
  - source: github://doaelectronics-coder/esphome-ooycyoo-mppt@main
    components: [ooycyoo_mppt]
```

ESPHome needs the entity domains the component creates to be loaded, so keep
the empty `sensor:`, `binary_sensor:`, `switch:`, `button:` and `number:`
sections from the example. [example.yaml](example.yaml) is a complete
two-controller configuration.

### Options per controller

| Option | Meaning |
|---|---|
| `uart_id` | UART for this controller |
| `poll_enabled` | Start polling. Default off; see above |
| `auto_resync` | Resend the requested settings when the controller drifts out of sync (default `true`). `false` only reports the mismatch |
| `pv_off_voltage`, `load_off_voltage`, `load_on_voltage` | Placeholder thresholds, 0.1 V steps |
| `evening_hours`, `interval_hours`, `dawn_hours` | Placeholder load timers, 0-24 h |

### Entities

- **Telemetry:** PV voltage, charging current, battery voltage and
  temperature, load voltage and current, total energy, load-path and
  charge-path fault flags. PV current, solar power, charging power and battery
  percent are estimates derived from those.
- **`Requested ...` numbers:** the settings the bridge sends.
- **`Controller ...` sensors:** the settings decoded from the controller's
  last valid reply. Use these, not the numbers, to confirm a change.
- **`Settings Acknowledged`:** on when all six requested and reported settings
  match.
- **`Setting Update Pending`:** on while a change is being sent, awaiting
  confirmation, or the controller is out of sync.
- **`Setting Write Attempts`:** commits for the current request the controller
  has not yet confirmed.
- **`DC Load`** switch and **`Reset Settings`** button.

### How a setting change works

The display is the bus master. A change is sent in nine ordinary polls and
then one commit poll; the controller's next reply carries the new value. If
three replies in a row disagree with the request, the bridge resends it.
Unconfirmed writes are retried after 30 s, 2 min, 10 min, then hourly, since
each attempt is a settings commit on the controller.

## Load timers

From the controller manual: **Evening** `24` keeps the load always on, `0` is
dusk-to-dawn, `1`-`23` is on at dusk for that many hours. **Interval** is the
hours off after the evening period, and **Dawn** the hours back on before
dawn. The three do not have to add up to 24.

## Tests

`tests/host_sync_test.cpp` builds the component against minimal ESPHome
stand-ins and runs it against a simulated controller:

```sh
g++ -std=c++17 -Wall -Wextra -Itests/stubs -Icomponents tests/host_sync_test.cpp -o /tmp/ooycyoo_host_test && /tmp/ooycyoo_host_test
```

This is not an ESPHome build. Compile with ESPHome before flashing.

Developed and tested with ESPHome 2026.8.2.

## License

[MIT](LICENSE).

## Support

If this saved you some reverse-engineering, you can
[buy me a coffee on Ko-fi](https://ko-fi.com/cboxde). Donations are a
thank-you, not a support contract, and do not change the disclaimer above.
