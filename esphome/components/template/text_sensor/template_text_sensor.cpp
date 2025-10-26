#include "template_text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

static const char *const TAG = "template.text_sensor";

// Template instantiations
template<typename F> void TemplateTextSensorBase<F>::dump_config() { LOG_TEXT_SENSOR("", "Template Sensor", this); }

template class TemplateTextSensorBase<std::function<optional<std::string>()>>;
template class TemplateTextSensorBase<optional<std::string> (*)()>;

}  // namespace template_
}  // namespace esphome
