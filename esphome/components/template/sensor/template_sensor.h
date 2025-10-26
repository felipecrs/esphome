#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateSensorBase : public sensor::Sensor, public PollingComponent {
 public:
  void update() override {
    if (!this->f_.has_value())
      return;
    auto val = (*this->f_)();
    if (val.has_value()) {
      this->publish_state(*val);
    }
  }

  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  optional<F> f_;
};

class TemplateSensor : public TemplateSensorBase<std::function<optional<float>()>> {
 public:
  void set_template(std::function<optional<float>()> &&f) { this->f_ = f; }
};

/** Optimized template sensor for stateless lambdas (no capture).
 *
 * Uses function pointer instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function).
 */
class StatelessTemplateSensor : public TemplateSensorBase<optional<float> (*)()> {
 public:
  explicit StatelessTemplateSensor(optional<float> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome
