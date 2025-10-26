#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateTextSensorBase : public text_sensor::TextSensor, public PollingComponent {
 public:
  void update() override {
    if (!this->f_.has_value())
      return;
    auto val = (*this->f_)();
    if (val.has_value()) {
      this->publish_state(*val);
    }
  }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void dump_config() override;

 protected:
  optional<F> f_;
};

class TemplateTextSensor : public TemplateTextSensorBase<std::function<optional<std::string>()>> {
 public:
  void set_template(std::function<optional<std::string>()> &&f) { this->f_ = f; }
};

/** Optimized template text sensor for stateless lambdas (no capture).
 *
 * Uses function pointer instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function).
 */
class StatelessTemplateTextSensor : public TemplateTextSensorBase<optional<std::string> (*)()> {
 public:
  explicit StatelessTemplateTextSensor(optional<std::string> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome
