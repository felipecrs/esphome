from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_CONDITION, CONF_ID, CONF_LAMBDA, CONF_STATE
from esphome.cpp_generator import LambdaExpression

from .. import template_ns

TemplateBinarySensor = template_ns.class_(
    "TemplateBinarySensor", binary_sensor.BinarySensor, cg.Component
)
StatelessTemplateBinarySensor = template_ns.class_(
    "StatelessTemplateBinarySensor", binary_sensor.BinarySensor, cg.Component
)

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(TemplateBinarySensor)
    .extend(
        {
            cv.Exclusive(CONF_LAMBDA, CONF_CONDITION): cv.returning_lambda,
            cv.Exclusive(
                CONF_CONDITION, CONF_CONDITION
            ): automation.validate_potentially_and_condition,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    # Check if we have a lambda first - determines which class to instantiate
    if lamb := config.get(CONF_LAMBDA):
        # Use new_lambda_pvariable to create either TemplateBinarySensor or StatelessTemplateBinarySensor
        template_ = await cg.process_lambda(
            lamb, [], return_type=cg.optional.template(bool)
        )
        var = automation.new_lambda_pvariable(
            config[CONF_ID], template_, StatelessTemplateBinarySensor
        )
        # Manually register as binary sensor since we didn't use new_binary_sensor
        await binary_sensor.register_binary_sensor(var, config)
        await cg.register_component(var, config)
    elif condition := config.get(CONF_CONDITION):
        # For conditions, create stateful version and set template
        var = await binary_sensor.new_binary_sensor(config)
        await cg.register_component(var, config)
        condition = await automation.build_condition(
            condition, cg.TemplateArguments(), []
        )
        template_ = LambdaExpression(
            f"return {condition.check()};", [], return_type=cg.optional.template(bool)
        )
        cg.add(var.set_template(template_))
    else:
        # No lambda or condition - just create the base template sensor
        var = await binary_sensor.new_binary_sensor(config)
        await cg.register_component(var, config)


@automation.register_action(
    "binary_sensor.template.publish",
    binary_sensor.BinarySensorPublishAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_STATE): cv.templatable(cv.boolean),
        }
    ),
)
async def binary_sensor_template_publish_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_STATE], args, bool)
    cg.add(var.set_state(template_))
    return var
