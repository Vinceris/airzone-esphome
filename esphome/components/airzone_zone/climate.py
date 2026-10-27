"""Airzone zone as an ESPHome climate entity (Innobus Pro6, Modbus RTU).

Reads come from the modbus_controller sensors you already declare (temperature,
setpoint, zone register 0, system register 0); writes go straight to the same
controller with function 06 (read-modify-write for register 0).
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, climate, modbus_controller, sensor

DEPENDENCIES = ["modbus_controller"]
AUTO_LOAD = ["climate"]

airzone_ns = cg.esphome_ns.namespace("airzone")
AirzoneZone = airzone_ns.class_("AirzoneZone", climate.Climate, cg.Component)

CONF_MODBUS_CONTROLLER_ID = "modbus_controller_id"
CONF_ZONE = "zone"
CONF_TEMPERATURE = "temperature"
CONF_SETPOINT = "setpoint"
CONF_ZONE_REGISTER = "zone_register"
CONF_SYSTEM_REGISTER = "system_register"
CONF_DEMAND = "demand"
CONF_MIN_TEMPERATURE = "min_temperature"
CONF_MAX_TEMPERATURE = "max_temperature"

CONFIG_SCHEMA = climate.climate_schema(AirzoneZone).extend(
    {
        cv.GenerateID(CONF_MODBUS_CONTROLLER_ID): cv.use_id(modbus_controller.ModbusController),
        cv.Required(CONF_ZONE): cv.int_range(min=1, max=8),
        cv.Required(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_SETPOINT): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ZONE_REGISTER): cv.use_id(sensor.Sensor),
        cv.Required(CONF_SYSTEM_REGISTER): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_DEMAND): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_MIN_TEMPERATURE, default=18.0): cv.float_,
        cv.Optional(CONF_MAX_TEMPERATURE, default=30.0): cv.float_,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    cg.add(var.set_controller(await cg.get_variable(config[CONF_MODBUS_CONTROLLER_ID])))
    cg.add(var.set_zone(config[CONF_ZONE]))
    cg.add(var.set_temperature_sensor(await cg.get_variable(config[CONF_TEMPERATURE])))
    cg.add(var.set_setpoint_sensor(await cg.get_variable(config[CONF_SETPOINT])))
    cg.add(var.set_zone_register(await cg.get_variable(config[CONF_ZONE_REGISTER])))
    cg.add(var.set_system_register(await cg.get_variable(config[CONF_SYSTEM_REGISTER])))
    if CONF_DEMAND in config:
        cg.add(var.set_demand_sensor(await cg.get_variable(config[CONF_DEMAND])))
    cg.add(var.set_limits(config[CONF_MIN_TEMPERATURE], config[CONF_MAX_TEMPERATURE]))
