#include "template_sensor.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace template_ {

static const char *const TAG = "template.sensor";

// Template instantiations
template<typename F> void TemplateSensorBase<F>::dump_config() {
  LOG_SENSOR("", "Template Sensor", this);
  LOG_UPDATE_INTERVAL(this);
}

template class TemplateSensorBase<std::function<optional<float>()>>;
template class TemplateSensorBase<optional<float> (*)()>;

}  // namespace template_
}  // namespace esphome
