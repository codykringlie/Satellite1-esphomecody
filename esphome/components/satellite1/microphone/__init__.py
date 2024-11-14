import esphome.config_validation as cv
import esphome.codegen as cg

from esphome import pins
from esphome.const import CONF_ID, CONF_NUMBER
from esphome.components import microphone, esp32
from esphome.components.adc import ESP32_VARIANT_ADC1_PIN_TO_CHANNEL, validate_adc_pin

from esphome.components.i2s_audio.i2s_settings import (
    BITS_PER_SAMPLE,
    CONF_CLK_MODE,
    I2S_CLK_MODES,
    EXTERNAL_CLK,
    CONF_CHANNEL,
    CONF_FIXED_SETTINGS,
    CHANNEL_FORMAT,
    _validate_bits,
) 

from esphome.components.i2s_audio import (
    I2SAudioComponent,
    I2SReader,
    CONF_BITS_PER_SAMPLE,
    CONF_I2S_AUDIO_ID,
    CONF_I2S_DIN_PIN,
    register_i2s_reader
)

CODEOWNERS = ["@gnumpi"]
DEPENDENCIES = ["i2s_audio"]

CONF_ADC_PIN = "adc_pin"
CONF_AMPLIFY_SHIFT = "amplify_shift"
CONF_CHANNEL_0 = "channel_0"
CONF_CHANNEL_1 = "channel_1"
CONF_PDM = "pdm"
CONF_SAMPLE_RATE = "sample_rate"
CONF_USE_APLL = "use_apll"


nabu_microphone_ns = cg.esphome_ns.namespace("nabu_microphone")

NabuMicrophone = nabu_microphone_ns.class_("NabuMicrophone", I2SReader, cg.Component)
NabuMicrophoneChannel = nabu_microphone_ns.class_(
    "NabuMicrophoneChannel", microphone.Microphone, cg.Component
)

i2s_channel_fmt_t = cg.global_ns.enum("i2s_channel_fmt_t")
CHANNELS = {
    "left": i2s_channel_fmt_t.I2S_CHANNEL_FMT_ONLY_LEFT,
    "right": i2s_channel_fmt_t.I2S_CHANNEL_FMT_ONLY_RIGHT,
}



MICROPHONE_CHANNEL_SCHEMA = microphone.MICROPHONE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(NabuMicrophoneChannel),
        cv.Optional(CONF_AMPLIFY_SHIFT, default=0): cv.All(
            cv.uint8_t, cv.Range(min=0, max=8)
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NabuMicrophone),
        cv.GenerateID(CONF_I2S_AUDIO_ID): cv.use_id(I2SAudioComponent),
        cv.Optional(CONF_SAMPLE_RATE, default=48000): cv.int_range(min=1),
        cv.Optional(CONF_BITS_PER_SAMPLE, default="32bit"): cv.All(
            _validate_bits, cv.enum(BITS_PER_SAMPLE)
        ),
        cv.Optional(CONF_CLK_MODE, default=EXTERNAL_CLK): cv.enum(I2S_CLK_MODES),
        cv.Optional(CONF_CHANNEL, default="right_left"): cv.enum(CHANNEL_FORMAT),
        cv.Optional(CONF_USE_APLL, default=False): cv.boolean,
        cv.Optional(CONF_CHANNEL_0): MICROPHONE_CHANNEL_SCHEMA,
        cv.Optional(CONF_CHANNEL_1): MICROPHONE_CHANNEL_SCHEMA,
        
        cv.Required(CONF_I2S_DIN_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_PDM): cv.boolean,
        cv.Optional(CONF_FIXED_SETTINGS, default=True): cv.boolean,
    
    }
).extend(cv.COMPONENT_SCHEMA)


def _supported_satellite1_settings(config):
    if config[CONF_PDM] :
        raise cv.Invalid("PDM is not supported for the Satellite1 microphone integration.")
    if config[CONF_BITS_PER_SAMPLE] != 32:
        raise cv.Invalid("I2S needs to be set to 32bit for the satellite1 microphone integration.")
    if config[CONF_SAMPLE_RATE] != 48000:
        raise cv.Invalid("I2S needs to be set to 48kHz, downsampling to 16kHz is hard coded.")
    


FINAL_VALIDATE_SCHEMA = _supported_satellite1_settings



async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await cg.register_parented(var, config[CONF_I2S_AUDIO_ID])

    if channel_0_config := config.get(CONF_CHANNEL_0):
        channel_0 = cg.new_Pvariable(channel_0_config[CONF_ID])
        await cg.register_component(channel_0, channel_0_config)
        await cg.register_parented(channel_0, config[CONF_ID])
        await microphone.register_microphone(channel_0, channel_0_config)
        cg.add(var.set_channel_0(channel_0))
        cg.add(channel_0.set_amplify_shift(channel_0_config[CONF_AMPLIFY_SHIFT]))

    if channel_1_config := config.get(CONF_CHANNEL_1):
        channel_1 = cg.new_Pvariable(channel_1_config[CONF_ID])
        await cg.register_component(channel_1, channel_1_config)
        await cg.register_parented(channel_1, config[CONF_ID])
        await microphone.register_microphone(channel_1, channel_1_config)
        cg.add(var.set_channel_1(channel_1))
        cg.add(channel_1.set_amplify_shift(channel_1_config[CONF_AMPLIFY_SHIFT]))

    await register_i2s_reader(var, config)

    cg.add_define("USE_OTA_STATE_CALLBACK")
