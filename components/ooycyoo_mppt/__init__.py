import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, number, sensor, switch, uart
from esphome.const import CONF_ID, CONF_UART_ID

CODEOWNERS = []
DEPENDENCIES = ["uart"]
MULTI_CONF = True

ooycyoo_ns = cg.esphome_ns.namespace("ooycyoo_mppt")
OoycyooMPPT = ooycyoo_ns.class_("OoycyooMPPT", cg.PollingComponent, uart.UARTDevice)
OoycyooLoadSwitch = ooycyoo_ns.class_("OoycyooLoadSwitch", switch.Switch)
OoycyooResetButton = ooycyoo_ns.class_("OoycyooResetButton", button.Button)
OoycyooSettingNumber = ooycyoo_ns.class_("OoycyooSettingNumber", number.Number)

CONF_POLL_ENABLED = "poll_enabled"
CONF_PV_OFF = "pv_off_voltage"
CONF_LOAD_OFF = "load_off_voltage"
CONF_LOAD_ON = "load_on_voltage"
CONF_EVENING = "evening_hours"
CONF_INTERVAL = "interval_hours"
CONF_DAWN = "dawn_hours"
CONF_LOAD_SWITCH = "load_switch"
CONF_RESET_BUTTON = "reset_button"
CONF_AUTO_RESYNC = "auto_resync"
CONF_WRITE_ATTEMPTS = "setting_write_attempts"

NUMBERS = {
    "pv_off_number": (0, "V", 0.0, 99.9, 0.1),
    "load_off_number": (1, "V", 0.0, 99.9, 0.1),
    "load_on_number": (2, "V", 0.0, 99.9, 0.1),
    "evening_number": (3, "h", 0, 24, 1),
    "interval_number": (4, "h", 0, 24, 1),
    "dawn_number": (5, "h", 0, 24, 1),
}

SENSORS = {
    "pv_voltage": ("set_pv_voltage_sensor", "V"),
    "pv_current": ("set_pv_current_sensor", "A"),
    "solar_power": ("set_solar_power_sensor", "W"),
    "battery_voltage": ("set_battery_voltage_sensor", "V"),
    "battery_current": ("set_battery_current_sensor", "A"),
    "battery_charging_power": ("set_battery_charging_power_sensor", "W"),
    "battery_temperature": ("set_battery_temperature_sensor", "°C"),
    "battery_percent": ("set_battery_percent_sensor", "%"),
    "load_voltage": ("set_load_voltage_sensor", "V"),
    "load_current": ("set_load_current_sensor", "A"),
    "total_energy": ("set_total_energy_sensor", "kWh"),
}

SETTING_FEEDBACK_SENSORS = {
    "pv_off_voltage_feedback": (0, "V"),
    "load_off_voltage_feedback": (1, "V"),
    "load_on_voltage_feedback": (2, "V"),
    "evening_hours_feedback": (3, "h"),
    "interval_hours_feedback": (4, "h"),
    "dawn_hours_feedback": (5, "h"),
}

BINARY_SENSORS = {
    "load_path_fault": "set_load_path_fault_sensor",
    "charge_path_fault": "set_charge_path_fault_sensor",
    "settings_acknowledged": "set_settings_acknowledged_sensor",
    "setting_update_pending": "set_setting_update_pending_sensor",
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OoycyooMPPT),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_POLL_ENABLED, default=False): cv.boolean,
        cv.Optional(CONF_AUTO_RESYNC, default=True): cv.boolean,
        cv.Optional(CONF_WRITE_ATTEMPTS): sensor.sensor_schema(),
        cv.Required(CONF_PV_OFF): cv.float_range(min=0.0, max=99.9),
        cv.Required(CONF_LOAD_OFF): cv.float_range(min=0.0, max=99.9),
        cv.Required(CONF_LOAD_ON): cv.float_range(min=0.0, max=99.9),
        cv.Required(CONF_EVENING): cv.int_range(min=0, max=24),
        cv.Required(CONF_INTERVAL): cv.int_range(min=0, max=24),
        cv.Required(CONF_DAWN): cv.int_range(min=0, max=24),
        cv.Optional(CONF_LOAD_SWITCH): switch.switch_schema(OoycyooLoadSwitch),
        cv.Optional(CONF_RESET_BUTTON): button.button_schema(OoycyooResetButton),
        **{
            cv.Optional(key): number.number_schema(
                OoycyooSettingNumber, unit_of_measurement=unit
            )
            for key, (_, unit, _, _, _) in NUMBERS.items()
        },
        **{
            cv.Optional(key): sensor.sensor_schema(unit_of_measurement=unit)
            for key, (_, unit) in SENSORS.items()
        },
        **{
            cv.Optional(key): sensor.sensor_schema(unit_of_measurement=unit)
            for key, (_, unit) in SETTING_FEEDBACK_SENSORS.items()
        },
        **{
            cv.Optional(key): binary_sensor.binary_sensor_schema()
            for key in BINARY_SENSORS
        },
    }
).extend(cv.polling_component_schema("840ms"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_poll_enabled(config[CONF_POLL_ENABLED]))
    cg.add(var.set_auto_resync(config[CONF_AUTO_RESYNC]))
    # Saved requested settings are keyed by the controller block's id.
    cg.add(var.set_store_key(f"ooycyoo_mppt_{config[CONF_ID].id}"))
    if CONF_WRITE_ATTEMPTS in config:
        cg.add(
            var.set_write_attempts_sensor(
                await sensor.new_sensor(config[CONF_WRITE_ATTEMPTS])
            )
        )
    cg.add(
        var.set_settings(
            config[CONF_PV_OFF],
            config[CONF_LOAD_OFF],
            config[CONF_LOAD_ON],
            config[CONF_EVENING],
            config[CONF_INTERVAL],
            config[CONF_DAWN],
        )
    )

    for key, (setter, _) in SENSORS.items():
        if key in config:
            cg.add(getattr(var, setter)(await sensor.new_sensor(config[key])))
    for key, (setting, _) in SETTING_FEEDBACK_SENSORS.items():
        if key in config:
            cg.add(
                var.set_setting_feedback_sensor(
                    setting, await sensor.new_sensor(config[key])
                )
            )
    for key, setter in BINARY_SENSORS.items():
        if key in config:
            cg.add(
                getattr(var, setter)(
                    await binary_sensor.new_binary_sensor(config[key])
                )
            )
    if CONF_LOAD_SWITCH in config:
        load_switch = cg.new_Pvariable(config[CONF_LOAD_SWITCH][CONF_ID], var)
        await switch.register_switch(load_switch, config[CONF_LOAD_SWITCH])
        cg.add(var.set_load_switch(load_switch))
    if CONF_RESET_BUTTON in config:
        reset_button = cg.new_Pvariable(config[CONF_RESET_BUTTON][CONF_ID], var)
        await button.register_button(reset_button, config[CONF_RESET_BUTTON])
    for key, (setting, _, minimum, maximum, step) in NUMBERS.items():
        if key in config:
            entity = cg.new_Pvariable(config[key][CONF_ID], var, setting)
            await number.register_number(
                entity,
                config[key],
                min_value=minimum,
                max_value=maximum,
                step=step,
            )
            cg.add(var.set_setting_number(setting, entity))
