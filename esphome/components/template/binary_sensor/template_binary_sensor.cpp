#include "template_binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

static const char *const TAG = "template.binary_sensor";

// Template instantiations
template<typename F> void TemplateBinarySensorBase<F>::dump_config() {
  LOG_BINARY_SENSOR("", "Template Binary Sensor", this);
}

template class TemplateBinarySensorBase<std::function<optional<bool>()>>;
template class TemplateBinarySensorBase<optional<bool> (*)()>;

}  // namespace template_
}  // namespace esphome
