import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE
from esphome.const import CONF_ID, CONF_PORT

CODEOWNERS = ["@gdave44"]
DEPENDENCIES = ["network"]

CONF_TIME_ID = "time_id"

ntp_server_ns = cg.esphome_ns.namespace("ntp_server")
gps_pps_ns = cg.esphome_ns.namespace("gps_pps")

NTP_Server = ntp_server_ns.class_("NTP_Server", cg.Component)
GPSPPSTime = gps_pps_ns.class_("GPSPPSTime")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NTP_Server),
        cv.Optional(CONF_PORT, default=123): cv.port,
        cv.Optional(CONF_TIME_ID): cv.use_id(GPSPPSTime),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    if CORE.is_esp32 and CORE.using_arduino:
        cg.add_library("WiFi", None)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_port(config[CONF_PORT]))

    if time_id := config.get(CONF_TIME_ID):
        time_source = await cg.get_variable(time_id)
        cg.add(var.set_time_source(time_source))
