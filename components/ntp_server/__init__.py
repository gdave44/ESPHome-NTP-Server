import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE
from esphome.const import CONF_ID, CONF_TIME

CODEOWNERS = ["@RobertJN64"]
DEPENDENCIES = ["time"]

ntp_server_ns = cg.esphome_ns.namespace("ntp_server")
NTP_Server = ntp_server_ns.class_("NTP_Server", cg.Component)

CONF_GPS_ID = "ntp_server_id"
CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NTP_Server),
        }
    )
)

async def to_code(config):
    if CORE.is_esp32 and CORE.using_arduino:
        cg.add_library("Ethernet", None)
        cg.add_library("WiFi", None)
        cg.add_library("SPI", None)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
