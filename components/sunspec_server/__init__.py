"""
ESPHome external component: sunspec_server
SunSpec Modbus TCP server — Growatt 3600 TL-X (single-phase, Model 101).
Optimised for ESP8266 (ESP-07).

Usage in YAML:
  external_components:
    - source:
        type: local
        path: components

  sunspec_server:
    port: 502
    ac_current_id:     ac_current
    ac_voltage_id:     ac_voltage
    ac_power_id:       ac_power
    grid_frequency_id: grid_frequency
    pv1_current_id:    pv1_current
    pv2_current_id:    pv2_current
    pv1_voltage_id:    pv1_voltage
    pv1_power_id:      pv1_power
    pv2_power_id:      pv2_power
    total_energy_id:   total_energy
    inverter_temp_id:  inverter_temp
    status_code_id:    status
    power_limit_id:    sunspec_power_limit
    wmax_lim_pct_id:   sunspec_wmax_lim_pct
    wmax_lim_ena_id:   sunspec_wmax_lim_ena
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, number
from esphome.const import CONF_ID

CONF_PORT             = "port"
CONF_AC_CURRENT       = "ac_current_id"
CONF_AC_VOLTAGE       = "ac_voltage_id"
CONF_AC_POWER         = "ac_power_id"
CONF_GRID_FREQUENCY   = "grid_frequency_id"
CONF_PV1_CURRENT      = "pv1_current_id"
CONF_PV2_CURRENT      = "pv2_current_id"
CONF_PV1_VOLTAGE      = "pv1_voltage_id"
CONF_PV1_POWER        = "pv1_power_id"
CONF_PV2_POWER        = "pv2_power_id"
CONF_TOTAL_ENERGY     = "total_energy_id"
CONF_INVERTER_TEMP    = "inverter_temp_id"
CONF_STATUS_CODE      = "status_code_id"
CONF_POWER_LIMIT      = "power_limit_id"

sunspec_ns = cg.esphome_ns.namespace("sunspec_server")
SunSpecServer = sunspec_ns.class_("SunSpecServer", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SunSpecServer),
        cv.Optional(CONF_PORT, default=502): cv.port,

        cv.Required(CONF_AC_CURRENT):     cv.use_id(sensor.Sensor),
        cv.Required(CONF_AC_VOLTAGE):     cv.use_id(sensor.Sensor),
        cv.Required(CONF_AC_POWER):       cv.use_id(sensor.Sensor),
        cv.Required(CONF_GRID_FREQUENCY): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PV1_CURRENT):    cv.use_id(sensor.Sensor),
        cv.Required(CONF_PV2_CURRENT):    cv.use_id(sensor.Sensor),
        cv.Required(CONF_PV1_VOLTAGE):    cv.use_id(sensor.Sensor),
        cv.Required(CONF_PV1_POWER):      cv.use_id(sensor.Sensor),
        cv.Required(CONF_PV2_POWER):      cv.use_id(sensor.Sensor),
        cv.Required(CONF_TOTAL_ENERGY):   cv.use_id(sensor.Sensor),
        cv.Required(CONF_INVERTER_TEMP):  cv.use_id(sensor.Sensor),
        cv.Required(CONF_STATUS_CODE):    cv.use_id(sensor.Sensor),

        cv.Required(CONF_POWER_LIMIT):    cv.use_id(number.Number),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_PORT])
    await cg.register_component(var, config)

    # Wire up sensors
    for conf_key, setter in [
        (CONF_AC_CURRENT,     "set_ac_current"),
        (CONF_AC_VOLTAGE,     "set_ac_voltage"),
        (CONF_AC_POWER,       "set_ac_power"),
        (CONF_GRID_FREQUENCY, "set_grid_frequency"),
        (CONF_PV1_CURRENT,    "set_pv1_current"),
        (CONF_PV2_CURRENT,    "set_pv2_current"),
        (CONF_PV1_VOLTAGE,    "set_pv1_voltage"),
        (CONF_PV1_POWER,      "set_pv1_power"),
        (CONF_PV2_POWER,      "set_pv2_power"),
        (CONF_TOTAL_ENERGY,   "set_total_energy"),
        (CONF_INVERTER_TEMP,  "set_inverter_temp"),
        (CONF_STATUS_CODE,    "set_status_code"),
    ]:
        sens = await cg.get_variable(config[conf_key])
        cg.add(getattr(var, setter)(sens))

    # Wire up power-limit number
    pwr = await cg.get_variable(config[CONF_POWER_LIMIT])
    cg.add(var.set_power_limit_number(pwr))
