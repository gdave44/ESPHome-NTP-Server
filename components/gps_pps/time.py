import esphome.codegen as cg
from esphome.components import gps, sensor, time as time_
import esphome.config_validation as cv
from esphome import pins
from esphome.const import (
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
)

CONF_GPS_ID = "gps_id"
CONF_PPS_PIN = "pps_pin"
CONF_CLOCK_OFFSET = "clock_offset"
CONF_PPS_DRIFT = "pps_drift"

DEPENDENCIES = ["gps"]
AUTO_LOAD = ["sensor"]

gps_pps_ns = cg.esphome_ns.namespace("gps_pps")
GPSPPSTime = gps_pps_ns.class_("GPSPPSTime", time_.RealTimeClock, gps.GPSListener)

CONFIG_SCHEMA = time_.TIME_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(GPSPPSTime),
        cv.GenerateID(CONF_GPS_ID): cv.use_id(gps.GPS),
        cv.Required(CONF_PPS_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_CLOCK_OFFSET): sensor.sensor_schema(
            unit_of_measurement="µs",
            icon="mdi:clock-check-outline",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PPS_DRIFT): sensor.sensor_schema(
            unit_of_measurement="µs",
            icon="mdi:clock-alert-outline",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await time_.register_time(var, config)
    await cg.register_component(var, config)

    gps_parent = await cg.get_variable(config[CONF_GPS_ID])
    cg.add(gps_parent.register_listener(var))

    pps_pin = await cg.gpio_pin_expression(config[CONF_PPS_PIN])
    cg.add(var.set_pps_pin(pps_pin))

    if offset_config := config.get(CONF_CLOCK_OFFSET):
        sens = await sensor.new_sensor(offset_config)
        cg.add(var.set_clock_offset_sensor(sens))

    if drift_config := config.get(CONF_PPS_DRIFT):
        sens = await sensor.new_sensor(drift_config)
        cg.add(var.set_pps_drift_sensor(sens))
